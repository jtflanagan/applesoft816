/* shell.c - the command line: BASIC.SYSTEM's place (docs/M5.md).
 *
 * A line typed is BASIC.SYSTEM's command (dos.c) if it is one, else
 * AppleSoft's: a numbered line goes into the program (the text bank),
 * LIST/NEW/RUN are done here, anything else is a direct line, compiled
 * against the program and run by the VM.  RUN compiles the program (or
 * takes it from the bytecode cache) and runs it.  COMPILE name is ours: a
 * timed compile, not run.
 *
 * Everything reaches the machine through machine.c and glue.s: the ROM's
 * COUT/GETLN in emulation mode, the MLI with its parameters in bank 0
 * (res.h), the VM.
 */
#include <stdint.h>

/* the shell runs between programs: small before fast (the compiler, -Os) */
#pragma clang attribute push (__attribute__((minsize)), apply_to = function)
#include <string.h>

#include "dos.h"
#include "machine.h"
#include "parse.h"


/* the VM's direct page (vm/vm.s) and image header */
#define VM_DENTRY 0xE6
#define VM_FRESH 0xE8
#define VM_ONERR 0xE4
#define VM_CBREQ 0x12
#define VM_CURLIN 0x26
#define VM_CSP 0x24
#define VM_DATPTR 0xCF
#define VM_CONTIP 0xE9
#define VM_T0 0x30
#define VM_CMDLEN 0x18
#define VM_IOQUIET 0x11
#define VMH_DIRECT 0x18
#define VMH_CMDBUF 0x1A

static uint8_t raw[256];             /* the line typed, 7-bit, 0 after (INLIN's) */
static uint8_t len;

/* the program in the text bank: $0801 to prog_end (after its 00 00) */
static uint16_t prog_end = 0x0803;
static uint8_t compiled;             /* the VM bank holds it, compiled */
static uint8_t from_file;            /* it is path's file, unedited: the cache applies */

/* the program: its pathname (length first), and what the cache checks */
static uint8_t path[65];
static uint16_t src_len, src_hash;
static uint16_t vm_print;           /* the VM image's hash: bytecode names its handlers */
static uint16_t prog_start;         /* where the VM image ends and the program begins */

static int is_digit(uint8_t c) { return c >= '0' && c <= '9'; }

/* say what a compile cost (TIMING turns it on for RUN; COMPILE always) */
static uint8_t timing, say_time;

/* ---- the program from a file, to a file ---- */

static void hash_source(void)
{
  src_len = (uint16_t)(prog_end - 0x0801);
  hash_acc = 0;
  xfer_far = (text_base | 0x0801) - 1, xfer_n = src_len;
  if (src_len) far_hash();
  src_hash = hash_acc;
}

/* LOAD: the BAS file at p into the text bank at $0801; an MLI error (or
 * $4A for the file's type, $4D for its size) */
uint8_t prog_load(const uint8_t *p)
{
  InfoParms info;
  ReadParms r;
  uint8_t e, ref, z[2] = {0, 0};
  uint16_t at = 0x0801;
  put_path(p);
  memset(&info, 0, sizeof info);
  info.n = 10, info.path = RES_PATH;
  if ((e = mli(MLI_GET_FILE_INFO, &info, sizeof info)) != 0) return e;
  if (info.type != 0xFC) return 0x4A;
  if ((e = open_path(&ref)) != 0) return e;
  r.n = 4, r.ref = ref, r.buf = RES_IOBUF, r.req = RES_IOSIZE;
  for (;;) {
    r.trans = 0;
    e = mli(MLI_READ, &r, sizeof r);
    if (e == MLI_EOF || (!e && !r.trans)) { e = 0; break; }
    if (e) break;
    if ((uint32_t)at + r.trans > 0xFFF0) { e = 0x4D; break; }
    xfer_far = (uint32_t)RES_IOBUF - 1, xfer_far2 = (text_base | at) - 1, xfer_n = r.trans;
    far_move();
    at = (uint16_t)(at + r.trans);
  }
  close_ref(ref);
  if (e) {
    static const uint8_t none[2] = {0, 0};
    to_text(0x0801, none, 2);              /* (half a program is none) */
    prog_end = 0x0803, compiled = 0, from_file = 0;
    return e;
  }
  /* the end: two zero bytes after it, whatever the file had; the links
   * made for here, wherever it was saved from (FIX.LINKS) */
  to_text(at, z, 2);
  prog_end = (uint16_t)(fix_links() + 2);
  memcpy(path, p, (size_t)p[0] + 1);
  compiled = 0, from_file = 1;
  hash_source();
  return 0;
}

