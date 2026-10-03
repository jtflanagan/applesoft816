/* shell.c - the command line: BASIC.SYSTEM's place (docs/M5.md).
 *
 * For now: RUN name (or -name) loads a tokenized program, compiles it -
 * timed, the number shown - and runs it; COMPILE name stops after the
 * compile; CAT/CATALOG; BYE.  Immediate
 * statements and the line editor come next.
 *
 * Everything reaches the machine through glue.s: the ROM's COUT/GETLN in
 * emulation mode, the MLI with its parameters in bank 0 (res.h), the VM.
 */
#include <stdint.h>
#include <string.h>

#include "res.h"
#include "shell.h"

/* the ROM */
#define COUT 0xFDED
#define GETLN 0xFD6A
#define HOME 0xFC58
#define CH 0x24
#define PROMPT 0x33
#define INBUF 0x0200

/* the MLI */
#define MLI_CREATE 0xC0
#define MLI_DESTROY 0xC1
#define MLI_WRITE 0xCB
#define MLI_GET_FILE_INFO 0xC4
#define MLI_GET_PREFIX 0xC7
#define MLI_OPEN 0xC8
#define MLI_READ 0xCA
#define MLI_CLOSE 0xCC
#define MLI_EOF 0x4C

/* the video counter: $C02E is the vertical count over two, $7D-$FF (262
 * lines a frame, 15734 a second) */
#define VERTCNT 0xC02E

static uint8_t line[256];
static uint8_t len;

/* the program: its pathname (length first), and what the cache checks */
static uint8_t path[65];
static uint16_t src_len, src_hash;
static uint16_t vm_print;           /* the VM image's hash: bytecode names its handlers */
static uint16_t prog_start;         /* where the VM image ends and the program begins */

/* ---- bank 0 ---- */
static void to0(uint16_t a, const void *src, uint16_t n)
{
  xfer_far = (uint32_t)a - 1, xfer_near = (uint16_t)((uint16_t)src - 1), xfer_n = n;
  far_out();
}
static void from0(void *dst, uint16_t a, uint16_t n)
{
  xfer_far = (uint32_t)a - 1, xfer_near = (uint16_t)((uint16_t)dst - 1), xfer_n = n;
  far_in();
}

/* ---- output, through the ROM's COUT ---- */
static void cout(uint8_t c)
{
  poke0(RES_A, (uint8_t)(c | 0x80));
  rom_call(COUT);
}
static void print(const char *s)
{
  while (*s) cout((uint8_t)*s++);
}
static void crout(void) { cout('\r'); }
static void print_u(uint32_t v)
{
  char b[11];
  int i = 10;
  b[i] = 0;
  do b[--i] = (char)('0' + v % 10); while ((v /= 10) != 0);
  print(b + i);
}
/* v/100 as d.dd */
static void print_centi(uint32_t v)
{
  print_u(v / 100);
  cout('.');
  cout((uint8_t)('0' + v / 10 % 10));
  cout((uint8_t)('0' + v % 10));
}

/* ---- the MLI ---- */
static uint8_t mli(uint8_t cmd, void *parms, uint8_t n)
{
  uint8_t e;
  to0(RES_PARMS, parms, n);
  e = mli_call(cmd);
  from0(parms, RES_PARMS, n);
  return e;
}

static void error(uint8_t e)
{
  crout();
  switch (e) {
  case 0x40: case 0x44: case 0x45: print("PATH NOT FOUND"); break;
  case 0x46: print("FILE NOT FOUND"); break;
  case 0x27: case 0x28: case 0x2F: print("I/O ERROR"); break;
  case 0x2B: print("WRITE PROTECTED"); break;
  case 0x4D: case 0x4C: print("RANGE ERROR"); break;
  case 0x4A: case 0x52: print("FILE TYPE MISMATCH"); break;
  case 0x80: print("SYNTAX ERROR"); break;
  case 0x81: print("PROGRAM TOO LARGE"); break;
  default: print("MLI ERROR $"); cout("0123456789ABCDEF"[e >> 4]); cout("0123456789ABCDEF"[e & 15]); break;
  }
  crout();
}

/* a pathname (length first) into RES_PATH */
static void put_path(const uint8_t *p)
{
  to0(RES_PATH, p, (uint16_t)(p[0] + 1));
}

/* the name at line[i] (to the end) into path and RES_PATH; 0 if there is none */
static int set_path(uint8_t i)
{
  uint8_t n = 0;
  while (i < len && line[i] == ' ') i++;
  while (i < len && n < 64 && line[i] != ' ' && line[i] != ',') path[1 + n++] = line[i++];
  if (!n) return 0;
  path[0] = n;
  put_path(path);
  return 1;
}

