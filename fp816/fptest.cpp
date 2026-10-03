// fptest: fp816 against the IIgs ROM's own AppleSoft float routines.
//
// Both run on emu816 (the ares 65816, SingleStepTests-clean): the ROM in
// emulation mode with bank $FF's $D000-$FFFF visible in bank 0 and FAC on
// zero page; fp816 in native mode from its bank with FAC on a direct page at
// $1000.  Each case gives both the same state and operand, then compares
// the whole FAC (exponent, mantissa, raw sign byte, rounding byte), the
// stored bytes, and the error outcome.
//
//   fptest ROM fp.bin [cases] [seed]     ROM = the 256K IIgs ROM 3 image
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "cpu816.hpp"

namespace {

// zero-page / direct-page offsets (AppleSoft's)
constexpr int FAC = 0x9D, FACSIGN = 0xA2, SHIFTEXT = 0xA4, ARG = 0xA5, ARGSIGN = 0xAA, SGNCPR = 0xAB,
              FAC_EXT = 0xAC, ARG_EXT = 0x92, FP_PTR = 0x5E, FP_ERR = 0xF2, FP_RES = 0xF3;

// ROM entries (S-C DocuMentor labels)
constexpr uint16_t R_LOAD_FAC = 0xEAF9, R_STORE_FAC = 0xEB2B, R_FADD = 0xE7BE, R_FSUB = 0xE7A7,
                   R_FMULT = 0xE97F, R_FDIV = 0xEA66, R_FCOMP = 0xEBB2, R_MUL10 = 0xEA39,
                   R_DIV10 = 0xEA55, R_SGN = 0xEB90, R_ABS = 0xEBAF, R_INT = 0xEC23, R_QINT = 0xEBF2,
                   R_AYINT = 0xE10C, R_GIVAYF = 0xE2F2, R_CONINT = 0xE6FB, R_GETADR = 0xE752,
                   R_ERROR = 0xD412, R_CHRGOT = 0x00B7;
// fp816 jump table
constexpr uint32_t LIB = 0xF20000;
constexpr uint32_t F_LOAD_FAC = LIB + 0x00, F_STORE_FAC = LIB + 0x08, F_ADD = LIB + 0x0C,
                   F_SUB = LIB + 0x10, F_MUL = LIB + 0x1C, F_DIV = LIB + 0x20, F_CMP = LIB + 0x24,
                   F_MUL10 = LIB + 0x28, F_DIV10 = LIB + 0x2C, F_SGN = LIB + 0x30, F_ABS = LIB + 0x34,
                   F_INT = LIB + 0x38, F_QINT = LIB + 0x3C, F_AYINT = LIB + 0x40, F_GIVAYF = LIB + 0x44,
                   F_CONINT = LIB + 0x48, F_GETADR = LIB + 0x4C;
constexpr int LINNUM = 0x50, FP_INT = 0xF8;

// how a ROM routine takes its operand
enum Conv { AY, XY, NONE, INT16 }; // INT16: A = high, Y = low (operand bytes 3, 4)
// where the byte/word result is
enum Res { R_NONE, R_A, R_X, R_LINNUM };

constexpr uint32_t OPERAND = 0x000300; // 5 packed bytes
constexpr uint32_t STUB = 0x000310;
constexpr uint32_t DP_OURS = 0x1000;

struct Fac {
  uint8_t exp, m[4], sign, ext;
  bool operator==(const Fac& o) const
  {
    return exp == o.exp && !memcmp(m, o.m, 4) && sign == o.sign && ext == o.ext;
  }
};

struct Outcome {
  Fac fac;
  uint8_t stored[5];
  int err;     // 0, or the AppleSoft error code
  int res = 0; // the byte result (FCOMP's A), when the test has one
  bool operator==(const Outcome& o) const
  {
    return fac == o.fac && !memcmp(stored, o.stored, 5) && err == o.err && res == o.res;
  }
};

uint64_t rng_state = 0x9E3779B97F4A7C15ull;
uint64_t rnd()
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}
unsigned rndn(unsigned n) { return unsigned(rnd() % n); }