/* SAVE: the program as a BAS file at p (made anew) */
uint8_t prog_save(const uint8_t *p)
{
  CreateParms c;
  PathParms d;
  ReadParms w;
  uint8_t e, ref;
  uint16_t at, n;
  put_path(p);
  d.n = 1, d.path = RES_PATH;
  mli(MLI_DESTROY, &d, sizeof d);
  c.n = 7, c.path = RES_PATH, c.access = 0xC3, c.type = 0xFC, c.aux = 0x0801, c.storage = 0x01;
  p8_now(&c.cdate, &c.ctime);
  if ((e = mli(MLI_CREATE, &c, sizeof c)) != 0) return e;
  if ((e = open_path(&ref)) != 0) return e;
  w.n = 4, w.ref = ref, w.buf = RES_IOBUF;
  for (at = 0x0801; at < prog_end && !e; at = (uint16_t)(at + n)) {
    n = (uint16_t)(prog_end - at) < RES_IOSIZE ? (uint16_t)(prog_end - at) : RES_IOSIZE;
    xfer_far = (text_base | at) - 1, xfer_far2 = (uint32_t)RES_IOBUF - 1, xfer_n = n;
    far_move();
    w.req = n;
    e = mli(MLI_WRITE, &w, sizeof w);
  }
  close_ref(ref);
  if (!e) {                               /* it is that file now */
    memcpy(path, p, (size_t)p[0] + 1);
    hash_source();
    from_file = 1;
  }
  return e;
}

/* ---- the bytecode cache: DIR/AS816.CACHE/NAME beside DIR/NAME (ProDOS
 * names leave no room for a suffix), the VM bank's image of the compiled
 * program from prog_start to the stacks, after a header that says which
 * source and which VM it was made from, then the compiler's tables ---- */
typedef struct {
  uint8_t magic[4];                 /* "A816" */
  uint8_t kind, version;            /* 'C', CACHE_VERSION */
  uint16_t vm_print, src_len, src_hash, start, len, lines, code;
  uint16_t state;                   /* the compiler's tables after the image: their length */
} CacheHdr;

#define CACHE_DIR "AS816.CACHE"
#define CACHE_VERSION 4             /* 2: the compiler's tables after the image; 3: salist_cap;
                                     * 4: their length in the header */

/* n bytes between the open cache file and the stage bank (the compiler's
 * tables), through the I/O buffer: 0 or an MLI error */
static uint8_t stage_file(uint8_t ref, uint16_t n, int save)
{
  ReadParms r;
  uint16_t at = 0, k;
  uint8_t e;
  r.n = 4, r.ref = ref, r.buf = RES_IOBUF;
  while (at < n) {
    k = (uint16_t)(n - at) < RES_IOSIZE ? (uint16_t)(n - at) : RES_IOSIZE;
    r.req = k, r.trans = 0;
    if (save) {
      xfer_far = (stage_base | at) - 1, xfer_far2 = (uint32_t)RES_IOBUF - 1, xfer_n = k;
      far_move();
      if ((e = mli(MLI_WRITE, &r, sizeof r)) != 0) return e;
    } else {
      if ((e = mli(MLI_READ, &r, sizeof r)) != 0) return e;
      if (r.trans != k) return MLI_EOF;
      xfer_far = (uint32_t)RES_IOBUF - 1, xfer_far2 = (stage_base | at) - 1, xfer_n = k;
      far_move();
    }
    at = (uint16_t)(at + k);
  }
  return 0;
}

/* the cache's pathname (or, dir_only, its directory's) into out; 0 if too long */
static int cache_path(uint8_t *out, int dir_only)
{
  static const char cd[] = CACHE_DIR;
  uint8_t n = path[0], i, cut = 0, k = 0;
  for (i = 1; i <= n; i++)
    if (path[i] == '/') cut = i;
  if (cut + sizeof cd + (n - cut) > 64) return 0;
  for (i = 1; i <= cut; i++) out[++k] = path[i];
  for (i = 0; cd[i]; i++) out[++k] = (uint8_t)cd[i];
  if (!dir_only) {
    out[++k] = '/';
    for (i = (uint8_t)(cut + 1); i <= n; i++) out[++k] = path[i];
  }
  out[0] = k;
  return 1;
}

static int cache_load(CompileStats *st)
{
  uint8_t cp[65], ref, e;
  CacheHdr h;
  ReadParms r;
  uint16_t at, got = 0;
  if (!cache_path(cp, 0)) return 0;
  put_path(cp);
  if (open_path(&ref)) return 0;
  r.n = 4, r.ref = ref, r.buf = RES_IOBUF, r.req = sizeof h, r.trans = 0;
  if (mli(MLI_READ, &r, sizeof r) || r.trans != sizeof h) goto no;
  from0(&h, RES_IOBUF, sizeof h);
  if (memcmp(h.magic, "A816", 4) || h.kind != 'C' || h.version != CACHE_VERSION || h.vm_print != vm_print ||
      h.src_len != src_len || h.src_hash != src_hash || h.start != prog_start)
    goto no;
  at = h.start;
  while (got < h.len) {
    r.req = (uint16_t)(h.len - got) < RES_IOSIZE ? (uint16_t)(h.len - got) : RES_IOSIZE;
    r.trans = 0;
    e = mli(MLI_READ, &r, sizeof r);
    if (e || r.trans != r.req) goto no;
    xfer_far = (uint32_t)RES_IOBUF - 1, xfer_far2 = (vm_base | at) - 1, xfer_n = r.trans;
    far_move();
    at = (uint16_t)(at + r.trans), got = (uint16_t)(got + r.trans);
  }
  e = stage_file(ref, h.state, 0);
  close_ref(ref);
  if (e || cc_state_load(h.state)) return 0;
  st->lines = h.lines, st->code_size = h.code;
  return 1;
no:
  close_ref(ref);
  return 0;
}

