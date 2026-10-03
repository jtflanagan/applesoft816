// gsrun: run an AppleSoft program the way a IIgs would, and record its output -
// under the IIgs ROM's own AppleSoft (the oracle), or under applesoft816's
// compiler and VM (--vm).  Both go through the same virtual monitor, so their
// outputs can be compared byte for byte.
//
// The machine is emu816 (the ares 65816, SingleStepTests-clean) with the IIgs
// ROM 3 in banks $FC-$FF and bank $FF's $C100-$FFFF visible in bank 0 (the
// language card reading ROM, the internal $Cx firmware).
//
// The monitor's I/O entries are taken over - the GS ROM's monitor reaches into
// firmware vectors in banks $E0/$E1 that only a real boot sets up, and the
// point is AppleSoft's behaviour, not the video hardware's:
//   COUT   $FDED  the byte is recorded; CH/CV move as the monitor's COUT1
//                 would in a 40-column window (CR, wrap, scroll)
//   GETLN  $FD6A  prompt ($33) and echo through COUT, the script's keys into
//                 $0200 (high bit set) up to Return, X = the length
//   RDKEY  $FD0C  the next key of the script, high bit set
//   WAIT   $FCA8  SPEED='s per-character delay: returns at once
//   HOME $FC58, CLREOP $FC42, CLREOL $FC9C, VTAB $FC22: recorded as markers
//                 ($01 'H'/'P'/'L', $01 'V' row) and the screen and cursor
//                 kept as COUT1 would
//   SETWND $FB4B  TEXT's and GR's window (the GS sends it to the firmware):
//                 the IIe's - top = A, full width, bottom 24, CV = 23 ($01 'W')
//   INPORT $FE8B, OUTPORT $FE95: IN#/PR# - KSW/CSW to the monitor's KEYIN /
//                 COUT1 for slot 0, $Cn00 otherwise ($01 'I'/'O' slot)
// $C000 reads "no key"; $C0xx otherwise reads 0 and holds nothing.  A jump
// into the firmware banks stops the run and names the last monitor entry.
//
//   gsrun ROM --prog TOKENS [--vm VM.bin [--cc cc.bin]] [--keys SCRIPT] [--out FILE] [--max N]
//
// --cc: compile with the compiler built for the 65816 (cc/), run on this
// machine, instead of the PC build - and check the two images are identical
// byte for byte; the compile's cycles are reported.
//
// ROM AppleSoft: cold start at $E000, the program installed at the first
// prompt, RUN typed; the output is every COUT byte from RUN until AppleSoft
// reaches RESTART (END, STOP, an error and its message).  --vm: the program
// compiled into the VM's bank and run by vm_run until it returns.  Key
// scripts: literal characters, {CR} {ESC} {SP}.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "cpu816.hpp"
extern "C" {
#include "compile.h"
}

