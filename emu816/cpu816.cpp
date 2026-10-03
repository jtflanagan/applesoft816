// The ares WDC65816 core, compiled from its own sources (unmodified), minus
// the disassembler and save states.
#include "cpu816.hpp"

namespace ares {
#include "registers.hpp"
#include "memory.cpp"
#include "algorithms.cpp"
#include "instructions-read.cpp"
#include "instructions-write.cpp"
#include "instructions-modify.cpp"
#include "instructions-pc.cpp"
#include "instructions-other.cpp"
#include "instruction.cpp"

// ares's own power(), from wdc65816.cpp (which also pulls in ares.hpp)
auto WDC65816::power() -> void {
  r.pc.d = 0x000000;
  r.a = 0x0000;
  r.x = 0x0000;
  r.y = 0x0000;
  r.s = 0x01ff;
  r.d = 0x0000;
  r.b = 0x00;
  r.p = 0x34;
  r.e = 1;

  r.irq = 0;
  r.wai = 0;
  r.stp = 0;
  r.mar = 0x000000;
  r.mdr = 0x00;

  r.vector = 0xfffc;
}
#include "registers.hpp"
} // namespace ares

void Cpu816::reset_regs() { power(); }

void Cpu816::set_regs(uint16_t a, uint16_t x, uint16_t y, uint16_t s, uint16_t d, uint8_t dbr, uint8_t p,
                      bool e)
{
  r.a = a;
  r.x = x;
  r.y = y;
  r.s = s;
  r.d = d;
  r.b = dbr;
  r.p = ares::n8(p);
  r.e = e;
  if (e) { // what the chip holds in emulation mode, whatever was loaded
    r.s.h = 0x01;
    r.x.h = 0x00;
    r.y.h = 0x00;
    r.p.m = 1;
    r.p.x = 1;
  } else if (r.p.x) {
    r.x.h = 0x00;
    r.y.h = 0x00;
  }
}

ares::n8 Cpu816::read(ares::n24 address)
{
  cycles++;
  uint8_t v;
  if (on_read && on_read(address, v)) return v;
  return mem[address];
}

void Cpu816::write(ares::n24 address, ares::n8 data)
{
  cycles++;
  if (on_write && on_write(address, uint8_t(data))) return;
  mem[address] = uint8_t(data);
}
