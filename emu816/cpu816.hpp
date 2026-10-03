// A standalone 65816: the ares WDC65816 core over a flat 16 MB memory.
//
// Cycle-counted: every bus cycle the core asks for (read, write, idle) is one
// cycle.  The memory is plain RAM unless a caller hooks reads/writes.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "nall_shim.hpp"
#include "wdc65816.hpp" // from ~/repos/ares/ares/component/processor/wdc65816

struct Cpu816 : ares::WDC65816 {
  std::vector<uint8_t> mem = std::vector<uint8_t>(1 << 24);
  uint64_t cycles = 0;

  // optional hooks: return true if handled
  std::function<bool(uint32_t addr, uint8_t& value)> on_read;
  std::function<bool(uint32_t addr, uint8_t value)> on_write;

  void reset_regs(); // ares power(): E=1, P=$34, S=$01FF, everything else 0
  void step() { instruction(); }

  // registers as plain numbers
  uint32_t pc() const { return r.pc.d; }
  void set_pc(uint32_t v) { r.pc.d = v & 0xFFFFFF; }
  uint16_t a() const { return r.a.w; }
  uint16_t x() const { return r.x.w; }
  uint16_t y() const { return r.y.w; }
  uint16_t s() const { return r.s.w; }
  uint16_t d() const { return r.d.w; }
  uint8_t dbr() const { return r.b; }
  uint8_t p() const { return uint8_t(uint32_t(r.p)); }
  bool e() const { return r.e; }
  void set_regs(uint16_t a, uint16_t x, uint16_t y, uint16_t s, uint16_t d, uint8_t dbr, uint8_t p,
                bool e);
  bool halted() const { return r.stp || r.wai; }

  // ares::WDC65816
  void idle() override { cycles++; }
  ares::n8 read(ares::n24 address) override;
  void write(ares::n24 address, ares::n8 data) override;
  void lastCycle() override {}
  bool interruptPending() const override { return false; }
  // ares spins in WAI/STP until synchronizing(); with no interrupts here that
  // never ends, so they return at once (r.wai / r.stp stay set: see halted())
  bool synchronizing() const override { return true; }
};