namespace {

constexpr uint16_t COUT = 0xFDED, RDKEY = 0xFD0C, GETLN = 0xFD6A, MONWAIT = 0xFCA8, HOME = 0xFC58,
                   CLREOP = 0xFC42, CLREOL = 0xFC9C, VTAB = 0xFC22, SETWND = 0xFB4B, INPORT = 0xFE8B,
                   OUTPORT = 0xFE95, BASIC_COLD = 0xE000,
                   RESTART = 0xD43C;
constexpr int CH = 0x24, CV = 0x25, WNDLFT = 0x20, WNDWDTH = 0x21, WNDTOP = 0x22, WNDBTM = 0x23;

// where the VM goes (any bank and pages would do)
constexpr uint32_t VM_BANK = 0x050000, VM_DP = 0x9800, VM_STUB = 0x9900, SCRATCH = 0x9A00, CALL_AT = 0x9B80;
constexpr uint8_t STRING_BANK = 0x06;
// its direct page (vm.s / fp.s)
constexpr int DP_SH_BANK = 0x48, DP_COUT_VEC = 0x2A, DP_FLASH = 0x2D, DP_SPEED = 0x2E, DP_ERR = 0x2F, DP_RNDSEED = 0xC9,
              DP_FP_TXT = 0xFA, DP_FP_ERR = 0xF2, DP_FAC = 0x9D, DP_FAC_EXT = 0xAC;

std::vector<uint8_t> slurp(const char* p)
{
  std::vector<uint8_t> b;
  FILE* f = fopen(p, "rb");
  if (!f) { perror(p); exit(2); }
  int c;
  while ((c = fgetc(f)) != EOF) b.push_back(uint8_t(c));
  fclose(f);
  return b;
}

std::vector<uint8_t> parse_keys(const std::string& t)
{
  std::vector<uint8_t> k;
  bool bol = true;
  for (size_t i = 0; i < t.size(); i++) {
    if (bol && t[i] == '#') { // a comment line (asprof's scripts have them)
      while (i < t.size() && t[i] != '\n') i++;
      continue;
    }
    bol = t[i] == '\n';
    if (t[i] == '\n' || t[i] == '\r') continue;
    if (t[i] == '{') {
      size_t e = t.find('}', i);
      std::string n = t.substr(i + 1, e - i - 1);
      if (n != "SNAP") k.push_back(n == "CR" ? 0x0D : n == "ESC" ? 0x1B : n == "SP" ? 0x20 : 0);
      i = e;
      continue;
    }
    k.push_back(uint8_t(t[i]));
  }
  return k;
}

struct Machine {
  Cpu816 c;
  std::vector<uint8_t> keys;
  size_t kpos = 0;
  bool recording = false, done = false;
  std::vector<uint8_t> out;
  uint32_t last_monitor = 0;
  const char* why = "instruction limit";
  std::function<void()> on_getln_first; // ROM mode: install the program
  std::vector<uint8_t> screen;           // the text page when the program ended
  // the text/lo-res page when the program ended; the hi-res pages too if it
  // touched HIRES ($C057; not always: ROM AppleSoft's variables may live there)
  bool hires = false;
  std::vector<uint8_t> hgr;
  void snap()
  {
    if (!screen.empty()) return;
    screen.assign(&c.mem[0x400], &c.mem[0x800]);
    if (hires) hgr.assign(&c.mem[0x2000], &c.mem[0x6000]);
  }
  long long trace = 0;                   // GSRUN_TRACE=n: the VM run's first n instructions
  uint32_t ring[64] = {0};
  uint32_t break_pc = getenv("GSRUN_BREAK") ? strtoul(getenv("GSRUN_BREAK"), nullptr, 16) : 0;
  int break_n = 0;
  uint32_t guard_end = 0;                // GSRUN_GUARD: report writes into the VM image below this
  int guard_n = 0;
  unsigned ring_i = 0;
  std::map<uint32_t, long>* hist = nullptr; // GSRUN_HIST/GSRUN_CCPROF: cycles spent at each PC

