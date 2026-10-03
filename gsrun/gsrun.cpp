// gsrun: run an AppleSoft program the way a IIgs would, and record its output.
//
// The machine is emu816 (the ares 65816, SingleStepTests-clean) with the IIgs
// ROM 3 in banks $FC-$FF and bank $FF's $C100-$FFFF visible in bank 0 (the
// language card reading ROM, the internal $Cx firmware).  The ROM's AppleSoft
// is cold-started at $E000 in emulation mode, as the IIgs does.
//
// The monitor's I/O entries are taken over - the GS ROM's monitor reaches into
// firmware vectors in banks $E0/$E1 that only a real boot sets up, and the
// point is AppleSoft's behaviour, not the video hardware's - so output can be
// compared byte for byte:
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
//   gsrun ROM --prog TOKENS [--keys SCRIPT] [--out FILE] [--max N]
//
// The program (tokenized, as at $801) is installed at the first ] prompt,
// then RUN is typed.  The output is every COUT byte from RUN until AppleSoft
// reaches RESTART (END, STOP, an error and its message) - the same window
// prof/asprof records.  Key scripts: literal characters, {CR} {ESC} {SP}.
#include <algorithm>
#include <cstdio>
#include <map>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cpu816.hpp"

namespace {

constexpr uint16_t COUT = 0xFDED, RDKEY = 0xFD0C, GETLN = 0xFD6A, MONWAIT = 0xFCA8, HOME = 0xFC58,
                   CLREOP = 0xFC42, CLREOL = 0xFC9C, VTAB = 0xFC22, BASIC_COLD = 0xE000, RESTART = 0xD43C;
constexpr int CH = 0x24, CV = 0x25, WNDLFT = 0x20, WNDWDTH = 0x21, WNDTOP = 0x22, WNDBTM = 0x23;

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

} // namespace