/* the compiled program into the cache; quietly nothing if the disk says no */
static void cache_save(const CompileStats *st)
{
  uint8_t cp[65], ref;
  CacheHdr h;
  CreateParms c;
  PathParms d;
  ReadParms w;
  uint16_t end = vm_read16((uint16_t)(prog_start + 2)), at, n;   /* the header's fstack */
  memcpy(h.magic, "A816", 4);
  h.kind = 'C', h.version = CACHE_VERSION, h.vm_print = vm_print, h.src_len = src_len, h.src_hash = src_hash;
  h.start = prog_start, h.len = (uint16_t)(end - prog_start), h.lines = st->lines, h.code = st->code_size;
  h.state = cc_state_save();
  if (!cache_path(cp, 1)) return;
  put_path(cp);
  c.n = 7, c.path = RES_PATH, c.access = 0xC3, c.type = 0x0F, c.aux = 0, c.storage = 0x0D;
  p8_now(&c.cdate, &c.ctime);
  mli(MLI_CREATE, &c, sizeof c);              /* (there already, mostly) */
  cache_path(cp, 0);
  put_path(cp);
  d.n = 1, d.path = RES_PATH;
  mli(MLI_DESTROY, &d, sizeof d);
  c.type = 0x06, c.aux = prog_start, c.storage = 0x01;
  if (mli(MLI_CREATE, &c, sizeof c) || open_path(&ref)) return;
  to0(RES_IOBUF, &h, sizeof h);
  w.n = 4, w.ref = ref, w.buf = RES_IOBUF, w.req = sizeof h;
  if (!mli(MLI_WRITE, &w, sizeof w))
    for (at = prog_start; at < end; at = (uint16_t)(at + n)) {
      n = (uint16_t)(end - at) < RES_IOSIZE ? (uint16_t)(end - at) : RES_IOSIZE;
      xfer_far = (vm_base | at) - 1, xfer_far2 = (uint32_t)RES_IOBUF - 1, xfer_n = n;
      far_move();
      w.req = n;
      if (mli(MLI_WRITE, &w, sizeof w)) break;
    }
  stage_file(ref, h.state, 1);
  close_ref(ref);
}

/* the program in memory, compiled (from the cache when it is a file's and
 * the cache has it); 0, having said how long the compile took */
static int compile_current(void)
{
  CompileStats st;
  uint32_t clk_half;
  uint8_t e;
  if (compiled) return 0;
  if (from_file && cache_load(&st)) {
    compiled = 1;
    poke0((uint16_t)(vm_dp + VM_FRESH), 1);
    if (!say_time) return 0;
    crout();
    print_u(st.lines);
    print(" LINES, ");
    print_u(st.code_size);
    print(" BYTES: CACHED");
    crout();
    return 0;
  }
  e = (uint8_t)compile(&st, &clk_half);
  if (e) { as_error("OUT OF MEMORY"); return 1; }
  compiled = 1;
  poke0((uint16_t)(vm_dp + VM_FRESH), 1);
  if (from_file) cache_save(&st);
  if (!st.lines || !say_time) return 0;       /* (no program: nothing to say) */
  crout();
  print_u(st.lines);
  print(" LINES, ");
  print_u(st.code_size);
  print(" BYTES: ");
  print_centi(clk_half * 200 / 15734);       /* half-lines to hundredths */
  print(" S");
  crout();
  return 0;
}

/* ---- running ---- */

/* what a program's CHR$(4) asked for, done once the VM is back: 1 RUN,
 * 2 LOAD, 3 BYE, 4 CHAIN */
static uint8_t pending, pending_path[65];
static int32_t pending_line;
static int bye;
static uint8_t ran;                  /* a program ran: FILE(S) STILL OPEN is due */
static uint8_t from_exec;            /* the line came from EXEC (RUN's CR is not seen) */

void prog_pending(uint8_t what, const uint8_t *p, int32_t line)
{
  pending = what, pending_line = line;
  if (p) memcpy(pending_path, p, (size_t)p[0] + 1);
}

static void direct_tokens(void);

/* RUN [n] of the program in memory */
static uint8_t run_file;              /* RUN path: no blank line first */
static void run_from(int32_t line)
{
  say_time = timing;
  ran = 1;
  if (!from_exec && !run_file) crout();   /* (RUN of the program in memory starts a line) */
  run_file = 0;
  if (line < 0) {
    if (!compile_current()) vm_call(0);
    return;
  }
  /* RUN n: a direct line of its own */
  {
    uint8_t k = 1;
    char d[6];
    int8_t j = 0;
    raw[0] = 0xAC;                        /* RUN */
    do d[j++] = (char)('0' + line % 10); while ((line /= 10) != 0);
    while (j) raw[k++] = (uint8_t)d[--j];
    raw[k] = 0;
    direct_tokens();
  }
}

/* ---- CHAIN: the program's variables, by name, into the next one's slots.
 * The spare bank holds them meanwhile: per variable a kind (0 scalar, 1
 * array, $FF the end), its name (two characters, its type), then for a
 * scalar its size and value (a string's descriptor: the string stays in
 * the heap, which CHAIN keeps); for an array its dimensions, element size,
 * bounds, the bytes' count and the bytes. ---- */