  explicit Machine(const std::vector<uint8_t>& rom)
  {
    memcpy(&c.mem[0xFC0000], rom.data(), rom.size());
    c.on_read = [this](uint32_t a, uint8_t& v) {
      if ((a & 0xFFFF) == 0xC057 && (a >> 16 == 0 || a >> 16 == 0xE0)) hires = true;
      if (a >= 0xC100 && a <= 0xFFFF) { v = c.mem[0xFF0000 | a]; return true; } // ROM / firmware
      if (a >= 0xC000 && a <= 0xC0FF) { v = 0; return true; }                    // I/O: no key, 40 col
      return false;
    };
    c.on_write = [this](uint32_t a, uint8_t) {
      if (a == 0xC057) hires = true;
      if (guard_end && a >= VM_BANK && a < VM_BANK + guard_end && guard_n < 400000) {   // GSRUN_GUARD
        guard_n++;
        fprintf(stderr, "guard: write $%06X at PC $%06X\n", a, c.pc());
        if (getenv("GSRUN_GUARD_PC") && c.pc() == strtoul(getenv("GSRUN_GUARD_PC"), nullptr, 16) && guard_n < 400000) {
          for (int k = 0; k < 64; k++) fprintf(stderr, "%06X%c", ring[(ring_i + k) & 63], k % 16 == 15 ? '\n' : ' ');
          fprintf(stderr, "x=%04X y=%04X s=%04X\n", c.x(), c.y(), c.s());
          guard_n = 400000;
        }
      }
      return a >= 0xC000 && a <= 0xFFFF;
    };
    // the monitor's text window and a clear screen, as INIT leaves them
    c.mem[WNDLFT] = 0, c.mem[WNDWDTH] = 40, c.mem[WNDTOP] = 0, c.mem[WNDBTM] = 24;
    c.mem[0x32] = 0xFF; // INVFLG: normal
    for (int a = 0x400; a < 0x800; a++) c.mem[a] = 0xA0;
  }

  void record(uint8_t b) { if (recording && !done) out.push_back(b); }

  // the text page as the monitor keeps it: row r's address, the window's left
  // edge added (BASL), CH counting from there
  uint32_t row(int r) { return 0x400 + (r & 7) * 0x80 + (r >> 3) * 0x28 + c.mem[WNDLFT]; }
  void clear_line(int r, int from)
  {
    for (int x = from; x < c.mem[WNDWDTH]; x++) c.mem[row(r) + x] = 0xA0;
  }
  void line_feed()
  {
    if (++c.mem[CV] < c.mem[WNDBTM]) return;
    c.mem[CV] = uint8_t(c.mem[WNDBTM] - 1);
    for (int r = c.mem[WNDTOP]; r + 1 < c.mem[WNDBTM]; r++) // SCROLL
      for (int x = 0; x < c.mem[WNDWDTH]; x++) c.mem[row(r) + x] = c.mem[row(r + 1) + x];
    clear_line(c.mem[WNDBTM] - 1, 0);
  }
  // COUT1 ($FDF0): printable characters through INVFLG onto the screen,
  // CR, LF, BS, BELL
  void cout(uint8_t ch)
  {
    record(ch);
    uint8_t b = ch | 0x80;
    if (b >= 0xA0) {
      c.mem[row(c.mem[CV]) + c.mem[CH]] = b & c.mem[0x32];
      if (++c.mem[CH] >= c.mem[WNDWDTH]) c.mem[CH] = 0, line_feed();
      return;
    }
    switch (b) {
    case 0x8D: c.mem[CH] = 0; line_feed(); break;
    case 0x8A: line_feed(); break;
    case 0x88:
      if (c.mem[CH] > 0) c.mem[CH]--;
      else if (c.mem[CV] > c.mem[WNDTOP]) c.mem[CH] = uint8_t(c.mem[WNDWDTH] - 1), c.mem[CV]--;
      break;
    }
  }
  void home()
  {
    for (int r = c.mem[WNDTOP]; r < c.mem[WNDBTM]; r++) clear_line(r, 0);
    c.mem[CH] = 0, c.mem[CV] = c.mem[WNDTOP];
  }
  bool next_key(uint8_t& k)
  {
    if (kpos >= keys.size()) return false;
    k = keys[kpos++] | 0x80;
    return true;
  }
  void set_a(uint16_t a) { c.set_regs(a, c.x(), c.y(), c.s(), c.d(), c.dbr(), c.p(), c.e()); }
  void set_x(uint16_t x) { c.set_regs(c.a(), x, c.y(), c.s(), c.d(), c.dbr(), c.p(), c.e()); }

