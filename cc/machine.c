/* machine.c - the shell's reach into the machine (machine.h). */
#include <stdint.h>

/* the shell runs between programs: small before fast (the compiler, -Os) */
#pragma clang attribute push (__attribute__((minsize)), apply_to = function)

#include "machine.h"

/* ---- bank 0, the VM bank, the text bank ---- */
void to0(uint16_t a, const void *src, uint16_t n)
{
  if (!n) return;                         /* (the glue's count runs n down to 1) */
  xfer_far = (uint32_t)a - 1, xfer_near = (uint16_t)((uint16_t)src - 1), xfer_n = n;
  far_out();
}
void from0(void *dst, uint16_t a, uint16_t n)
{
  if (!n) return;
  xfer_far = (uint32_t)a - 1, xfer_near = (uint16_t)((uint16_t)dst - 1), xfer_n = n;
  far_in();
}
uint16_t vm_read16(uint16_t a)
{
  uint16_t v;
  xfer_far = (vm_base | a) - 1, xfer_near = (uint16_t)((uint16_t)&v - 1), xfer_n = 2;
  far_in();
  return v;
}
void to_text(uint16_t a, const void *src, uint16_t n)
{
  if (!n) return;
  xfer_far = (text_base | a) - 1, xfer_near = (uint16_t)((uint16_t)src - 1), xfer_n = n;
  far_out();
}
void from_text(void *dst, uint16_t a, uint16_t n)
{
  if (!n) return;
  xfer_far = (text_base | a) - 1, xfer_near = (uint16_t)((uint16_t)dst - 1), xfer_n = n;
  far_in();
}

/* ---- the screen, through the ROM's COUT ---- */
void cout(uint8_t c)
{
  poke0(RES_A, (uint8_t)(c | 0x80));
  rom_call(COUT);
}
void print(const char *s)
{
  while (*s) cout((uint8_t)*s++);
}
void crout(void) { cout('\r'); }
void print_w(uint32_t v, uint8_t width)
{
  char b[11];
  uint8_t i = 10;
  b[i] = 0;
  do b[--i] = (char)('0' + v % 10); while ((v /= 10) != 0);
  while (10 - i < width) cout(' '), width--;
  print(b + i);
}
void print_u(uint32_t v) { print_w(v, 0); }
void print_hex(uint16_t v, uint8_t digits)
{
  while (digits--) cout((uint8_t)"0123456789ABCDEF"[(v >> (4 * digits)) & 15]);
}
/* v/100 as d.dd */
void print_centi(uint32_t v)
{
  print_u(v / 100);
  cout('.');
  cout((uint8_t)('0' + v / 10 % 10));
  cout((uint8_t)('0' + v % 10));
}

void as_error(const char *m)
{
  crout();
  cout('?');
  print(m);
  print(" ERROR");
  cout(7);
}

/* ---- the MLI ---- */
uint8_t mli(uint8_t cmd, void *parms, uint8_t n)
{
  uint8_t e;
  to0(RES_PARMS, parms, n);
  e = mli_call(cmd);
  from0(parms, RES_PARMS, n);
  return e;
}

void put_path(const uint8_t *p)
{
  to0(RES_PATH, p, (uint16_t)(p[0] + 1));
}

uint8_t open_path(uint8_t *ref)
{
  OpenParms o = {3, RES_PATH, RES_FBUF, 0};
  uint8_t e = mli(MLI_OPEN, &o, sizeof o);
  *ref = o.ref;
  return e;
}
void close_ref(uint8_t ref)
{
  RefParms c = {1, ref};
  mli(MLI_CLOSE, &c, sizeof c);
}

void p8_now(uint16_t *date, uint16_t *time)
{
  uint8_t d[4], none = 0;
  mli(MLI_GET_TIME, &none, 0);
  from0(d, P8_DATE, 4);
  *date = (uint16_t)(d[0] | d[1] << 8), *time = (uint16_t)(d[2] | d[3] << 8);
}

#pragma clang attribute pop