static uint16_t chain_at;
static void chain_put(const void *p, uint16_t n)
{
  if (!n) return;
  xfer_far = (xfer_base | chain_at) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  far_out();
  chain_at = (uint16_t)(chain_at + n);
}
static void chain_get(void *p, uint16_t n)
{
  if (!n) return;
  xfer_far = (xfer_base | chain_at) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  far_in();
  chain_at = (uint16_t)(chain_at + n);
}
static void vm_get(void *p, uint16_t a, uint16_t n)
{
  xfer_far = (vm_base | a) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  if (n) far_in();
}
static void vm_put(uint16_t a, const void *p, uint16_t n)
{
  xfer_far = (vm_base | a) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  if (n) far_out();
}

/* a string outside the heap (a literal in the old program, or a copy an
 * earlier CHAIN made) goes with it: copied into this CHAIN's half of the
 * spare bank's top 32K, its descriptor (at d) pointed there */
static uint8_t chain_half;
static uint16_t chain_str;
static void chain_keep(uint8_t *d)
{
  uint8_t sb = peek0((uint16_t)(vm_dp + 0x48));   /* SH_BANK: the heap's */
  if (!d[0] || d[3] == sb) return;
  if ((uint32_t)chain_str + d[0] > (chain_half ? 0x10000ul : 0xC000ul)) return;   /* (no room: as it was) */
  xfer_far = ((uint32_t)d[3] << 16 | (uint16_t)(d[1] | d[2] << 8)) - 1, xfer_far2 = (xfer_base | chain_str) - 1, xfer_n = d[0];
  far_move();
  d[1] = (uint8_t)chain_str, d[2] = (uint8_t)(chain_str >> 8), d[3] = (uint8_t)(xfer_base >> 16);
  chain_str = (uint16_t)(chain_str + d[0]);
}

static void chain_export(void)
{
  uint16_t i, addr, n, e, nv, na;
  uint8_t name[3], nd, k, v[8], d[4 + 2 * 88];
  uint32_t count;
  cc_tables(&nv, &na);
  chain_at = 0;
  chain_half ^= 1;
  chain_str = (uint16_t)(0x8000u + chain_half * 0x4000u);
  for (i = 0; i < nv; i++) {
    cc_entry(i, name, &nd, &addr);
    k = name[2] == 1 ? 2 : name[2] == 2 ? 4 : 5;
    vm_get(v, addr, k);
    if (name[2] == 2) chain_keep(v);
    chain_put("\0", 1), chain_put(name, 3), chain_put(&k, 1), chain_put(v, k);
  }
  for (i = 0; i < na; i++) {
    cc_entry((uint16_t)(nv + i), name, &nd, &addr);
    vm_get(d, addr, (uint16_t)(4 + 2 * nd));
    if (!(d[0] | d[1])) continue;               /* never dimensioned */
    for (count = 1, k = 0; k < nd; k++) count *= (uint32_t)(d[4 + 2 * k] | d[5 + 2 * k] << 8) + 1;
    count *= d[3];
    if ((uint32_t)chain_at + count + 300 > 0x7FFF) continue;   /* (no room below the strings: lost) */
    n = (uint16_t)count;
    chain_put("\1", 1), chain_put(name, 3), chain_put(&nd, 1), chain_put(d + 3, 1);
    chain_put(d + 4, (uint16_t)(2 * nd)), chain_put(&n, 2);
    xfer_far = (vm_base | (uint16_t)(d[0] | d[1] << 8)) - 1, xfer_far2 = (xfer_base | chain_at) - 1, xfer_n = n;
    if (n) far_move();
    if (name[2] == 2)                           /* a string array's elements: keep theirs */
      for (e = 0; e < n; e = (uint16_t)(e + 4)) {
        xfer_far = (xfer_base | (uint16_t)(chain_at + e)) - 1, xfer_near = (uint16_t)((uint16_t)v - 1), xfer_n = 4;
        far_in();
        chain_keep(v);
        far_out();
      }
    chain_at = (uint16_t)(chain_at + n);
  }
  chain_put("\377", 1);
}

static void chain_import(void)
{
  uint8_t kind, name[3], other[3], k, nd, ond, esize, v[8], bounds[2 * 88];
  uint16_t i, addr, n, hf, hend, nv, na;
  vm_get(&hf, (uint16_t)(prog_start + 8), 2);    /* the array heap: its free pointer, end */
  vm_get(&hend, (uint16_t)(prog_start + 10), 2);
  chain_at = 0;
  cc_tables(&nv, &na);
  for (;;) {
    chain_get(&kind, 1);
    if (kind == 0xFF) break;
    chain_get(name, 3);
    if (!kind) {
      chain_get(&k, 1), chain_get(v, k);
      for (i = 0; i < nv; i++) {
        cc_entry(i, other, &ond, &addr);
        if (!memcmp(name, other, 3)) break;
      }
      if (i == nv) addr = compile_adopt(name, 0), cc_tables(&nv, &na);   /* (kept, unnamed here) */
      if (addr) vm_put(addr, v, k);
      continue;
    }
    chain_get(&nd, 1), chain_get(&esize, 1), chain_get(bounds, (uint16_t)(2 * nd)), chain_get(&n, 2);
    for (i = 0; i < na; i++) {
      cc_entry((uint16_t)(nv + i), other, &ond, &addr);
      if (!memcmp(name, other, 3)) break;
    }
    if (i == na) addr = compile_adopt(name, nd), ond = nd, cc_tables(&nv, &na);   /* (kept, unnamed here) */
    if (addr && ond == nd && (uint32_t)hf + n <= hend) {
      vm_put(addr, &hf, 2);                       /* storage, the bounds; the bytes */
      vm_put((uint16_t)(addr + 4), bounds, (uint16_t)(2 * nd));
      xfer_far = (xfer_base | chain_at) - 1, xfer_far2 = (vm_base | hf) - 1, xfer_n = n;
      if (n) far_move();
      hf = (uint16_t)(hf + n);
    }
    chain_at = (uint16_t)(chain_at + n);
  }
  vm_put((uint16_t)(prog_start + 8), &hf, 2);
}

