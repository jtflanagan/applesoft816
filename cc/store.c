/* store.c - STORE and RESTORE: the variables to and from a VAR file in
 * BASIC.SYSTEM's own form, which is AppleSoft's variable memory as it lay:
 *
 *   +0  the tables' length (simple variables and arrays), +2 the simple
 *       variables', +4 HIMEM's page; the type's aux is HIMEM less the rest
 *   simple variables, 7 bytes: the name's two characters (real: both bit 7
 *       clear; integer: both set; string: the second; DEF FN: the first),
 *       the value (packed real; integer high, low; string length, address)
 *   arrays: name, the offset to the next, the dimensions' count, each one's
 *       size (high, low; the last first), the elements (the first
 *       subscript fastest; a string 3 bytes)
 *   the strings, up to HIMEM
 *
 * Ours hold reals and integers the same way (packed MBF, integers high
 * first, elements in the same order); a string's descriptor has a bank
 * too.  The file is built in (and read into) the CHAIN bank's low 32K.
 */
#include <stdint.h>
#include <string.h>

#pragma clang attribute push (__attribute__((minsize)), apply_to = function)

#include "dos.h"
#include "machine.h"

#define HIMEM 0x9600u
#define SH_BANK 0x48                /* the VM's direct page: the string heap */
#define SH_FREE 0x49
#define SH_END 0x4B
#define SH_HALF 0x4D

static void vm_get(void *p, uint16_t a, uint16_t n)
{
  if (!n) return;
  xfer_far = (vm_base | a) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  far_in();
}
static void vm_put(uint16_t a, const void *p, uint16_t n)
{
  if (!n) return;
  xfer_far = (vm_base | a) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  far_out();
}
static void x_put(uint16_t a, const void *p, uint16_t n)
{
  if (!n) return;
  xfer_far = (xfer_base | a) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  far_out();
}
static void x_get(void *p, uint16_t a, uint16_t n)
{
  if (!n) return;
  xfer_far = (xfer_base | a) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  far_in();
}

static uint8_t esize_as(uint8_t type) { return type == 1 ? 2 : type == 2 ? 3 : 5; }
static uint8_t esize_ours(uint8_t type) { return type == 1 ? 2 : type == 2 ? 4 : 5; }

/* the name as AppleSoft marks its type */
static void as_name(const uint8_t name[3], uint8_t out[2])
{
  out[0] = name[0], out[1] = name[1];
  if (name[2] == 1) out[0] |= 0x80, out[1] |= 0x80;
  if (name[2] == 2) out[1] |= 0x80;
}

static uint16_t prog;               /* the VM image's program header */
static void clear_vars(void);

/* ---- STORE ---- */
static uint16_t f_at, s_at, s_base;  /* the file's write position; the strings' */

/* a string (our descriptor d) into the file's string area: its 3 bytes */
static void str_out(const uint8_t d[4], uint8_t out[3])
{
  uint16_t p = s_at;
  out[0] = d[0], out[1] = (uint8_t)p, out[2] = (uint8_t)(p >> 8);
  if (!d[0]) { out[1] = out[2] = 0; return; }
  xfer_far = ((uint32_t)d[3] << 16 | (uint16_t)(d[1] | d[2] << 8)) - 1;
  xfer_far2 = (xfer_base | (uint16_t)(f_at + (p - s_base))) - 1, xfer_n = d[0];
  far_move();
  s_at = (uint16_t)(s_at + d[0]);
}

/* the array's element count and our bytes; 0 if never dimensioned */
static uint32_t elements(uint16_t desc, uint8_t nd, uint8_t *dsc)
{
  uint32_t n = 1;
  uint8_t k;
  vm_get(dsc, desc, (uint16_t)(4 + 2 * nd));
  if (!(dsc[0] | dsc[1])) return 0;
  for (k = 0; k < nd; k++) n *= (uint32_t)(dsc[4 + 2 * k] | dsc[5 + 2 * k] << 8) + 1;
  return n;
}