typedef struct { uint8_t n; uint16_t path, buf; uint8_t ref; } OpenParms;
typedef struct { uint8_t n, ref; uint16_t buf, req, trans; } ReadParms;
typedef struct { uint8_t n, ref; } CloseParms;
typedef struct { uint8_t n, ref; uint16_t buf, req, trans; } WriteParms;
typedef struct { uint8_t n; uint16_t path; } PathParms;
typedef struct {
  uint8_t n; uint16_t path; uint8_t access, type; uint16_t aux; uint8_t storage; uint16_t cdate, ctime;
} CreateParms;
typedef struct {
  uint8_t n; uint16_t path; uint8_t access, type; uint16_t aux; uint8_t storage;
  uint16_t blocks, mdate, mtime, cdate, ctime;
} InfoParms;

static uint8_t open_path(uint8_t *ref)
{
  OpenParms o = {3, RES_PATH, RES_FBUF, 0};
  uint8_t e = mli(MLI_OPEN, &o, sizeof o);
  *ref = o.ref;
  return e;
}
static void close_ref(uint8_t ref)
{
  CloseParms c = {1, ref};
  mli(MLI_CLOSE, &c, sizeof c);
}

/* ---- RUN ---- */

/* the program at RES_PATH into the text bank at $0801; 0 or an error */
static uint8_t load_program(void)
{
  InfoParms info;
  ReadParms r;
  uint8_t e, ref, z[2] = {0, 0};
  uint16_t at = 0x0801;
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
    if ((uint32_t)at + r.trans > 0xFFF0) { e = 0x81; break; }
    xfer_far = (uint32_t)RES_IOBUF - 1, xfer_far2 = (text_base | at) - 1, xfer_n = r.trans;
    far_move();
    at = (uint16_t)(at + r.trans);
  }
  close_ref(ref);
  if (e) return e;
  /* the end of the program: two zero bytes after it, whatever the file had */
  xfer_far = (text_base | at) - 1, xfer_near = (uint16_t)((uint16_t)z - 1), xfer_n = 2;
  far_out();
  if (info.aux != 0x0801) return 0x81;   /* TODO: relink a program saved elsewhere */
  src_len = (uint16_t)(at - 0x0801);
  hash_acc = 0;
  xfer_far = (text_base | 0x0801) - 1, xfer_n = src_len;
  if (src_len) far_hash();
  src_hash = hash_acc;
  return 0;
}

/* ---- the bytecode cache: DIR/AS816.CACHE/NAME beside DIR/NAME (ProDOS
 * names leave no room for a suffix), the VM bank's image of the compiled
 * program from prog_start to the stacks, after a header that says which
 * source and which VM it was made from ---- */
typedef struct {
  uint8_t magic[4];                 /* "A816" */
  uint8_t kind, version;            /* 'C', 1 */
  uint16_t vm_print, src_len, src_hash, start, len, lines, code;
} CacheHdr;

#define CACHE_DIR "AS816.CACHE"

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

static uint16_t vm_read16(uint16_t a)
{
  uint16_t v;
  xfer_far = (vm_base | a) - 1, xfer_near = (uint16_t)((uint16_t)&v - 1), xfer_n = 2;
  far_in();
  return v;
}

/* the compiled program from the cache, if it is this source's and this
 * VM's: 1 */
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
  if (memcmp(h.magic, "A816", 4) || h.kind != 'C' || h.version != 1 || h.vm_print != vm_print ||
      h.src_len != src_len || h.src_hash != src_hash || h.start != prog_start)
    goto no;
  at = h.start;
  r.req = RES_IOSIZE;
  for (;;) {
    r.trans = 0;
    e = mli(MLI_READ, &r, sizeof r);
    if (e == MLI_EOF || (!e && !r.trans)) break;
    if (e || r.trans > h.len - got) goto no;
    xfer_far = (uint32_t)RES_IOBUF - 1, xfer_far2 = (vm_base | at) - 1, xfer_n = r.trans;
    far_move();
    at = (uint16_t)(at + r.trans), got = (uint16_t)(got + r.trans);
  }
  close_ref(ref);
  if (got != h.len) return 0;
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
  WriteParms w;
  uint16_t end = vm_read16((uint16_t)(prog_start + 2)), at, n;   /* the header's fstack */
  memcpy(h.magic, "A816", 4);
  h.kind = 'C', h.version = 1, h.vm_print = vm_print, h.src_len = src_len, h.src_hash = src_hash;
  h.start = prog_start, h.len = (uint16_t)(end - prog_start), h.lines = st->lines, h.code = st->code_size;
  if (!cache_path(cp, 1)) return;
  put_path(cp);
  c.n = 7, c.path = RES_PATH, c.access = 0xC3, c.type = 0x0F, c.aux = 0, c.storage = 0x0D, c.cdate = c.ctime = 0;
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
  close_ref(ref);
}

/* ---- the clock: half-lines of the video counter, counted by the
 * compiler's poll (at every statement: well under a frame apart) ---- */