/* STORE's and RESTORE's start: the program compiled, its variables to be
 * read or set without a CLEAR to come (a new image's stacks and DATA as a
 * CLEAR leaves them); prog_start into *at; 1 if it does not compile, 2 if
 * the variables are the caller's to CLEAR (a new image) */
int prog_vars(uint16_t *at)
{
  uint16_t w;
  if (compile_current()) return 1;
  *at = prog_start;
  if (!peek0((uint16_t)(vm_dp + VM_FRESH))) return 0;
  poke0((uint16_t)(vm_dp + VM_FRESH), 0);
  w = vm_read16((uint16_t)(prog_start + 4));
  poke0((uint16_t)(vm_dp + VM_CSP), (uint8_t)w), poke0((uint16_t)(vm_dp + VM_CSP + 1), (uint8_t)(w >> 8));
  w = vm_read16((uint16_t)(prog_start + 0x16));
  poke0((uint16_t)(vm_dp + VM_DATPTR), (uint8_t)w), poke0((uint16_t)(vm_dp + VM_DATPTR + 1), (uint8_t)(w >> 8));
  poke0((uint16_t)(vm_dp + VM_CONTIP), 0), poke0((uint16_t)(vm_dp + VM_CONTIP + 1), 0);
  return 2;
}

/* CHAIN path[,@line]: the program in, its variables the old one's, run
 * without a CLEAR (vm_direct, VM_FRESH off): strings stay, FOR/GOSUB, DATA,
 * ONERR and CONT start afresh */
static void chain_run(int32_t line)
{
  uint16_t w;
  uint8_t z = 0;
  ran = 1;
  if (compile_current()) return;
  chain_import();
  poke0((uint16_t)(vm_dp + VM_FRESH), 0);
  w = vm_read16((uint16_t)(prog_start + 4));           /* the control stack: empty */
  poke0((uint16_t)(vm_dp + VM_CSP), (uint8_t)w), poke0((uint16_t)(vm_dp + VM_CSP + 1), (uint8_t)(w >> 8));
  w = vm_read16((uint16_t)(prog_start + 0x16));        /* DATA from its start */
  poke0((uint16_t)(vm_dp + VM_DATPTR), (uint8_t)w), poke0((uint16_t)(vm_dp + VM_DATPTR + 1), (uint8_t)(w >> 8));
  poke0((uint16_t)(vm_dp + VM_CONTIP), 0), poke0((uint16_t)(vm_dp + VM_CONTIP + 1), 0);
  poke0((uint16_t)(vm_dp + VM_ONERR), 0), poke0((uint16_t)(vm_dp + VM_ONERR + 1), 0);
  poke0(0xD8, (uint8_t)(peek0(0xD8) & 0x7F));
  (void)z;
  if (line >= 0) {                               /* GOTO line, as a direct line */
    uint8_t k = 1;
    char d[6];
    int8_t j = 0;
    raw[0] = 0xAB;                               /* GOTO */
    do d[j++] = (char)('0' + line % 10); while ((line /= 10) != 0);
    while (j) raw[k++] = (uint8_t)d[--j];
    raw[k] = 0;
    direct_tokens();
    return;
  }
  w = vm_read16(prog_start);                     /* its entry */
  poke0((uint16_t)(vm_dp + VM_DENTRY), (uint8_t)w), poke0((uint16_t)(vm_dp + VM_DENTRY + 1), (uint8_t)(w >> 8));
  vm_call(vm_read16(VMH_DIRECT));
}

static void new_program(void);
static int find_line(uint16_t num, uint16_t *at, uint16_t *link);
static uint16_t del_from, del_to;        /* DEL's lines, for after_vm */
static uint16_t vm_dp16(uint8_t a)
{
  return (uint16_t)(peek0((uint16_t)(vm_dp + a)) | peek0((uint16_t)(vm_dp + a + 1)) << 8);
}