// an exponent near `near` (or anywhere): equal, close, far, and the edges
uint8_t pick_exp(int near)
{
  switch (rndn(10)) {
  case 0: return 0;
  case 1: return uint8_t(rndn(3) == 0 ? 0xFF : rndn(3) == 0 ? 1 : 0xFE);
  case 2: case 3: return uint8_t(near);
  case 4: case 5: case 6: return uint8_t(std::clamp(near + int(rndn(17)) - 8, 1, 255));
  case 7: return uint8_t(std::clamp(near + int(rndn(81)) - 40, 1, 255));
  default: return uint8_t(rnd());
  }
}

void pick_mantissa(uint8_t m[4])
{
  switch (rndn(8)) {
  case 0: memset(m, 0, 4); break;
  case 1: memset(m, 0xFF, 4); break;
  case 2: for (int i = 0; i < 4; i++) m[i] = uint8_t(rndn(2) ? 0 : 0xFF); break;
  case 3: for (int i = 0; i < 4; i++) m[i] = uint8_t(1u << rndn(8)); break;
  default: for (int i = 0; i < 4; i++) m[i] = uint8_t(rnd());
  }
}

Fac random_fac(int near)
{
  Fac f;
  f.exp = pick_exp(near);
  pick_mantissa(f.m);
  f.m[0] |= 0x80; // FAC is kept normalized
  static const uint8_t signs[] = {0x00, 0xFF, 0x80, 0x7F};
  f.sign = rndn(3) ? signs[rndn(4)] : uint8_t(rnd());
  f.ext = rndn(3) ? uint8_t(rnd()) : 0;
  return f;
}

void random_packed(uint8_t p[5], int near)
{
  p[0] = pick_exp(near);
  pick_mantissa(p + 1);
}

// an operand at (or a hair from) FAC's own value: equal compares, rounding-
// byte ties, catastrophic cancellation in subtraction
void packed_near(uint8_t p[5], Fac& f)
{
  p[0] = f.exp;
  memcpy(p + 1, f.m, 4);
  p[1] = uint8_t((p[1] & 0x7F) | (rndn(2) ? (f.sign & 0x80) : (~f.sign & 0x80)));
  switch (rndn(4)) {
  case 0: break;
  case 1: p[4] = uint8_t(p[4] + 1); break;
  case 2: p[4] = uint8_t(p[4] - 1); break;
  default: p[3 + rndn(2)] ^= uint8_t(1u << rndn(8));
  }
  static const uint8_t exts[] = {0x7E, 0x7F, 0x80, 0x81, 0x00, 0xFF};
  f.ext = exts[rndn(6)];
}

struct Machine {
  Cpu816 cpu;
  std::vector<uint8_t> rom; // 256K
  explicit Machine(const std::vector<uint8_t>& r) : rom(r)
  {
    memcpy(&cpu.mem[0xFC0000], rom.data(), rom.size()); // banks $FC-$FF
    cpu.on_read = [this](uint32_t a, uint8_t& v) {
      if (a >= 0xD000 && a <= 0xFFFF) { // bank 0 shows bank $FF's ROM
        v = cpu.mem[0xFF0000 | a];
        return true;
      }
      return false;
    };
    cpu.on_write = [](uint32_t a, uint8_t) { return a >= 0xD000 && a <= 0xFFFF; };
  }

  void put_fac(uint32_t dp, const Fac& f)
  {
    cpu.mem[dp + FAC] = f.exp;
    memcpy(&cpu.mem[dp + FAC + 1], f.m, 4);
    cpu.mem[dp + FACSIGN] = f.sign;
    cpu.mem[dp + FAC_EXT] = f.ext;
    cpu.mem[dp + SHIFTEXT] = 0;
  }
  Fac get_fac(uint32_t dp)
  {
    Fac f;
    f.exp = cpu.mem[dp + FAC];
    memcpy(f.m, &cpu.mem[dp + FAC + 1], 4);
    f.sign = cpu.mem[dp + FACSIGN];
    f.ext = cpu.mem[dp + FAC_EXT];
    return f;
  }

  // run until STP; returns false on a runaway
  bool run(uint32_t pc, bool rom_mode, int& err)
  {
    cpu.set_pc(pc);
    err = 0;
    for (int n = 0; n < 200000; n++) {
      if (rom_mode && cpu.pc() == R_ERROR) {
        err = cpu.x() & 0xFF;
        return true;
      }
      if (rom_mode && cpu.pc() == R_CHRGOT) return true; // CONINT's tail: JMP CHRGOT
      cpu.step();
      if (cpu.halted()) return true;
    }
    return false;
  }

