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
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "cpu816.hpp"

namespace {

// zero-page / direct-page offsets (AppleSoft's)
constexpr int FAC = 0x9D, FACSIGN = 0xA2, SHIFTEXT = 0xA4, ARG = 0xA5, ARGSIGN = 0xAA, SGNCPR = 0xAB,
              FAC_EXT = 0xAC, ARG_EXT = 0x92, FP_PTR = 0x5E, FP_ERR = 0xF2;

// ROM entries (S-C DocuMentor labels)
constexpr uint16_t R_LOAD_FAC = 0xEAF9, R_STORE_FAC = 0xEB2B, R_FADD = 0xE7BE, R_FSUB = 0xE7A7,
                   R_ERROR = 0xD412;
// fp816 jump table
constexpr uint32_t LIB = 0xF20000;
constexpr uint32_t F_LOAD_FAC = LIB + 0x00, F_STORE_FAC = LIB + 0x08, F_ADD = LIB + 0x0C,
                   F_SUB = LIB + 0x10;

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
  int err; // 0, or the AppleSoft error code
  bool operator==(const Outcome& o) const
  {
    return fac == o.fac && !memcmp(stored, o.stored, 5) && err == o.err;
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
      cpu.step();
      if (cpu.halted()) return true;
    }
    return false;
  }

  // ROM: JSR routine with A/Y = operand address, then STP
  Outcome rom_call(uint16_t routine, const Fac& f, const uint8_t op[5], bool store)
  {
    memset(&cpu.mem[0], 0, 0x100);
    put_fac(0, f);
    memcpy(&cpu.mem[OPERAND], op, 5);
    uint8_t stub[] = {0x20, uint8_t(routine), uint8_t(routine >> 8), 0xDB};
    memcpy(&cpu.mem[STUB], stub, sizeof stub);
    cpu.reset_regs();
    // STORE takes X/Y (low/high); the loads and FADD/FSUB take A/Y
    cpu.set_regs(store ? 0 : OPERAND & 0xFF, store ? OPERAND & 0xFF : 0, OPERAND >> 8, 0x01FF, 0, 0, 0x34,
                 true);
    Outcome o{};
    if (!run(STUB, true, o.err)) o.err = -1;
    o.fac = get_fac(0);
    memcpy(o.stored, &cpu.mem[OPERAND], 5);
    return o;
  }

  // fp816: JSL entry with FP_PTR = operand, D = DP_OURS, then STP
  Outcome lib_call(uint32_t entry, const Fac& f, const uint8_t op[5])
  {
    memset(&cpu.mem[DP_OURS], 0, 0x100);
    put_fac(DP_OURS, f);
    memcpy(&cpu.mem[OPERAND], op, 5);
    cpu.mem[DP_OURS + FP_PTR] = uint8_t(OPERAND);
    cpu.mem[DP_OURS + FP_PTR + 1] = uint8_t(OPERAND >> 8);
    cpu.mem[DP_OURS + FP_PTR + 2] = uint8_t(OPERAND >> 16);
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
  printf("     stored %02X %02X %02X %02X %02X  err=%d\n", o.stored[0], o.stored[1], o.stored[2], o.stored[3],
         o.stored[4], o.err);
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
  };
  std::vector<Test> tests = {
      {"load FAC", [&](bool o, const Fac& f, const uint8_t* p) {
         return o ? m.lib_call(F_LOAD_FAC, f, p) : m.rom_call(R_LOAD_FAC, f, p, false); }},
      {"store FAC rounded", [&](bool o, const Fac& f, const uint8_t* p) {
         return o ? m.lib_call(F_STORE_FAC, f, p) : m.rom_call(R_STORE_FAC, f, p, true); }},
      {"FADD mem+FAC", [&](bool o, const Fac& f, const uint8_t* p) {
         return o ? m.lib_call(F_ADD, f, p) : m.rom_call(R_FADD, f, p, false); }},
      {"FSUB mem-FAC", [&](bool o, const Fac& f, const uint8_t* p) {
         return o ? m.lib_call(F_SUB, f, p) : m.rom_call(R_FSUB, f, p, false); }},
  };

  int failed_tests = 0;
  for (auto& t : tests) {
    long bad = 0, errs = 0;
    for (long i = 0; i < cases; i++) {
      int centre = int(rndn(256));
      Fac f = random_fac(centre);
      uint8_t op[5];
      random_packed(op, f.exp ? f.exp : centre);
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