  // one step of the machine, the monitor's entries taken over; false = stop
  bool step()
  {
    uint32_t pc = c.pc();
    if (pc >= 0xF800 && pc <= 0xFFFF) last_monitor = pc;
    if ((pc >> 16) == 0xE0 || (pc >> 16) == 0xE1) {
      fprintf(stderr, "gsrun: into firmware at %06X after monitor %04X\n", pc, last_monitor);
      why = "unmodelled firmware call";
      return false;
    }
    switch (pc) {
    case COUT: cout(uint8_t(c.a())); c.sim_rts(); return true;
    case MONWAIT: c.sim_rts(); return true;
    case HOME: case CLREOP: case CLREOL:
      record(0x01), record(pc == HOME ? 'H' : pc == CLREOP ? 'P' : 'L');
      if (pc == HOME) home();
      else {
        clear_line(c.mem[CV], c.mem[CH]);
        if (pc == CLREOP)
          for (int r = c.mem[CV] + 1; r < c.mem[WNDBTM]; r++) clear_line(r, 0);
      }
      c.sim_rts();
      return true;
    case VTAB: record(0x01), record('V'), record(c.mem[CV]); c.sim_rts(); return true;
    case INPORT: case OUTPORT: {
      uint8_t slot = c.a() & 0x0F;
      int vec = pc == INPORT ? 0x38 : 0x36;
      uint16_t to = slot ? uint16_t(0xC000 | slot << 8) : pc == INPORT ? 0xFD1B : 0xFDF0;
      c.mem[vec] = uint8_t(to), c.mem[vec + 1] = uint8_t(to >> 8);
      record(0x01), record(pc == INPORT ? 'I' : 'O'), record(slot);
      c.sim_rts();
      return true;
    }
    case SETWND:
      c.mem[WNDTOP] = uint8_t(c.a()), c.mem[WNDLFT] = 0, c.mem[WNDWDTH] = 40, c.mem[WNDBTM] = 24;
      c.mem[CV] = 23;
      record(0x01), record('W'), record(c.mem[WNDTOP]);
      c.sim_rts();
      return true;
    case GETLN: {
      if (on_getln_first) { auto f = on_getln_first; on_getln_first = nullptr; f(); }
      cout(c.mem[0x33]); // the prompt
      int len = 0;
      uint8_t k = 0;
      bool got = false;
      while ((got = next_key(k))) {
        cout(k);
        c.mem[0x200 + len] = k;
        if ((k & 0x7F) == 0x0D) break;
        len++;
      }
      if (!got) { why = done ? "back at the prompt" : "waiting for input"; return false; }
      set_x(uint16_t(len));
      c.sim_rts();
      return true;
    }
    case RDKEY: {
      uint8_t k;
      if (!next_key(k)) { why = done ? "back at the prompt" : "waiting for a key"; return false; }
      set_a(k);
      c.sim_rts();
      return true;
    }
    }
    c.step();
    if (c.halted()) { why = "STP"; return false; }
    return true;
  }

