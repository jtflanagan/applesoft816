// The handful of ares/nall types the ares WDC65816 core uses, so the core's
// own sources (~/repos/ares, ISC licence) compile unmodified outside ares.
//   n8 n16 n24 n32  - wrapping unsigned integers (nall::Natural<N>)
//   BitRange<P,L,H> - a live view of bits L..H of a Natural<P> (register halves)
//   u32 s32 i8 i16  - plain integers
// Only what the core touches is here; the disassembler and save states are
// left out of the build, so their types are empty stand-ins.
#pragma once
#include <cstdint>
#include <type_traits>
#include <utility>

#define NALL_NOINLINE __attribute__((noinline))

namespace ares {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s32 = int32_t;
using i8 = int8_t;
using i16 = int16_t;

template <int Bits> struct Natural {
  static_assert(Bits > 0 && Bits <= 32);
  static constexpr u64 Mask = (u64(1) << Bits) - 1;
  u32 v = 0;

  Natural() = default;
  Natural(const Natural&) = default;
  // any integer, or anything that converts to one (a BitRange, another width)
  template <class T, class = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Natural>>>
  Natural(const T& x) : v(u32(u64(x) & Mask)) {}

  Natural& operator=(const Natural&) = default;
  template <class T> Natural& operator=(const T& x) { v = u32(u64(x) & Mask); return *this; }

  operator u32() const { return v; }

  bool bit(int i) const { return (v >> i) & 1; }

  Natural& operator++() { v = u32((u64(v) + 1) & Mask); return *this; }
  Natural& operator--() { v = u32((u64(v) - 1) & Mask); return *this; }
  Natural operator++(int) { Natural t = *this; ++*this; return t; }
  Natural operator--(int) { Natural t = *this; --*this; return t; }

  template <class T> Natural& operator+=(const T& x) { return *this = u64(v) + u64(x); }
  template <class T> Natural& operator-=(const T& x) { return *this = u64(v) - u64(x); }
  template <class T> Natural& operator|=(const T& x) { return *this = u64(v) | u64(x); }
  template <class T> Natural& operator&=(const T& x) { return *this = u64(v) & u64(x); }
  template <class T> Natural& operator^=(const T& x) { return *this = u64(v) ^ u64(x); }
  template <class T> Natural& operator<<=(const T& x) { return *this = u64(v) << u64(x); }
  template <class T> Natural& operator>>=(const T& x) { return *this = u64(v) >> u64(x); }
};

using n8 = Natural<8>;
using n16 = Natural<16>;
using n24 = Natural<24>;
using n32 = Natural<32>;

template <int Precision, int Lo, int Hi> struct BitRange {
  static constexpr u32 Mask = u32((u64(1) << (Hi - Lo + 1)) - 1);
  Natural<Precision>* t;

  BitRange(Natural<Precision>* target) : t(target) {}
  BitRange(const BitRange&) = delete; // a copy would alias the register

  u32 get() const { return (t->v >> Lo) & Mask; }
  void set(u64 x) { t->v = (t->v & ~(Mask << Lo)) | (u32(x & Mask) << Lo); }

  operator u32() const { return get(); }
  BitRange& operator=(const BitRange& o) { set(o.get()); return *this; }
  template <class T> BitRange& operator=(const T& x) { set(u64(x)); return *this; }

  BitRange& operator++() { set(u64(get()) + 1); return *this; }
  BitRange& operator--() { set(u64(get()) - 1); return *this; }
  u32 operator++(int) { u32 old = get(); ++*this; return old; }
  u32 operator--(int) { u32 old = get(); --*this; return old; }

  template <class T> BitRange& operator+=(const T& x) { set(u64(get()) + u64(x)); return *this; }
  template <class T> BitRange& operator-=(const T& x) { set(u64(get()) - u64(x)); return *this; }
  template <class T> BitRange& operator|=(const T& x) { set(u64(get()) | u64(x)); return *this; }
  template <class T> BitRange& operator&=(const T& x) { set(u64(get()) & u64(x)); return *this; }
  template <class T> BitRange& operator^=(const T& x) { set(u64(get()) ^ u64(x)); return *this; }
};

using std::swap; // XCE: swap(CF, EF)

// stand-ins for the parts of ares the build leaves out
struct serializer {};
struct string {};
template <class T> struct maybe {};

} // namespace ares