static void after_vm(void)
{
  uint8_t what, e;
  while ((what = pending) != 0) {
    pending = 0;
    if (what == 3) { bye = 1; return; }
    if (what == 5) { new_program(); return; }
    if (what == 6) {                             /* DEL: the lines out, FIX.LINKS */
      xfer_far = (text_base | del_to) - 1, xfer_far2 = (text_base | del_from) - 1;
      xfer_n = (uint16_t)(prog_end - del_to);
      far_move_up();
      prog_end = (uint16_t)(fix_links() + 2);
      compiled = 0, from_file = 0;
      return;
    }
    if (what == 4) chain_export();
    if ((e = prog_load(pending_path)) != 0) { dos_message(dos_error(e)); return; }
    if (what == 1) run_file = 1, run_from(pending_line);
    if (what == 4) chain_run(pending_line);
  }
}

void prog_chain(const uint8_t *p, int32_t line)
{
  uint8_t e;
  chain_export();
  if ((e = prog_load(p)) != 0) { dos_message(dos_error(e)); return; }
  chain_run(line);
  after_vm();
}

void prog_run(int32_t line)
{
  run_file = 1;
  run_from(line);
  after_vm();
}

/* the direct line in raw (tokens): compiled against the program - which is
 * compiled first, if it has to be - and run with its variables */
static void direct_tokens(void)
{
  uint16_t entry;
  say_time = timing;
  if (compile_current()) return;
  if (compile_direct(raw, &entry)) { as_error("OUT OF MEMORY"); return; }
  poke0((uint16_t)(vm_dp + VM_DENTRY), (uint8_t)entry);
  poke0((uint16_t)(vm_dp + VM_DENTRY + 1), (uint8_t)(entry >> 8));
  vm_call(vm_read16(VMH_DIRECT));
}

/* ---- the program in memory: lines in, out, listed ---- */
static void new_program(void)
{
  static const uint8_t z[2] = {0, 0};
  to_text(0x0801, z, 2);
  prog_end = 0x0803;
  compiled = 0, from_file = 0;
}

/* FNDLIN: the first line numbered num or more (or the end); 1 if it is num */
static int find_line(uint16_t num, uint16_t *at, uint16_t *link)
{
  uint8_t h[4];
  uint16_t a = 0x0801, n;
  for (;;) {
    from_text(h, a, 4);
    *at = a, *link = (uint16_t)(h[0] | h[1] << 8);
    if (!h[1]) return 0;
    n = (uint16_t)(h[2] | h[3] << 8);
    if (n >= num) return n == num;
    a = *link;
  }
}

/* a numbered line typed: tok (n bytes, its 0 last) in place of line num's;
 * n = 1 (nothing but the 0) takes the line out */
static int edit_line(uint16_t num, const uint8_t *tok, uint8_t n)
{
  int said = 0;
  uint16_t at, link, size = (uint16_t)(4 + n);
  uint8_t h[4];
  if (find_line(num, &at, &link)) {
    xfer_far = (text_base | link) - 1, xfer_far2 = (text_base | at) - 1, xfer_n = (uint16_t)(prog_end - link);
    far_move_up();
    prog_end = (uint16_t)(prog_end - (link - at));
  }
  if (n > 1) {
    if ((uint32_t)prog_end + size > 0xFF00) { as_error("OUT OF MEMORY"); said = 1; }
    else {
      xfer_far = (text_base | at) - 1, xfer_far2 = (text_base | (uint16_t)(at + size)) - 1;
      xfer_n = (uint16_t)(prog_end - at);
      far_move();
      h[0] = h[1] = 0xFF, h[2] = (uint8_t)num, h[3] = (uint8_t)(num >> 8);   /* (a link, for FIX.LINKS) */
      to_text(at, h, 4);
      to_text((uint16_t)(at + 4), tok, n);
      prog_end = (uint16_t)(prog_end + size);
    }
  }
  prog_end = (uint16_t)(fix_links() + 2);
  compiled = 0, from_file = 0;
  return said;
}

/* LIST ($D6A5 on), as the GS ROM does it: CR, a space, the number, then
 * each token as " WORD "; past column 33 (73 in 80 columns), a new line
 * from column 5 */
static int list_wrap(void)
{
  if (peek0(0xC01F) & 0x80) return peek0(0x057B) >= 0x49;
  return peek0(CH) >= 0x21;
}
/* where LIST's characters go: the screen, or WRITE's file (a program's
 * LIST under WRITE: the screen's column does not move, nothing wraps) */