  // JSL to `entry` in native mode with D = dp; run to the STP after it
  bool call_native(uint32_t entry, uint16_t dp, long long max)
  {
    uint8_t stub[] = {0x22, uint8_t(entry), uint8_t(entry >> 8), uint8_t(entry >> 16), 0xDB};
    memcpy(&c.mem[CALL_AT], stub, sizeof stub);
    c.reset_regs();
    // S below $01FC, as AppleSoft keeps it: INPUT puts a ',' at $01FF
    c.set_regs(0, 0, 0, 0x01FB, dp, 0, 0x30, false);
    c.set_pc(CALL_AT);
    for (long long n = 0; n < max; n++) {
      if (trace > 0 && n < trace)
        fprintf(stderr, "%06X a=%04X x=%04X y=%04X s=%04X d=%04X b=%02X p=%02X\n", c.pc(), c.a(), c.x(), c.y(),
                c.s(), c.d(), c.dbr(), c.p());
      uint32_t pc = c.pc();
      uint64_t c0 = c.cycles;
      ring[ring_i++ & 63] = pc;
      if (break_pc && pc == break_pc && break_n++ < 60) {   // GSRUN_BREAK=pc: registers there
        fprintf(stderr, "break %06X: a=%04X x=%04X y=%04X d=%04X dp20-4F:", pc, c.a(), c.x(), c.y(), c.d());
        for (int k = 0x30; k < 0x50; k++) fprintf(stderr, " %02X", c.mem[c.d() + k]);
        fprintf(stderr, "\n");
      }
      if (!step()) {
        if (getenv("GSRUN_LAST")) {               // the last 64 PCs before it stopped
          for (int k = 0; k < 64; k++) fprintf(stderr, "%06X%c", ring[(ring_i + k) & 63], k % 16 == 15 ? '\n' : ' ');
        }
        return c.halted();
      }
      if (hist) (*hist)[pc] += long(c.cycles - c0);
    }
    return false;
  }
};

// ---- the compiler's platform, on the emulated machine ----
struct VmPlatform {
  Machine* m;
  std::vector<uint8_t> prog;
  uint32_t fp_table;
};

void pf_read(void* ctx, uint16_t a, uint8_t* dst, uint16_t n)
{
  auto* v = static_cast<VmPlatform*>(ctx);
  for (uint16_t k = 0; k < n; k++) {
    size_t i = size_t(uint16_t(a + k)) - 0x801;
    dst[k] = i < v->prog.size() ? v->prog[i] : 0;
  }
}
void pf_write(void* ctx, uint16_t a, const uint8_t* src, uint16_t n)
{
  for (uint16_t k = 0; k < n; k++) static_cast<VmPlatform*>(ctx)->m->c.mem[VM_BANK | uint16_t(a + k)] = src[k];
}

// FIN: fp816's own, run on the machine
int pf_fin(void* ctx, const uint8_t* text, int len, uint8_t fac[7])
{
  auto* v = static_cast<VmPlatform*>(ctx);
  Cpu816& c = v->m->c;
  memcpy(&c.mem[SCRATCH], text, size_t(len));
  c.mem[SCRATCH + len] = 0;
  c.mem[VM_DP + DP_FP_TXT] = uint8_t(SCRATCH - 1);
  c.mem[VM_DP + DP_FP_TXT + 1] = uint8_t((SCRATCH - 1) >> 8);
  c.mem[VM_DP + DP_FP_TXT + 2] = 0;
  c.mem[VM_DP + DP_FAC_EXT] = 0;
  bool rec = v->m->recording;
  v->m->recording = false;
  bool ok = v->m->call_native(v->fp_table + 0x50, VM_DP, 1000000);
  v->m->recording = rec;
  if (!ok || c.mem[VM_DP + DP_FP_ERR]) return -1;
  memcpy(fac, &c.mem[VM_DP + DP_FAC], 6);
  fac[6] = c.mem[VM_DP + DP_FAC_EXT];
  return int(c.mem[VM_DP + DP_FP_TXT] | c.mem[VM_DP + DP_FP_TXT + 1] << 8) - int(SCRATCH);
}

void show(const std::vector<uint8_t>& out)
{
  for (size_t i = 0; i < out.size(); i++) {
    uint8_t b = out[i];
    if (b == 0x01 && i + 1 < out.size() && out[i + 1] == 'S' && i + 2 + 1024 <= out.size()) {
      printf("\n---- the screen at the end ----\n");
      for (int r = 0; r < 24; r++) {
        for (int x = 0; x < 40; x++) {
          uint8_t ch = out[i + 2 + (r & 7) * 0x80 + (r >> 3) * 0x28 + x] & 0x7F;
          putchar(ch < 0x20 ? ch + 0x40 : ch);
        }
        putchar('\n');
      }
      i += 1 + 1024;
      continue;
    }
    if (b == 0x01 && i + 1 < out.size()) {
      char k = char(out[++i]);
      if (k == 'V' && i + 1 < out.size()) printf("{VTAB %d}", out[++i] + 1);
      else if (k == 'W' && i + 1 < out.size()) printf("{WINDOW TOP %d}", out[++i]);
      else if ((k == 'I' || k == 'O') && i + 1 < out.size()) printf("{%s#%d}", k == 'I' ? "IN" : "PR", out[++i]);
      else printf("{%s}", k == 'H' ? "HOME" : k == 'P' ? "CLREOP" : "CLREOL");
      continue;
    }
    putchar((b & 0x7F) == 0x0D ? '\n' : (b & 0x7F) < 0x20 ? '^' : b & 0x7F);
  }
  putchar('\n');
}

} // namespace