uint8_t prog_store(const uint8_t *path)
{
  uint16_t nv, na, i, slot, t_simple, t_all, strings = 0, len, e;
  uint32_t n, total;
  uint8_t name[3], nd, d[4 + 2 * 88], v[8], k;
  CreateParms c;
  PathParms dp;
  InfoParms info;
  ReadParms w;
  uint8_t err, ref;
  if ((k = (uint8_t)prog_vars(&prog)) == 1) return B_TOO_LARGE;
  if (k) clear_vars();
  cc_tables(&nv, &na);
  /* sizes first: the tables, the strings */
  t_simple = (uint16_t)(7 * nv);
  total = t_simple;
  for (i = 0; i < nv; i++) {
    cc_entry(i, name, &nd, &slot);
    if (name[2] == 2) { vm_get(v, slot, 4); strings = (uint16_t)(strings + v[0]); }
  }
  for (i = 0; i < na; i++) {
    cc_entry((uint16_t)(nv + i), name, &nd, &slot);
    if (!(n = elements(slot, nd, d))) continue;
    total += 5 + 2 * (uint32_t)nd + n * esize_as(name[2]);
    if (name[2] == 2) {
      uint16_t st = (uint16_t)(d[0] | d[1] << 8);
      for (e = 0; e < n; e++) { vm_get(v, (uint16_t)(st + 4 * e), 1); strings = (uint16_t)(strings + v[0]); }
    }
  }
  if (total + strings + 5 > 0x7FFF) return B_RANGE;   /* (more than the CHAIN bank's half) */
  t_all = (uint16_t)total;
  v[0] = (uint8_t)t_all, v[1] = (uint8_t)(t_all >> 8), v[2] = (uint8_t)t_simple, v[3] = (uint8_t)(t_simple >> 8);
  v[4] = (uint8_t)(HIMEM >> 8);
  x_put(0, v, 5);
  f_at = (uint16_t)(5 + t_all);                      /* the strings' place in the file */
  s_base = s_at = (uint16_t)(HIMEM - strings);
  /* the simple variables */
  for (i = 0, len = 5; i < nv; i++, len = (uint16_t)(len + 7)) {
    cc_entry(i, name, &nd, &slot);
    as_name(name, v);
    memset(v + 2, 0, 5);
    if (name[2] == 2) {
      uint8_t sd[4];
      vm_get(sd, slot, 4);
      str_out(sd, v + 2);
    } else
      vm_get(v + 2, slot, name[2] == 1 ? 2 : 5);
    x_put(len, v, 7);
  }
  /* the arrays */
  for (i = 0; i < na; i++) {
    uint16_t st;
    cc_entry((uint16_t)(nv + i), name, &nd, &slot);
    if (!(n = elements(slot, nd, d))) continue;
    st = (uint16_t)(d[0] | d[1] << 8);
    as_name(name, v);
    e = (uint16_t)(5 + 2 * nd + n * esize_as(name[2]));
    v[2] = (uint8_t)e, v[3] = (uint8_t)(e >> 8), v[4] = nd;
    x_put(len, v, 5);
    len = (uint16_t)(len + 5);
    for (k = nd; k-- > 0;) {                         /* sizes, the last first, high first */
      uint16_t sz = (uint16_t)((d[4 + 2 * k] | d[5 + 2 * k] << 8) + 1);
      v[0] = (uint8_t)(sz >> 8), v[1] = (uint8_t)sz;
      x_put(len, v, 2);
      len = (uint16_t)(len + 2);
    }
    if (name[2] != 2) {                              /* reals, integers: as they are */
      xfer_far = (vm_base | st) - 1, xfer_far2 = (xfer_base | len) - 1, xfer_n = (uint16_t)(n * esize_as(name[2]));
      far_move();
      len = (uint16_t)(len + n * esize_as(name[2]));
    } else
      for (e = 0; e < n; e++, len = (uint16_t)(len + 3)) {
        uint8_t sd[4];
        vm_get(sd, (uint16_t)(st + 4 * e), 4);
        str_out(sd, v);
        x_put(len, v, 3);
      }
  }
  len = (uint16_t)(f_at + strings);                  /* the file's length */
  /* the file, made anew (one of another type: not ours to replace) */
  put_path(path);
  memset(&info, 0, sizeof info);
  info.n = 10, info.path = RES_PATH;
  if (!mli(MLI_GET_FILE_INFO, &info, sizeof info)) {
    if (info.type != 0xFD) return B_FILE_TYPE;
    dp.n = 1, dp.path = RES_PATH;
    if ((err = mli(MLI_DESTROY, &dp, sizeof dp)) != 0) return dos_error(err);
  }
  c.n = 7, c.path = RES_PATH, c.access = 0xC3, c.type = 0xFD, c.aux = (uint16_t)(HIMEM - (len - 5));
  c.storage = 0x01;
  p8_now(&c.cdate, &c.ctime);
  if ((err = mli(MLI_CREATE, &c, sizeof c)) != 0) return dos_error(err);
  if ((err = open_path(&ref)) != 0) return dos_error(err);
  w.n = 4, w.ref = ref, w.buf = RES_IOBUF;
  for (i = 0; i < len && !err; i = (uint16_t)(i + w.req)) {
    w.req = (uint16_t)(len - i) < RES_IOSIZE ? (uint16_t)(len - i) : RES_IOSIZE;
    xfer_far = (xfer_base | i) - 1, xfer_far2 = (uint32_t)RES_IOBUF - 1, xfer_n = w.req;
    far_move();
    err = mli(MLI_WRITE, &w, sizeof w);
  }
  close_ref(ref);
  return err ? dos_error(err) : 0;
}