static uint8_t list_file, lbuf[256], lbn;
static void lput(uint8_t c)
{
  if (!list_file) { cout(c); return; }
  lbuf[lbn++] = c;
  if (c == 0x0D || lbn == 255) dos_put(lbuf, lbn), lbn = 0;
}
static void list_out(uint8_t c)
{
  lput(c);
  if (!list_file && list_wrap()) { crout(); poke0(CH, 5); }
}
static void list_num(uint16_t v)
{
  char d[6];
  int8_t j = 0;
  do d[j++] = (char)('0' + v % 10); while ((v /= 10) != 0);
  while (j) lput((uint8_t)d[--j]);
}
static void list(uint16_t from, uint16_t to)
{
  const uint8_t *kw = as816_token_table();
  uint8_t l[256], i, c;
  uint16_t at, link, num, n;
  find_line(from, &at, &link);
  for (;;) {
    from_text(l, at, 4);
    link = (uint16_t)(l[0] | l[1] << 8);
    if (!l[1]) break;
    if (peek0(0xC000) == 0x83) {        /* ISCNTC: Ctrl-C */
      poke0(0xC010, 0);
      crout();
      print("BREAK");
      cout(7);
      break;
    }
    lput(0x0D);                         /* (CRDO before the range check: a line */
    num = (uint16_t)(l[2] | l[3] << 8); /* past it leaves a blank line) */
    if (num > to) break;
    n = (uint16_t)(link - at - 4) < 250 ? (uint16_t)(link - at - 4) : 250;
    from_text(l + 4, (uint16_t)(at + 4), n);
    l[4 + n] = 0;
    lput(' ');
    list_num(num);
    list_out(' ');
    for (i = 4; (c = l[i]) != 0; i++) {
      if (c < 0x80) { list_out(c); continue; }
      {
        uint16_t k = 0;
        uint8_t t = (uint8_t)(c - 0x80);
        while (t) { while (!(kw[k++] & 0x80)) {} t--; }
        lput(' ');
        while (!(kw[k] & 0x80)) lput(kw[k++]);
        lput((uint8_t)(kw[k] & 0x7F));
        list_out(' ');
      }
    }
    at = link;
  }
  lput(0x0D);
  if (lbn) dos_put(lbuf, lbn), lbn = 0;
}

/* a program's (or a direct line's) LIST, NEW, DEL (VM_CBREQ 4, 5, 6; the
 * numbers at VM_T0, VM_T1): 0 on, 1 the VM leaves (after_vm does it) */
static uint8_t text_request(uint8_t req)
{
  uint16_t a = vm_dp16(VM_T0), b = vm_dp16(VM_T0 + 2), link;
  if (req == 4) {
    list_file = (uint8_t)dos_writing();
    list(a, b);
    list_file = 0;
    return 0;
  }
  if (req == 6) {                         /* FNDLIN a, FNDLIN b+1: backwards, on */
    find_line(a, &del_from, &link);
    find_line((uint16_t)(b + 1), &del_to, &link);
    if (del_from > del_to) return 0;
  }
  pending = req;
  return 1;
}

/* ---- CHR$(4) commands from a running program: vm_command, called by the
 * VM (glue.s cmd_entry) with the command in its buffer.  0: go on; 1: the
 * program stops (an error said, or a RUN/LOAD/BYE pending); else an error
 * number for ONERR. ---- */
uint8_t cmd_answer;

static int onerr(void)
{
  return (peek0(0xD8) & 0x80) && (peek0((uint16_t)(vm_dp + VM_ONERR)) | peek0((uint16_t)(vm_dp + VM_ONERR + 1)));
}

uint8_t vm_command(void)
{
  uint8_t n, k, quiet = (uint8_t)onerr(), req = peek0((uint16_t)(vm_dp + VM_CBREQ));
  /* a running program's command, or a direct line's (NOT DIRECT COMMAND) */
  int deferred = peek0((uint16_t)(vm_dp + VM_CURLIN + 1)) != 0xFF;
  uint16_t b;
  if (req >= 4) return text_request(req);
  if (req) {                              /* a text file's (dos.c) */
    if ((k = dos_io(req)) == 0) return 0;
    if (quiet) return k;
    dos_message(k);
    return (uint8_t)(0x80 | k);
  }
  b = vm_read16(VMH_CMDBUF);
  xfer_far = (vm_base | b) - 1, xfer_near = (uint16_t)((uint16_t)&n - 1), xfer_n = 1;
  far_in();
  if (!n) return 0;
  xfer_far = (vm_base | (uint16_t)(b + 1)) - 1, xfer_near = (uint16_t)((uint16_t)raw - 1), xfer_n = n;
  far_in();
  k = dos_command(raw, n, deferred, quiet);
  if (k == DOS_OK) return 0;
  if (k == DOS_BYE) { pending = 3; return 1; }
  if (k == DOS_STOP) return 1;
  if (k == DOS_NOT) {                     /* not a command: BASIC.SYSTEM's SYNTAX ERROR */
    if (quiet) return 16;
    dos_message(16);
    return 0x80 | 16;
  }
  return quiet ? (uint8_t)(k & 0x7F) : k; /* (said: the VM stops, BREAK) */
}

/* ---- the command line ---- */

/* LINGET over the tokens at raw[*p]: a line number, digits (no spaces are
 * left by now); 0 for none; -1 past 63999 */
static int32_t linget(uint8_t *p)
{
  uint16_t n = 0;
  while (is_digit(raw[*p])) {
    if (n >= 6400) return -1;
    n = (uint16_t)(n * 10 + (raw[(*p)++] - '0'));
  }
  return n;
}

/* COMPILE name: ours, a timed compile; TIMING: RUN says it too (or not) */
static int compile_command(void)
{
  static const char w[] = "COMPILE", tw[] = "TIMING";
  uint8_t i = 0, k = 0, p[65], e;
  while (i < len && raw[i] == ' ') i++;
  for (k = 0; tw[k] && i + k < len && (raw[i + k] & 0xDF) == (uint8_t)tw[k]; k++) {}
  if (!tw[k] && i + k == len) {
    timing = (uint8_t)!timing;
    crout();
    print(timing ? "TIMING ON" : "TIMING OFF");
    crout();
    return 1;
  }
  for (k = 0; w[k]; k++, i++)
    if (i >= len || (raw[i] & 0xDF) != (uint8_t)w[k]) return 0;
  while (i < len && raw[i] == ' ') i++;
  for (k = 0; i < len && raw[i] != ' ' && k < 64; i++) p[++k] = (uint8_t)(raw[i] >= 'a' && raw[i] <= 'z' ? raw[i] - 32 : raw[i]);
  p[0] = k;
  if (!k) { dos_message(16); return 1; }
  if ((e = prog_load(p)) != 0) dos_message(dos_error(e));
  else {
    say_time = 1;
    compile_current();
  }
  return 1;
}