  // ROM: JSR routine with the operand address in A/Y or X/Y, then STP
  Outcome rom_call(uint16_t routine, const Fac& f, const uint8_t op[5], Conv conv, Res res = R_NONE)
  {
    bool store = conv == XY;
    memset(&cpu.mem[0], 0, 0x100);
    put_fac(0, f);
    memcpy(&cpu.mem[OPERAND], op, 5);
    uint8_t stub[] = {0x20, uint8_t(routine), uint8_t(routine >> 8), 0xDB};
    memcpy(&cpu.mem[STUB], stub, sizeof stub);
    cpu.reset_regs();
    // STORE takes X/Y (low/high); the loads and FADD/FSUB take A/Y
    if (conv == INT16) cpu.set_regs(op[4], 0, op[3], 0x01FF, 0, 0, 0x34, true);
    else
      cpu.set_regs(store ? 0 : OPERAND & 0xFF, store ? OPERAND & 0xFF : 0, OPERAND >> 8, 0x01FF, 0, 0, 0x34,
                   true);
    Outcome o{};
    if (!run(STUB, true, o.err)) o.err = -1;
    o.fac = get_fac(0);
    memcpy(o.stored, &cpu.mem[OPERAND], 5);
    if (!o.err) {
      if (res == R_A) o.res = cpu.a() & 0xFF;
      if (res == R_X) o.res = cpu.x() & 0xFF;
      if (res == R_LINNUM) o.res = cpu.mem[LINNUM] | cpu.mem[LINNUM + 1] << 8;
    }
    return o;
  }

  // fp816: JSL entry with FP_PTR = operand, D = DP_OURS, then STP
  Outcome lib_call(uint32_t entry, const Fac& f, const uint8_t op[5], Conv conv, Res res = R_NONE)
  {
    memset(&cpu.mem[DP_OURS], 0, 0x100);
    put_fac(DP_OURS, f);
    memcpy(&cpu.mem[OPERAND], op, 5);
    cpu.mem[DP_OURS + FP_PTR] = uint8_t(OPERAND);
    cpu.mem[DP_OURS + FP_PTR + 1] = uint8_t(OPERAND >> 8);
    cpu.mem[DP_OURS + FP_PTR + 2] = uint8_t(OPERAND >> 16);
    if (conv == INT16) cpu.mem[DP_OURS + FP_INT] = op[3], cpu.mem[DP_OURS + FP_INT + 1] = op[4];
    uint8_t stub[] = {0x22, uint8_t(entry), uint8_t(entry >> 8), uint8_t(entry >> 16), 0xDB};
    memcpy(&cpu.mem[STUB], stub, sizeof stub);
    cpu.reset_regs();
    uint8_t p = uint8_t(rndn(4) << 4); // any M/X on entry: the library preserves them
    cpu.set_regs(0, 0, 0, 0x01FF, DP_OURS, 0, p, false);
    Outcome o{};
    if (!run(STUB, false, o.err)) o.err = -1;
    else o.err = cpu.mem[DP_OURS + FP_ERR];
    if (cpu.p() != p && o.err >= 0) o.err = -2; // M/X (or anything) not restored
    o.fac = get_fac(DP_OURS);
    memcpy(o.stored, &cpu.mem[OPERAND], 5);
    if (!o.err) {
      if (res == R_A || res == R_X) o.res = cpu.mem[DP_OURS + FP_RES];
      if (res == R_LINNUM) o.res = cpu.mem[DP_OURS + LINNUM] | cpu.mem[DP_OURS + LINNUM + 1] << 8;
    }
    return o;
  }
};

void show(const char* tag, const Fac& f)
{
  printf("  %s exp=%02X m=%02X%02X%02X%02X sign=%02X ext=%02X\n", tag, f.exp, f.m[0], f.m[1], f.m[2], f.m[3],
         f.sign, f.ext);
}

void show(const char* tag, const Outcome& o)
{
  show(tag, o.fac);
  printf("     stored %02X %02X %02X %02X %02X  err=%d res=%02X\n", o.stored[0], o.stored[1], o.stored[2],
         o.stored[3], o.stored[4], o.err, o.res);
}

} // namespace