static uint32_t clk_half;
static uint8_t clk_last;
static void clk_poll(void *ctx)
{
  uint8_t v = peek0(VERTCNT), d = (uint8_t)(v - clk_last);
  (void)ctx;
  if (v < clk_last) d = (uint8_t)(d - 125);     /* past $FF: on from $7D */
  clk_half += d;
  clk_last = v;
}
static void clk_start(void)
{
  clk_half = 0;
  clk_last = peek0(VERTCNT);
}

/* load and compile the program at RES_PATH; 0, having said how long the
 * compile took */
static int compile_program(void)
{
  CompileStats st;
  uint8_t e;
  if ((e = load_program()) != 0) { error(e); return 1; }
  if (cache_load(&st)) {
    crout();
    print_u(st.lines);
    print(" LINES, ");
    print_u(st.code_size);
    print(" BYTES: CACHED");
    crout();
    return 0;
  }
  clk_start();
  e = (uint8_t)compile(&st, clk_poll);
  clk_poll(0);
  if (e) { error(0x81); return 1; }
  crout();
  print_u(st.lines);
  print(" LINES, ");
  print_u(st.code_size);
  print(" BYTES: ");
  print_centi(clk_half * 200 / 15734);       /* half-lines to hundredths */
  print(" S");
  crout();
  cache_save(&st);
  return 0;
}

static void run_program(void)
{
  if (!compile_program()) vm_call();
}

/* ---- CATALOG ---- */
static const char *type_name(uint8_t t)
{
  switch (t) {
  case 0x04: return "TXT";
  case 0x06: return "BIN";
  case 0x0F: return "DIR";
  case 0xFC: return "BAS";
  case 0xFF: return "SYS";
  case 0xFA: return "INT";
  case 0xFB: return "IVR";
  }
  return 0;
}

static void catalog(uint8_t at)
{
  typedef struct { uint8_t n; uint16_t buf; } PrefixParms;
  PrefixParms pp = {1, RES_PATH};
  ReadParms r;
  uint8_t e, ref, blk[512], first = 1;
  uint16_t i;
  if (!set_path(at) && (e = mli(MLI_GET_PREFIX, &pp, sizeof pp)) != 0) { error(e); return; }
  if ((e = open_path(&ref)) != 0) { error(e); return; }
  r.n = 4, r.ref = ref, r.buf = RES_IOBUF, r.req = 512;
  crout();
  for (;;) {
    if (mli(MLI_READ, &r, sizeof r) || r.trans != 512) break;
    from0(blk, RES_IOBUF, 512);
    for (i = 4; i + 0x27 <= 512; i += 0x27) {
      uint8_t st = blk[i] >> 4, n = blk[i] & 15, k;
      const char *tn;
      if (first) {                        /* the directory's header */
        first = 0;
        cout('/');
        for (k = 0; k < n; k++) cout(blk[i + 1 + k]);
        crout();
        crout();
        continue;
      }
      if (!st) continue;
      cout(' ');
      for (k = 0; k < 15; k++) cout(k < n ? blk[i + 1 + k] : ' ');
      cout(' ');
      if ((tn = type_name(blk[i + 0x10])) != 0) print(tn);
      else { cout('$'); cout("0123456789ABCDEF"[blk[i + 0x10] >> 4]); cout("0123456789ABCDEF"[blk[i + 0x10] & 15]); }
      cout(' ');
      print_u((uint16_t)(blk[i + 0x13] | blk[i + 0x14] << 8));
      crout();
    }
  }
  close_ref(ref);
}

/* ---- the command line ---- */
static int word(const char *w, uint8_t *after)
{
  uint8_t i = 0;
  while (w[i] && i < len && line[i] == (uint8_t)w[i]) i++;
  if (w[i]) return 0;
  *after = i;
  return 1;
}

void shell(void)
{
  uint8_t i;
  rom_call(HOME);
  prog_start = vm_read16(0x0E);
  hash_acc = 0;
  xfer_far = vm_base - 1, xfer_n = prog_start;
  far_hash();
  vm_print = hash_acc;
  print("APPLESOFT816 0.1");
  crout();
  for (;;) {
    if (peek0(CH)) crout();
    poke0(PROMPT, ']' | 0x80);
    rom_call(GETLN);
    len = peek0(RES_X);
    from0(line, INBUF, len);
    for (i = 0; i < len; i++) {
      uint8_t c = line[i] & 0x7F;
      line[i] = c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c;
    }
    if (!len) continue;
    if (word("BYE", &i)) return;
    if (word("CATALOG", &i) || word("CAT", &i)) { catalog(i); continue; }
    if (word("COMPILE", &i)) {
      if (!set_path(i)) { error(0x80); continue; }
      compile_program();
      continue;
    }
    if (word("-", &i) || word("RUN", &i)) {
      if (!set_path(i)) { error(0x80); continue; }
      run_program();
      continue;
    }
    error(0x80);
  }
}
