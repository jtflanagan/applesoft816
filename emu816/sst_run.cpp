// Run packed SingleStepTests/65816 cases (sst_pack.py) through Cpu816:
// one instruction each; compare registers, memory and the cycle count.
//
//   sst_run TESTS.bin [-v]
//
// Known divergences of the ares core from the vectors (and so from our card's
// core, which passes them) are counted apart, and only other failures fail
// the run.  None of them is reachable from native-mode code:
//   - MVN/MVP ($44/$54): the vectors stop at 100 cycles, mid-move; ares works
//     a whole instruction at a time.  Not comparable.
//   - WAI/STP ($CB/$DB): cycles only; they return at once here (no interrupts).
//   - emulation mode, (dp,X) with DL != 0 and the pointer at $xxFF or past
//     $FFFF: ares wraps the pointer differently.
//   - emulation mode, JSR (abs,X) ($FC) with S at $0100/$0101: the push
//     leaves page 1 on the chip; ares keeps it there.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "cpu816.hpp"

struct Regs {
  uint16_t pc, s;
  uint8_t p;
  uint16_t a, x, y;
  uint8_t dbr;
  uint16_t d;
  uint8_t pbr, e;
};

struct Reader {
  std::vector<uint8_t> b;
  size_t i = 0;
  bool more() const { return i < b.size(); }
  uint8_t u8() { return b[i++]; }
  uint16_t u16() { uint16_t v = b[i] | b[i + 1] << 8; i += 2; return v; }
  uint32_t u32() { uint32_t v = b[i] | b[i + 1] << 8 | b[i + 2] << 16 | uint32_t(b[i + 3]) << 24; i += 4; return v; }
  Regs regs()
  {
    Regs r;
    r.pc = u16(), r.s = u16(), r.p = u8(), r.a = u16(), r.x = u16(), r.y = u16();
    r.dbr = u8(), r.d = u16(), r.pbr = u8(), r.e = u8();
    return r;
  }
  std::vector<std::pair<uint32_t, uint8_t>> mem()
  {
    std::vector<std::pair<uint32_t, uint8_t>> m(u16());
    for (auto& [a, v] : m) a = u32(), v = u8();
    return m;
  }
};

int main(int argc, char** argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: sst_run TESTS.bin [-v]\n");
    return 2;
  }
  bool verbose = argc > 2 && !strcmp(argv[2], "-v");
  Reader rd;
  {
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    rd.b.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(rd.b.data(), 1, rd.b.size(), f) != rd.b.size()) return 2;
    fclose(f);
  }

  Cpu816 cpu;
  struct Tally { long n = 0, regs = 0, mem = 0, cycles = 0, known = 0; };
  std::map<std::string, Tally> by_op; // "xx.e" / "xx.n"
  long total = 0, failed = 0, known = 0;

  while (rd.more()) {
    Regs ri = rd.regs();
    auto mi = rd.mem();
    Regs rf = rd.regs();
    auto mf = rd.mem();
    unsigned want_cycles = rd.u16();
    std::string name(reinterpret_cast<const char*>(&rd.b[rd.i + 1]), rd.b[rd.i]);
    rd.i += 1 + rd.b[rd.i];

    for (auto& [a, v] : mi) cpu.mem[a] = v;
    uint8_t op = cpu.mem[uint32_t(ri.pbr) << 16 | ri.pc];
    uint8_t operand = cpu.mem[uint32_t(ri.pbr) << 16 | uint16_t(ri.pc + 1)];
    cpu.reset_regs();
    cpu.set_regs(ri.a, ri.x, ri.y, ri.s, ri.d, ri.dbr, ri.p, ri.e);
    cpu.set_pc(uint32_t(ri.pbr) << 16 | ri.pc);
    cpu.cycles = 0;
    cpu.step();

    char key[8];
    snprintf(key, sizeof key, "%02x.%c", op, ri.e ? 'e' : 'n');
    Tally& t = by_op[key];
    t.n++, total++;
    bool regs_ok = cpu.pc() == (uint32_t(rf.pbr) << 16 | rf.pc) && cpu.s() == rf.s && cpu.p() == rf.p &&
                   cpu.a() == rf.a && cpu.x() == rf.x && cpu.y() == rf.y && cpu.dbr() == rf.dbr &&
                   cpu.d() == rf.d && cpu.e() == bool(rf.e);
    bool mem_ok = true;
    for (auto& [a, v] : mf) mem_ok &= cpu.mem[a] == v;
    bool cyc_ok = cpu.cycles == want_cycles;
    bool dpx = (op & 0x1F) == 0x01; // ORA AND EOR ADC STA LDA CMP SBC (dp,X)
    uint32_t ptr = uint32_t(ri.d) + operand + (ri.x & 0xFF);
    bool is_known = op == 0x44 || op == 0x54 || ((op == 0xCB || op == 0xDB) && regs_ok && mem_ok) ||
                    (ri.e && dpx && (ri.d & 0xFF) && ((ptr & 0xFF) == 0xFF || ptr > 0xFFFF)) ||
                    (ri.e && op == 0xFC && (ri.s & 0xFF) < 2 && regs_ok && cyc_ok);
    if ((!regs_ok || !mem_ok || !cyc_ok) && is_known) {
      t.known++, known++;
      goto next;
    }
    t.regs += !regs_ok, t.mem += !mem_ok, t.cycles += !cyc_ok;
    if (!regs_ok || !mem_ok || !cyc_ok) {
      failed++;
      if (verbose && (getenv("SST_ALL") || t.regs + t.mem + t.cycles <= 3)) {
        printf("FAIL %s:%s%s%s\n", name.c_str(), regs_ok ? "" : " regs", mem_ok ? "" : " mem",
               cyc_ok ? "" : " cycles");
        printf("  want pc=%02X%04X s=%04X p=%02X a=%04X x=%04X y=%04X b=%02X d=%04X e=%d cyc=%u\n", rf.pbr,
               rf.pc, rf.s, rf.p, rf.a, rf.x, rf.y, rf.dbr, rf.d, rf.e, want_cycles);
        printf("  got  pc=%06X s=%04X p=%02X a=%04X x=%04X y=%04X b=%02X d=%04X e=%d cyc=%llu\n", cpu.pc(),
               cpu.s(), cpu.p(), cpu.a(), cpu.x(), cpu.y(), cpu.dbr(), cpu.d(), cpu.e(),
               (unsigned long long)cpu.cycles);
      }
    }
  next:
    for (auto& [a, v] : mi) cpu.mem[a] = 0; // leave memory clean for the next test
    for (auto& [a, v] : mf) cpu.mem[a] = 0;
  }

  int bad_ops = 0;
  for (auto& [k, t] : by_op) {
    if (t.regs || t.mem || t.cycles) {
      bad_ops++;
      printf("%s  %5ld tests: regs %ld, mem %ld, cycles %ld\n", k.c_str(), t.n, t.regs, t.mem, t.cycles);
    }
    if (t.known && verbose) printf("%s  %5ld tests: %ld known divergences\n", k.c_str(), t.n, t.known);
  }
  printf("%ld tests, %ld failed, %ld known divergences; %zu opcode/mode files, %d with failures\n", total,
         failed, known, by_op.size(), bad_ops);
  return failed ? 1 : 0;
}