int main(int argc, char** argv)
{
  const char *rom_path = nullptr, *prog_path = nullptr, *keys_path = nullptr, *out_path = nullptr;
  long long max_insns = 2000000000;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--prog" && i + 1 < argc) prog_path = argv[++i];
    else if (a == "--keys" && i + 1 < argc) keys_path = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (a == "--max" && i + 1 < argc) max_insns = atoll(argv[++i]);
    else if (!rom_path) rom_path = argv[i];
    else { fprintf(stderr, "usage: gsrun ROM --prog TOKENS [--keys SCRIPT] [--out FILE]\n"); return 2; }
  }
  if (!rom_path || !prog_path) { fprintf(stderr, "usage: gsrun ROM --prog TOKENS [--keys SCRIPT] [--out FILE]\n"); return 2; }
  auto rom = slurp(rom_path);
  if (rom.size() != 0x40000) { fprintf(stderr, "%s: want the 256K ROM 3\n", rom_path); return 2; }
  auto prog = slurp(prog_path);
  std::vector<uint8_t> keys = parse_keys("RUN{CR}");
  if (keys_path) {
    auto k = slurp(keys_path);
    auto more = parse_keys(std::string(k.begin(), k.end()));
    keys.insert(keys.end(), more.begin(), more.end());
  }

  Cpu816 c;
  memcpy(&c.mem[0xFC0000], rom.data(), rom.size());
  c.on_read = [&](uint32_t a, uint8_t& v) {
    if (a >= 0xC100 && a <= 0xFFFF) { v = c.mem[0xFF0000 | a]; return true; } // ROM / firmware
    if (a >= 0xC000 && a <= 0xC0FF) { v = 0; return true; }                    // I/O: no key, 40 col
    return false;
  };
  c.on_write = [](uint32_t a, uint8_t) { return a >= 0xC000 && a <= 0xFFFF; };

  c.reset_regs();
  c.set_regs(0, 0, 0, 0x01FF, 0, 0, 0x34, true);
  c.set_pc(BASIC_COLD);
  // the monitor's text window, as INIT leaves it
  c.mem[WNDLFT] = 0, c.mem[WNDWDTH] = 40, c.mem[WNDTOP] = 0, c.mem[WNDBTM] = 24;

  size_t kpos = 0;
  bool installed = false, running = false, done = false;
  std::vector<uint8_t> out;
  const char* why = "instruction limit";
  uint32_t last_monitor = 0;
  auto record = [&](uint8_t b) { if (running && !done) out.push_back(b); };
  auto cout = [&](uint8_t ch) {
    record(ch);
    if ((ch & 0x7F) == 0x0D) c.mem[CH] = c.mem[WNDLFT], c.mem[CV]++;
    else if (++c.mem[CH] >= c.mem[WNDWDTH]) c.mem[CH] = c.mem[WNDLFT], c.mem[CV]++;
    if (c.mem[CV] >= c.mem[WNDBTM]) c.mem[CV] = c.mem[WNDBTM] - 1; // scrolled
  };
  auto set_a = [&](uint16_t a) { c.set_regs(a, c.x(), c.y(), c.s(), c.d(), c.dbr(), c.p(), c.e()); };
  auto set_x = [&](uint16_t x) { c.set_regs(c.a(), x, c.y(), c.s(), c.d(), c.dbr(), c.p(), c.e()); };
  auto install = [&] { // the first ] prompt: put the program in, then RUN is typed
    if (installed) return;
    installed = true;
    memcpy(&c.mem[0x801], prog.data(), prog.size());
    c.mem[0x800] = 0;
    uint16_t end = uint16_t(0x801 + prog.size());
    c.mem[0x67] = 0x01, c.mem[0x68] = 0x08;
    for (int z = 0x69; z <= 0x6D; z += 2) c.mem[z] = uint8_t(end), c.mem[z + 1] = uint8_t(end >> 8);
    c.mem[0xAF] = uint8_t(end), c.mem[0xB0] = uint8_t(end >> 8);
  };
  auto next_key = [&](uint8_t& k) {
    if (kpos >= keys.size()) return false;
    k = keys[kpos++] | 0x80;
    return true;
  };
  for (long long n = 0; n < max_insns; n++) {
    uint32_t pc = c.pc();
    if (pc >= 0xF800 && pc <= 0xFFFF) last_monitor = pc;
    if ((pc >> 16) == 0xE0 || (pc >> 16) == 0xE1) {
      fprintf(stderr, "gsrun: into firmware at %06X after monitor %04X\n", pc, last_monitor);
      why = "unmodelled firmware call";
      break;
    }
    if (pc == COUT) { cout(uint8_t(c.a())); c.sim_rts(); continue; }
    if (pc == MONWAIT) { c.sim_rts(); continue; }
    if (pc == HOME || pc == CLREOP || pc == CLREOL) {
      record(0x01), record(pc == HOME ? 'H' : pc == CLREOP ? 'P' : 'L');
      if (pc == HOME) c.mem[CH] = c.mem[WNDLFT], c.mem[CV] = c.mem[WNDTOP];
      c.sim_rts();
      continue;
    }
    if (pc == VTAB) { record(0x01), record('V'), record(c.mem[CV]); c.sim_rts(); continue; }
    if (pc == GETLN) {
      install();
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
      if (!got) { why = done ? "back at the prompt" : "waiting for input"; break; }
      if (kpos == 4) running = true; // the RUN line is in: the program's output starts
      set_x(uint16_t(len));
      c.sim_rts();
      continue;
    }
    if (pc == RDKEY) {
      uint8_t k;
      if (!next_key(k)) { why = done ? "back at the prompt" : "waiting for a key"; break; }
      set_a(k);
      c.sim_rts();
      continue;
    }
    if (pc == RESTART && running) done = true;
    if (getenv("GSRUN_LAST")) {
      static uint32_t ring[64]; static int ri = 0;
      ring[ri++ & 63] = pc;
      if (pc == uint32_t(strtoul(getenv("GSRUN_LAST"), nullptr, 16))) {
        for (int i = 0; i < 64; i++) fprintf(stderr, "%06X ", ring[(ri + i) & 63]);
        fprintf(stderr, "\n");
        return 3;
      }
    }
    if (getenv("GSRUN_TRACE") && n < atoll(getenv("GSRUN_TRACE"))) {
      static std::map<uint32_t, long> hist;
      hist[pc]++;
      if (n + 1 == atoll(getenv("GSRUN_TRACE"))) {
        std::vector<std::pair<long, uint32_t>> v;
        for (auto& [k, cnt] : hist) v.push_back({cnt, k});
        std::sort(v.rbegin(), v.rend());
        for (int i = 0; i < 25 && i < int(v.size()); i++) fprintf(stderr, "%06X %ld\n", v[i].second, v[i].first);
      }
    }
    c.step();
    if (c.halted()) { why = "STP/WAI"; break; }
  }
  fprintf(stderr, "gsrun: %s; %zu output bytes; %llu cycles\n", why, out.size(), (unsigned long long)c.cycles);
  if (out_path) {
    FILE* f = fopen(out_path, "wb");
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
  } else {
    for (uint8_t b : out) putchar((b & 0x7F) == 0x0D ? '\n' : (b & 0x7F) < 0x20 ? '^' : b & 0x7F);
    putchar('\n');
  }
  return 0;
}
