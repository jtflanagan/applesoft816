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
//                 ($01 'H'/'P'/'L', $01 'V' row) and the cursor moved
// $C000 reads "no key"; $C0xx otherwise reads 0 and holds nothing.  A jump
// into the firmware banks stops the run and names the last monitor entry.
//
//   gsrun ROM --prog TOKENS [--vm VM.bin] [--keys SCRIPT] [--out FILE] [--max N]
//
// ROM AppleSoft: cold start at $E000, the program installed at the first
// prompt, RUN typed; the output is every COUT byte from RUN until AppleSoft
// reaches RESTART (END, STOP, an error and its message).  --vm: the program
// compiled into the VM's bank and run by vm_run until it returns.  Key
// scripts: literal characters, {CR} {ESC} {SP}.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "cpu816.hpp"
extern "C" {
#include "compile.h"
}

namespace {

constexpr uint16_t COUT = 0xFDED, RDKEY = 0xFD0C, GETLN = 0xFD6A, MONWAIT = 0xFCA8, HOME = 0xFC58,
                   CLREOP = 0xFC42, CLREOL = 0xFC9C, VTAB = 0xFC22, BASIC_COLD = 0xE000, RESTART = 0xD43C;
constexpr int CH = 0x24, CV = 0x25, WNDLFT = 0x20, WNDWDTH = 0x21, WNDTOP = 0x22, WNDBTM = 0x23;

// where the VM goes (any bank and pages would do)
constexpr uint32_t VM_BANK = 0x050000, VM_DP = 0x9800, VM_STUB = 0x9900, SCRATCH = 0x0400, CALL_AT = 0x0300;
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
  for (size_t i = 0; i < t.size(); i++) {
    if (t[i] == '\n' || t[i] == '\r') continue;
    if (t[i] == '{') {
      size_t e = t.find('}', i);
      std::string n = t.substr(i + 1, e - i - 1);
      k.push_back(n == "CR" ? 0x0D : n == "ESC" ? 0x1B : n == "SP" ? 0x20 : 0);
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
  long long trace = 0;                   // GSRUN_TRACE=n: the VM run's first n instructions

  explicit Machine(const std::vector<uint8_t>& rom)
  {
    memcpy(&c.mem[0xFC0000], rom.data(), rom.size());
    c.on_read = [this](uint32_t a, uint8_t& v) {
      if (a >= 0xC100 && a <= 0xFFFF) { v = c.mem[0xFF0000 | a]; return true; } // ROM / firmware
      if (a >= 0xC000 && a <= 0xC0FF) { v = 0; return true; }                    // I/O: no key, 40 col
      return false;
    };
    c.on_write = [](uint32_t a, uint8_t) { return a >= 0xC000 && a <= 0xFFFF; };
    // the monitor's text window, as INIT leaves it
    c.mem[WNDLFT] = 0, c.mem[WNDWDTH] = 40, c.mem[WNDTOP] = 0, c.mem[WNDBTM] = 24;
  }

  void record(uint8_t b) { if (recording && !done) out.push_back(b); }
  void cout(uint8_t ch)
  {
    record(ch);
    if ((ch & 0x7F) == 0x0D) c.mem[CH] = c.mem[WNDLFT], c.mem[CV]++;
    else if (++c.mem[CH] >= c.mem[WNDWDTH]) c.mem[CH] = c.mem[WNDLFT], c.mem[CV]++;
    if (c.mem[CV] >= c.mem[WNDBTM]) c.mem[CV] = c.mem[WNDBTM] - 1; // scrolled
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
      if (pc == HOME) c.mem[CH] = c.mem[WNDLFT], c.mem[CV] = c.mem[WNDTOP];
      c.sim_rts();
      return true;
    case VTAB: record(0x01), record('V'), record(c.mem[CV]); c.sim_rts(); return true;
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
    c.set_regs(0, 0, 0, 0x01FF, dp, 0, 0x30, false);
    c.set_pc(CALL_AT);
    for (long long n = 0; n < max; n++) {
      if (trace > 0 && n < trace)
        fprintf(stderr, "%06X a=%04X x=%04X y=%04X s=%04X d=%04X b=%02X p=%02X\n", c.pc(), c.a(), c.x(), c.y(),
                c.s(), c.d(), c.dbr(), c.p());
      if (!step()) return c.halted();
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

uint8_t pf_prog(void* ctx, uint16_t a)
{
  auto* v = static_cast<VmPlatform*>(ctx);
  size_t i = size_t(a) - 0x801;
  return i < v->prog.size() ? v->prog[i] : 0;
}
void pf_wr(void* ctx, uint16_t a, uint8_t b) { static_cast<VmPlatform*>(ctx)->m->c.mem[VM_BANK | a] = b; }

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
    if (b == 0x01 && i + 1 < out.size()) {
      char k = char(out[++i]);
      if (k == 'V' && i + 1 < out.size()) printf("{VTAB %d}", out[++i] + 1);
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
             *vm_path = nullptr;
  long long max_insns = 2000000000;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--prog" && i + 1 < argc) prog_path = argv[++i];
    else if (a == "--keys" && i + 1 < argc) keys_path = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (a == "--vm" && i + 1 < argc) vm_path = argv[++i];
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
      if (!m.recording && m.kpos == 4 && pc != GETLN) m.recording = true, start = m.c.cycles; // RUN is in
      if (pc == RESTART && m.recording && !m.done) m.done = true, run_cycles = m.c.cycles - start;
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
    pf.ctx = &vp, pf.prog = pf_prog, pf.wr = pf_wr, pf.fin = pf_fin;
    pf.prog_start = prog_start, pf.optable = handlers.data();
    CompileStats st{};
    if (as816_compile(&pf, &st) != 0) { fprintf(stderr, "gsrun: the program does not fit the VM bank\n"); return 1; }
    fprintf(stderr, "gsrun: compiled %u lines: %u bytes of code, %u variables, %u arrays, %u constants\n",
            st.lines, st.code_size, st.vars, st.arrays, st.consts);
    m.recording = true;
    if (getenv("GSRUN_TRACE")) m.trace = atoll(getenv("GSRUN_TRACE"));
    uint64_t start = m.c.cycles;
    m.call_native(VM_BANK, uint16_t(VM_DP), max_insns);
    run_cycles = m.c.cycles - start;
    if (m.c.halted()) {
      uint8_t e = dp[DP_ERR];
      m.why = e == 0 ? "END" : e == 0xFE ? "not implemented yet" : "error";
    }
  }

  fprintf(stderr, "gsrun: %s; %zu output bytes; %llu cycles in the program\n", m.why, m.out.size(),
          (unsigned long long)run_cycles);
  if (out_path) {
    FILE* f = fopen(out_path, "wb");
    fwrite(m.out.data(), 1, m.out.size(), f);
    fclose(f);
  } else
    show(m.out);
  return 0;
}