/* an AppleSoft line: numbered, into the program; else at once.  1 if
 * RESTART's CR is due before the prompt (all but a line taken in) */
static int basic_line(void)
{
  uint8_t x = 0, y, p;
  uint16_t num = 0;
  int32_t a, b;
  while (raw[x] == ' ') x++;
  if (is_digit(raw[x])) {
    while (is_digit(raw[x]) || raw[x] == ' ') {
      if (raw[x] != ' ') {
        if (num >= 6400) { as_error("SYNTAX"); return 1; }
        num = (uint16_t)(num * 10 + (raw[x] - '0'));
      }
      x++;
    }
    y = as816_parse(raw, x);
    return edit_line(num, raw, (uint8_t)(y - 4));
  }
  as816_parse(raw, x);
  switch (raw[0]) {
  case 0xBC:                            /* LIST [a][-,][b] */
    p = 1;
    if ((a = linget(&p)) < 0) break;
    b = a;
    if (raw[p] == 0xC9 || raw[p] == ',') {
      p++;
      if ((b = linget(&p)) < 0) break;
    }
    if (raw[p]) goto compiled;          /* more after it: a direct line */
    list((uint16_t)a, b ? (uint16_t)b : 0xFFFF);
    return 1;
  case 0xBF:                            /* NEW */
    if (raw[1]) goto compiled;
    new_program();
    return 1;
  case 0xAC:                            /* RUN (RUN n: a direct line) */
    if (raw[1]) direct_tokens();
    else run_from(-1);
    after_vm();
    return 1;
  case 0:
    return 1;
  default:
  compiled:
    direct_tokens();
    after_vm();
    return 1;
  }
  as_error("SYNTAX");
  return 1;
}

/* the prompt, line after line, until BYE */
static void prompt_loop(void)
{
  uint8_t i, k;
  while (!bye) {
    from_exec = 0;
    if (dos_execing()) {                  /* EXEC: the prompt and the Return show, */
      cout(']');                          /* and at its end the keyboard's prompt follows */
      from_exec = (uint8_t)dos_exec_line(raw, &len);
      crout();
    }
    if (!from_exec) {
      poke0(PROMPT, ']' | 0x80);
      rom_call(GETLN);
      len = peek0(RES_X);
      if (len > 239) len = 239;           /* INLIN's limit */
      from0(raw, INBUF, len);
    }
    for (i = 0; i < len; i++) raw[i] &= 0x7F;
    raw[len] = 0;
    for (i = 0; i < len && raw[i] == ' '; i++) {}
    if (i == len) continue;
    if (compile_command()) continue;
    k = dos_command(raw, len, 0, 0);
    if (k == DOS_BYE) return;
    /* RESTART's CRDO after an AppleSoft line or a program (RUN path, CHAIN,
     * -) - to WRITE's file if on */
    if (k == DOS_NOT ? basic_line() : ran) {
      if (dos_writing()) dos_put_cr();
      else crout();
    }
    dos_prompt(ran);
    ran = 0;
  }
}

/* Ctrl-Reset (sys.s x_reset: the stack afresh, the shell called warm), as
 * BASIC.SYSTEM's: the program stays; its variables go (as CLEAR: the VM
 * clears on its next run or direct line) and CONT with them; a command half
 * gathered, READ/WRITE/EXEC end, the files stay open; the prompt, from
 * column 0 of the cursor's line (no CR: tests/dos/reset1.txt).  A reset
 * inside the compiler leaves its tables half made: compiled again. */
void shell_reset(void)
{
  pending = 0, bye = 0, ran = 0, from_exec = 0, run_file = 0;
  if (cc_busy) cc_busy = 0, compiled = 0;
  poke0((uint16_t)(vm_dp + VM_FRESH), 1);
  poke0((uint16_t)(vm_dp + VM_CONTIP), 0), poke0((uint16_t)(vm_dp + VM_CONTIP + 1), 0);
  poke0((uint16_t)(vm_dp + VM_CMDLEN), 0xFF);
  poke0((uint16_t)(vm_dp + VM_IOQUIET), 0);
  dos_reset();
  vm_hook();
  poke0(CH, 0);                         /* the prompt at the line's start, no CR */
  prompt_loop();
}

void shell(void)
{
  rom_call(HOME);
  prog_start = vm_read16(0x0E);
  hash_acc = 0;
  xfer_far = vm_base - 1, xfer_n = prog_start;
  far_hash();
  vm_print = hash_acc;
  new_program();
  vm_hook();
  print("APPLESOFT816 0.1");
  crout();
  prompt_loop();
}

#pragma clang attribute pop