int main(int argc, char** argv)
{
  const char *rom_path = nullptr, *prog_path = nullptr, *keys_path = nullptr, *out_path = nullptr,
             *vm_path = nullptr, *cc_path = nullptr;
  long long max_insns = 2000000000;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--prog" && i + 1 < argc) prog_path = argv[++i];
    else if (a == "--keys" && i + 1 < argc) keys_path = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (a == "--vm" && i + 1 < argc) vm_path = argv[++i];
    else if (a == "--cc" && i + 1 < argc) cc_path = argv[++i];
    else if (a == "--max" && i + 1 < argc) max_insns = atoll(argv[++i]);
    else if (!rom_path) rom_path = argv[i];
    else rom_path = nullptr, i = argc;
  }
  if (!rom_path || !prog_path) {
    fprintf(stderr, "usage: gsrun ROM --prog TOKENS [--vm VM.bin] [--keys SCRIPT] [--out FILE] [--max N]\n");
    return 2;
  }
  auto rom = slurp(rom_path);
  if (rom.size() != 0x40000) { fprintf(stderr, "%s: want the 256K ROM 3\n", rom_path); return 2; }
  auto prog = slurp(prog_path);

  Machine m(rom);
  if (keys_path) {
    auto k = slurp(keys_path);
    m.keys = parse_keys(std::string(k.begin(), k.end()));
  }
  uint64_t run_cycles = 0;

  if (!vm_path) {
    // ROM AppleSoft: type RUN at the first prompt, the program installed there
    auto script = m.keys;
    m.keys = parse_keys("RUN{CR}");
    m.keys.insert(m.keys.end(), script.begin(), script.end());
    m.on_getln_first = [&] {
      memcpy(&m.c.mem[0x801], prog.data(), prog.size());
      m.c.mem[0x800] = 0;
      uint16_t end = uint16_t(0x801 + prog.size());
      m.c.mem[0x67] = 0x01, m.c.mem[0x68] = 0x08;
      for (int z = 0x69; z <= 0x6D; z += 2) m.c.mem[z] = uint8_t(end), m.c.mem[z + 1] = uint8_t(end >> 8);
      m.c.mem[0xAF] = uint8_t(end), m.c.mem[0xB0] = uint8_t(end >> 8);
    };
    m.c.reset_regs();
    m.c.set_regs(0, 0, 0, 0x01FF, 0, 0, 0x34, true);
    m.c.set_pc(BASIC_COLD);
    uint64_t start = 0;
    for (long long n = 0; n < max_insns; n++) {
      uint32_t pc = m.c.pc();
      if (!m.recording && m.kpos == 4 && pc != GETLN) { // RUN is in: from a clear screen, as the VM
        m.recording = true, start = m.c.cycles;
        m.home();
      }
      if (pc == RESTART && m.recording && !m.done) m.done = true, run_cycles = m.c.cycles - start, m.snap();
      if (!m.step()) break;
    }
  } else {
    // applesoft816: compile into the VM's bank, run it
    auto vm = slurp(vm_path);
    if (vm.size() < 0x18 || memcmp(&vm[4], "A816", 4)) { fprintf(stderr, "%s: not a VM image\n", vm_path); return 2; }
    memcpy(&m.c.mem[VM_BANK], vm.data(), vm.size());
    auto w = [&](size_t o) { return uint16_t(vm[o] | vm[o + 1] << 8); };
    uint16_t nops = w(0x0A), optab = w(0x0C), prog_start = w(0x0E), stub = w(0x10), stub_len = w(0x12);
    std::vector<uint16_t> handlers(nops);
    for (uint16_t i = 0; i < nops; i++) handlers[i] = w(optab + 2 * i);
    if (nops != OP_COUNT) { fprintf(stderr, "the VM has %u operations, the compiler %u\n", nops, OP_COUNT); return 2; }
    memcpy(&m.c.mem[VM_STUB], &vm[stub], stub_len);
    uint8_t* dp = &m.c.mem[VM_DP];
    dp[DP_COUT_VEC] = uint8_t(VM_STUB), dp[DP_COUT_VEC + 1] = uint8_t(VM_STUB >> 8), dp[DP_COUT_VEC + 2] = 0;
    dp[DP_FLASH] = 0, dp[DP_SPEED] = 1;
    dp[DP_SH_BANK] = STRING_BANK;
    // AppleSoft's cold start copies only $C9-$CC of its RND seed from the
    // ROM (80 4F C7 52); $CD keeps whatever memory held - 0 here, as it is in
    // the ROM run on this machine
    const uint8_t seed[5] = {0x80, 0x4F, 0xC7, 0x52, 0x00};
    memcpy(dp + DP_RNDSEED, seed, 5);

    VmPlatform vp{&m, prog, VM_BANK | w(0x16)};
    Platform pf{};
    pf.ctx = &vp, pf.read = pf_read, pf.write = pf_write, pf.fin = pf_fin;
    pf.prog_start = prog_start, pf.optable = handlers.data();
    CompileStats st{};
    if (as816_compile(&pf, &st) != 0) { fprintf(stderr, "gsrun: the program does not fit the VM bank\n"); return 1; }
    fprintf(stderr, "gsrun: compiled %u lines: %u bytes of code, %u variables, %u arrays, %u constants\n",
            st.lines, st.code_size, st.vars, st.arrays, st.consts);
    if (const char* img = getenv("GSRUN_IMAGE")) {   // FILE: the compiled image, prog_start to its end
      FILE* f = fopen(img, "wb");
      fwrite(&m.c.mem[VM_BANK + prog_start], 1, st.end - prog_start, f);
      fclose(f);
    }
    if (cc_path) {
      // the same compile by the 65816 build, on the machine; the PC build's
      // image is the reference
      std::vector<uint8_t> ref(&m.c.mem[VM_BANK], &m.c.mem[VM_BANK] + 0x10000);
      memcpy(&m.c.mem[VM_BANK], vm.data(), vm.size());
      memset(&m.c.mem[VM_BANK] + vm.size(), 0, 0x10000 - vm.size());
      auto cc = slurp(cc_path);
      if (cc.size() < 12 || memcmp(&cc[4], "CC16", 4)) { fprintf(stderr, "%s: not a compiler image\n", cc_path); return 2; }
      constexpr uint32_t CC_BANK = 0x070000, TEXT_BANK = 0x090000;
      memcpy(&m.c.mem[CC_BANK], cc.data(), cc.size());
      memcpy(&m.c.mem[TEXT_BANK + 0x801], prog.data(), prog.size());
      m.c.mem[TEXT_BANK + 0x801 + prog.size()] = 0, m.c.mem[TEXT_BANK + 0x802 + prog.size()] = 0;
      uint32_t params = 0x080000 | (cc[8] | cc[9] << 8);
      m.c.mem[params + 0] = uint8_t(VM_BANK >> 16);
      m.c.mem[params + 1] = uint8_t(TEXT_BANK >> 16);
      m.c.mem[params + 2] = uint8_t(VM_DP), m.c.mem[params + 3] = uint8_t(VM_DP >> 8);
      m.c.mem[params + 4] = 0xAA, m.c.mem[params + 5] = 0xAA;
      uint64_t c0 = m.c.cycles;
      std::map<uint32_t, long> cch;
      const char* prof = getenv("GSRUN_CCPROF");   // FILE: "pc cycles" lines (tools/ccprof.py)
      if (prof) m.hist = &cch;
      bool ok = m.call_native(CC_BANK, 0, 400000000);
      m.hist = nullptr;
      if (prof) {
        FILE* f = fopen(prof, "w");
        for (auto& [k, n] : cch) fprintf(f, "%06X %ld\n", k, n);
        fclose(f);
      }
      uint64_t cc_cycles = m.c.cycles - c0;
      int16_t res = int16_t(m.c.mem[params + 4] | m.c.mem[params + 5] << 8);
      int diffs = 0, first = -1;
      for (uint32_t a = prog_start; a < st.end; a++)
        if (m.c.mem[VM_BANK + a] != ref[a]) { diffs++; if (first < 0) first = int(a); }
      fprintf(stderr, "gsrun: the 65816 compiler: %s, result %d, %llu cycles (%.2f s at 2.8 MHz, %.2f s at 1 MHz); "
              "%d bytes differ from the PC build%s\n", ok ? "returned" : "did not return", res,
              (unsigned long long)cc_cycles, cc_cycles / 2.8e6, cc_cycles / 1.0205e6, diffs,
              first >= 0 ? (" (first at $" + std::to_string(first) + ")").c_str() : "");
      if (!ok || res != 0 || diffs) return 1;
    }
    m.recording = true;
    if (getenv("GSRUN_GUARD")) m.guard_end = prog_start;
    if (getenv("GSRUN_TRACE")) m.trace = atoll(getenv("GSRUN_TRACE"));
    std::map<uint32_t, long> hist;
    if (getenv("GSRUN_HIST")) m.hist = &hist;
    uint64_t start = m.c.cycles;
    m.call_native(VM_BANK, uint16_t(VM_DP), max_insns);
    if (m.hist) {
      std::vector<std::pair<long, uint32_t>> v;
      for (auto& [k, n] : hist) v.push_back({n, k});
      std::sort(v.rbegin(), v.rend());
      for (size_t i = 0; i < 20 && i < v.size(); i++)
        fprintf(stderr, "  %06X %ld\n", v[i].second, v[i].first);
      if (strcmp(getenv("GSRUN_HIST"), "1")) {    // GSRUN_HIST=FILE: every PC, "pc cycles"
        FILE* f = fopen(getenv("GSRUN_HIST"), "w");
        for (auto& [k, n] : hist) fprintf(f, "%06X %ld\n", k, n);
        fclose(f);
      }
    }
    run_cycles = m.c.cycles - start;
    if (m.c.halted()) {
      uint8_t e = dp[DP_ERR];
      m.why = e == 0 ? "END" : e == 0xFE ? "not implemented yet" : "error";
    }
  }

  // the text / lo-res page when the program ended, compared too
  m.snap();
  m.out.push_back(0x01), m.out.push_back('S');
  m.out.insert(m.out.end(), m.screen.begin(), m.screen.end());
  if (!m.hgr.empty()) {
    m.out.push_back(0x01), m.out.push_back('H');
    m.out.insert(m.out.end(), m.hgr.begin(), m.hgr.end());
  }
  fprintf(stderr, "gsrun: %s; %zu output bytes; %llu cycles in the program\n", m.why, m.out.size() - 1026 - (m.hgr.empty() ? 0 : m.hgr.size() + 2),
          (unsigned long long)run_cycles);
  if (out_path) {
    FILE* f = fopen(out_path, "wb");
    fwrite(m.out.data(), 1, m.out.size(), f);
    fclose(f);
  } else
    show(m.out);
  return 0;
}