/* ---- RESTORE ---- */
static uint16_t f_len, f_himem, f_lo;   /* the file's length; its strings' addresses, which end it */

/* the string at address p, length n, from the file into the string heap:
 * our descriptor into d; 0, or no room */
static int str_in(uint8_t n, uint16_t p, uint8_t d[4])
{
  uint16_t free, end;
  static const uint8_t z[2] = {0, 0};
  uint8_t bank = peek0((uint16_t)(vm_dp + SH_BANK));
  d[0] = n, d[1] = d[2] = d[3] = 0;
  if (!n) return 0;
  if (p < f_lo || (uint32_t)p + n > f_himem) { d[0] = 0; return 0; }   /* (into a program: gone) */
  free = (uint16_t)(peek0((uint16_t)(vm_dp + SH_FREE)) | peek0((uint16_t)(vm_dp + SH_FREE + 1)) << 8);
  end = (uint16_t)(peek0((uint16_t)(vm_dp + SH_END)) | peek0((uint16_t)(vm_dp + SH_END + 1)) << 8);
  if ((uint32_t)free + n + 2 > (end ? end : 0x10000ul)) return 1;
  xfer_far = ((uint32_t)bank << 16 | free) - 1, xfer_near = (uint16_t)((uint16_t)z - 1), xfer_n = 2;
  far_out();                                         /* the header: not moved */
  xfer_far = (xfer_base | (uint16_t)(f_len - (f_himem - p))) - 1;
  xfer_far2 = ((uint32_t)bank << 16 | (uint16_t)(free + 2)) - 1, xfer_n = n;
  far_move();
  d[1] = (uint8_t)(free + 2), d[2] = (uint8_t)((free + 2) >> 8), d[3] = bank;
  free = (uint16_t)(free + n + 2);
  poke0((uint16_t)(vm_dp + SH_FREE), (uint8_t)free), poke0((uint16_t)(vm_dp + SH_FREE + 1), (uint8_t)(free >> 8));
  return 0;
}

/* the variable named so: its slot (an array's descriptor), or one made */
static uint16_t slot_of(const uint8_t name[3], uint8_t nd)
{
  uint16_t nv, na, i, slot;
  uint8_t other[3], ond;
  cc_tables(&nv, &na);
  for (i = 0; i < (nd ? na : nv); i++) {
    cc_entry((uint16_t)(nd ? nv + i : i), other, &ond, &slot);
    if (!memcmp(name, other, 3)) return nd && ond != nd ? 0 : slot;
  }
  return compile_adopt(name, nd);
}

/* CLEAR's part for the variables, as clear_all does it (vm/vm.s): zero
 * (direct lines' too), descriptors pristine, the array heap and the
 * strings empty; the stacks and DATA stay */
static void clear_vars(void)
{
  uint16_t h[18], a;
  uint8_t z[64];
  memset(z, 0, sizeof z);
  vm_get(h, prog, sizeof h);
  for (a = h[13]; a < h[14]; a = (uint16_t)(a + (uint16_t)(h[14] - a < 64 ? h[14] - a : 64)))
    vm_put(a, z, (uint16_t)(h[14] - a < 64 ? h[14] - a : 64));
  if (h[17])
    for (a = h[17]; a; a = (uint16_t)(a + 64 > a ? a + 64 : 0))
      vm_put(a, z, (uint16_t)(0x10000ul - a < 64 ? 0x10000ul - a : 64));
  xfer_far = (vm_base | h[15]) - 1, xfer_far2 = (vm_base | h[14]) - 1, xfer_n = h[16];
  if (h[16]) far_move_up();
  vm_put((uint16_t)(prog + 8), &h[12], 2);
  poke0((uint16_t)(vm_dp + SH_FREE), 0), poke0((uint16_t)(vm_dp + SH_FREE + 1), 0);
  poke0((uint16_t)(vm_dp + SH_END), 0), poke0((uint16_t)(vm_dp + SH_END + 1), 0x80);
  poke0((uint16_t)(vm_dp + SH_HALF), 0);
}