int main(int argc, char** argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: fptest ROM fp.bin [cases] [seed]\n");
    return 2;
  }
  auto slurp = [](const char* p) {
    std::vector<uint8_t> b;
    FILE* f = fopen(p, "rb");
    if (!f) { perror(p); exit(2); }
    int c;
    while ((c = fgetc(f)) != EOF) b.push_back(uint8_t(c));
    fclose(f);
    return b;
  };
  auto rom = slurp(argv[1]);
  auto lib = slurp(argv[2]);
  if (rom.size() != 0x40000) { fprintf(stderr, "%s: want the 256K ROM 3 image\n", argv[1]); return 2; }
  long cases = argc > 3 ? atol(argv[3]) : 200000;
  if (argc > 4) rng_state = strtoull(argv[4], nullptr, 0) | 1;

  Machine m(rom);
  memcpy(&m.cpu.mem[LIB], lib.data(), lib.size());

  struct Test {
    const char* name;
    std::function<Outcome(bool ours, const Fac&, const uint8_t*)> call;
    int lo = 0, hi = 255; // the exponent range the cases centre on
  };
  auto both = [&](uint32_t ours, uint16_t rom, Conv conv, Res res = R_NONE) {
    return [&m, ours, rom, conv, res](bool o, const Fac& f, const uint8_t* p) {
      return o ? m.lib_call(ours, f, p, conv, res) : m.rom_call(rom, f, p, conv, res);
    };
  };
  std::vector<Test> tests = {
      {"load FAC", both(F_LOAD_FAC, R_LOAD_FAC, AY)},
      {"store FAC rounded", both(F_STORE_FAC, R_STORE_FAC, XY)},
      {"FADD mem+FAC", both(F_ADD, R_FADD, AY)},
      {"FSUB mem-FAC", both(F_SUB, R_FSUB, AY)},
      {"FMULT mem*FAC", both(F_MUL, R_FMULT, AY)},
      {"FDIV mem/FAC", both(F_DIV, R_FDIV, AY)},
      {"FCOMP mem:FAC", both(F_CMP, R_FCOMP, AY, R_A)},
      {"MUL10", both(F_MUL10, R_MUL10, NONE)},
      {"DIV10", both(F_DIV10, R_DIV10, NONE)},
      {"SGN", both(F_SGN, R_SGN, NONE)},
      {"ABS", both(F_ABS, R_ABS, NONE)},
      {"INT", both(F_INT, R_INT, NONE), 0x70, 0xA8},
      {"QINT", both(F_QINT, R_QINT, NONE), 0x70, 0xA0},
      {"AYINT", both(F_AYINT, R_AYINT, NONE), 0x70, 0x92},
      {"GIVAYF", both(F_GIVAYF, R_GIVAYF, INT16)},
      {"CONINT", both(F_CONINT, R_CONINT, NONE, R_X), 0x70, 0x8A},
      {"GETADR", both(F_GETADR, R_GETADR, NONE, R_LINNUM), 0x70, 0x92},
  };

  // optional filter: FPTEST_ONLY=substring
  if (const char* only = getenv("FPTEST_ONLY")) {
    std::vector<Test> keep;
    for (auto& t : tests)
      if (strstr(t.name, only)) keep.push_back(t);
    tests = keep;
  }

  int failed_tests = 0;
  for (auto& t : tests) {
    long bad = 0, errs = 0;
    for (long i = 0; i < cases; i++) {
      int centre = t.lo + int(rndn(unsigned(t.hi - t.lo + 1)));
      Fac f = random_fac(centre);
      uint8_t op[5];
      if (rndn(4) == 0 && f.exp) packed_near(op, f);
      else random_packed(op, f.exp ? f.exp : centre);
      Outcome want = t.call(false, f, op);
      Outcome got = t.call(true, f, op);
      errs += want.err != 0;
      if (!(want == got)) {
        if (++bad <= 4) {
          printf("%s: case %ld differs\n", t.name, i);
          show("in ", f);
          printf("     operand %02X %02X %02X %02X %02X\n", op[0], op[1], op[2], op[3], op[4]);
          show("ROM", want);
          show("fp ", got);
        }
      }
    }
    printf("%-20s %8ld cases  %6ld ROM errors  %ld differ\n", t.name, cases, errs, bad);
    failed_tests += bad != 0;
  }
  return failed_tests ? 1 : 0;
}