uint8_t prog_restore(const uint8_t *path)
{
  InfoParms info;
  ReadParms r;
  uint8_t err, ref, v[8], name[3], nd, k, bounds[2 * 88], dsc[2];
  uint16_t at, t_all, t_simple, slot, hf, hend, e;
  uint32_t n;
  put_path(path);
  memset(&info, 0, sizeof info);
  info.n = 10, info.path = RES_PATH;
  if ((err = mli(MLI_GET_FILE_INFO, &info, sizeof info)) != 0) return dos_error(err);
  if (info.type != 0xFD) return B_FILE_TYPE;
  if ((err = open_path(&ref)) != 0) return dos_error(err);
  r.n = 4, r.ref = ref, r.buf = RES_IOBUF;
  for (f_len = 0;;) {
    r.req = RES_IOSIZE, r.trans = 0;
    err = mli(MLI_READ, &r, sizeof r);
    if (err == MLI_EOF || (!err && !r.trans)) { err = 0; break; }
    if (err || (uint32_t)f_len + r.trans > 0x8000) break;
    xfer_far = (uint32_t)RES_IOBUF - 1, xfer_far2 = (xfer_base | f_len) - 1, xfer_n = r.trans;
    far_move();
    f_len = (uint16_t)(f_len + r.trans);
  }
  close_ref(ref);
  if (err) return dos_error(err);
  if (f_len < 5) return B_FILE_TYPE;
  x_get(v, 0, 5);
  t_all = (uint16_t)(v[0] | v[1] << 8), t_simple = (uint16_t)(v[2] | v[3] << 8);
  if (t_simple > t_all || 5u + t_all > f_len) return B_FILE_TYPE;
  f_himem = (uint16_t)(v[4] << 8), f_lo = (uint16_t)(f_himem - (f_len - 5 - t_all));
  if (prog_vars(&prog) == 1) return B_TOO_LARGE;
  clear_vars();
  vm_get(&hf, (uint16_t)(prog + 8), 2);
  vm_get(&hend, (uint16_t)(prog + 10), 2);
  /* the simple variables */
  for (at = 5; at + 7 <= 5u + t_simple; at = (uint16_t)(at + 7)) {
    x_get(v, at, 7);
    if ((v[0] & 0x80) && !(v[1] & 0x80)) continue;   /* DEF FN: it points into a program */
    name[0] = v[0] & 0x7F, name[1] = v[1] & 0x7F;
    name[2] = (v[0] & 0x80) ? 1 : (v[1] & 0x80) ? 2 : 0;
    if (!(slot = slot_of(name, 0))) continue;
    if (name[2] == 2) {
      uint8_t sd[4];
      if (str_in(v[2], (uint16_t)(v[3] | v[4] << 8), sd)) return B_TOO_LARGE;
      vm_put(slot, sd, 4);
    } else
      vm_put(slot, v + 2, name[2] == 1 ? 2 : 5);
  }
  /* the arrays */
  while (at + 5 <= 5u + t_all) {
    uint16_t next;
    x_get(v, at, 5);
    next = (uint16_t)(at + (v[2] | v[3] << 8));
    name[0] = v[0] & 0x7F, name[1] = v[1] & 0x7F;
    name[2] = (v[0] & 0x80) ? 1 : (v[1] & 0x80) ? 2 : 0;
    nd = v[4];
    if (!nd || nd > 88 || next <= at) break;
    at = (uint16_t)(at + 5);
    for (n = 1, k = nd; k-- > 0; at = (uint16_t)(at + 2)) {   /* sizes, the last first */
      uint16_t sz;
      x_get(dsc, at, 2);
      sz = (uint16_t)(dsc[0] << 8 | dsc[1]);
      bounds[2 * k] = (uint8_t)(sz - 1), bounds[2 * k + 1] = (uint8_t)((sz - 1) >> 8);
      n *= sz;
    }
    slot = slot_of(name, nd);
    if (slot && (uint32_t)hend + n * esize_ours(name[2]) <= hf) {   /* (the heap grows down) */
      hf = (uint16_t)(hf - n * esize_ours(name[2]));
      vm_put(slot, &hf, 2);
      vm_put((uint16_t)(slot + 4), bounds, (uint16_t)(2 * nd));
      if (name[2] != 2) {
        xfer_far = (xfer_base | at) - 1, xfer_far2 = (vm_base | hf) - 1, xfer_n = (uint16_t)(n * esize_as(name[2]));
        far_move();
      } else
        for (e = 0; e < n; e++) {
          uint8_t sd[4];
          x_get(v, (uint16_t)(at + 3 * e), 3);
          if (str_in(v[0], (uint16_t)(v[1] | v[2] << 8), sd)) return B_TOO_LARGE;
          vm_put((uint16_t)(hf + 4 * e), sd, 4);
        }
    }
    at = next;
  }
  vm_put((uint16_t)(prog + 8), &hf, 2);
  return 0;
}

#pragma clang attribute pop
