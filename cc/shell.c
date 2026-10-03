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

#include "parse.h"
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

/* the VM's direct page (vm/vm.s) and image header */
#define VM_DENTRY 0xE6
#define VM_FRESH 0xE8
#define VMH_DIRECT 0x18
#define VMH_CMDBUF 0x1A

static uint8_t raw[256];             /* the line typed, 7-bit, 0 after (INLIN's) */
static uint8_t line[256];            /* the same, upper case: the shell's commands */
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

/* AppleSoft's own: "?SYNTAX ERROR" and the bell */
static void as_error(const char *m)
{
  crout();
  cout('?');
  print(m);
  print(" ERROR");
  cout(7);
  crout();
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
  /* the end of the program: two zero bytes after it, whatever the file had;
   * the links made for here, wherever it was saved from (FIX.LINKS) */
  xfer_far = (text_base | at) - 1, xfer_near = (uint16_t)((uint16_t)z - 1), xfer_n = 2;
  far_out();
  prog_end = (uint16_t)(fix_links() + 2);
  compiled = 0, from_file = 1;
  src_len = (uint16_t)(prog_end - 0x0801);
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
#define CACHE_VERSION 2             /* 2: the compiler's tables after the image */

/* the compiler's tables to and from the open cache, through the I/O buffer */
static uint8_t cache_ref, cache_err;
static void cache_io(void *ctx, void *p, uint16_t n)
{
  ReadParms r;
  uint8_t *q = (uint8_t *)p;
  uint16_t k;
  int save = ctx != 0;
  while (n && !cache_err) {
    k = n < RES_IOSIZE ? n : RES_IOSIZE;
    r.n = 4, r.ref = cache_ref, r.buf = RES_IOBUF, r.req = k, r.trans = 0;
    if (save) {
      to0(RES_IOBUF, q, k);
      cache_err = mli(MLI_WRITE, &r, sizeof r);
    } else {
      cache_err = mli(MLI_READ, &r, sizeof r);
      if (!cache_err && r.trans != k) cache_err = 0x4C;
      if (!cache_err) from0(q, RES_IOBUF, k);
    }
    q += k, n = (uint16_t)(n - k);
  }
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
  cache_ref = ref, cache_err = 0;
  as816_state(0, cache_io, 0);
  close_ref(ref);
  if (cache_err) return 0;
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
  h.kind = 'C', h.version = CACHE_VERSION, h.vm_print = vm_print, h.src_len = src_len, h.src_hash = src_hash;
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
  cache_ref = ref, cache_err = 0;
  as816_state(1, cache_io, (void *)1);
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

/* the program in memory, compiled (from the cache when it is a file's and
 * the cache has it); 0, having said how long the compile took */
static int compile_current(void)
{
  CompileStats st;
  uint8_t e;
  if (compiled) return 0;
  if (from_file && cache_load(&st)) {
    crout();
    print_u(st.lines);
    print(" LINES, ");
    print_u(st.code_size);
    print(" BYTES: CACHED");
    crout();
    compiled = 1;
    poke0((uint16_t)(vm_dp + VM_FRESH), 1);
    return 0;
  }
  clk_start();
  e = (uint8_t)compile(&st, clk_poll);
  clk_poll(0);
  if (e) { as_error("OUT OF MEMORY"); return 1; }
  crout();
  print_u(st.lines);
  print(" LINES, ");
  print_u(st.code_size);
  print(" BYTES: ");
  print_centi(clk_half * 200 / 15734);       /* half-lines to hundredths */
  print(" S");
  crout();
  if (from_file) cache_save(&st);
  compiled = 1;
  poke0((uint16_t)(vm_dp + VM_FRESH), 1);
  return 0;
}

static uint8_t pending;               /* a CHR$(4) RUN (1) or LOAD (2) for after the VM */
static int word(const char *w, uint8_t *after);
static int is_digit(uint8_t c);

/* after the VM: what a program's CHR$(4) RUN or LOAD asked for */
static void after_vm(void)
{
  uint8_t what, e;
  while ((what = pending) != 0) {
    pending = 0;
    if ((e = load_program()) != 0) { error(e); return; }
    if (what == 1 && !compile_current()) vm_call(0);
  }
}

static void run_current(void)
{
  if (!compile_current()) vm_call(0);
  after_vm();
}

/* ---- the program in memory: lines in, out, listed ---- */
static void to_text(uint16_t a, const void *src, uint16_t n)
{
  xfer_far = (text_base | a) - 1, xfer_near = (uint16_t)((uint16_t)src - 1), xfer_n = n;
  far_out();
}
static void from_text(void *dst, uint16_t a, uint16_t n)
{
  xfer_far = (text_base | a) - 1, xfer_near = (uint16_t)((uint16_t)dst - 1), xfer_n = n;
  far_in();
}

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
static void edit_line(uint16_t num, const uint8_t *tok, uint8_t n)
{
  uint16_t at, link, size = (uint16_t)(4 + n);
  uint8_t h[4];
  if (find_line(num, &at, &link)) {
    xfer_far = (text_base | link) - 1, xfer_far2 = (text_base | at) - 1, xfer_n = (uint16_t)(prog_end - link);
    far_move_up();
    prog_end = (uint16_t)(prog_end - (link - at));
  }
  if (n > 1) {
    if ((uint32_t)prog_end + size > 0xFF00) { as_error("OUT OF MEMORY"); n = 0; }
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
}

/* LIST ($D6A5 on), as the GS ROM does it: CR, a space, the number, then
 * each token as " WORD "; past column 33 (73 in 80 columns), a new line
 * from column 5 */
static int list_wrap(void)
{
  if (peek0(0xC01F) & 0x80) return peek0(0x057B) >= 0x49;
  return peek0(CH) >= 0x21;
}
static void list_out(uint8_t c)
{
  cout(c);
  if (list_wrap()) { crout(); poke0(CH, 5); }
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
    num = (uint16_t)(l[2] | l[3] << 8);
    if (num > to) break;
    n = (uint16_t)(link - at - 4) < 250 ? (uint16_t)(link - at - 4) : 250;
    from_text(l + 4, (uint16_t)(at + 4), n);
    l[4 + n] = 0;
    crout();
    cout(' ');
    print_u(num);
    list_out(' ');
    for (i = 4; (c = l[i]) != 0; i++) {
      if (c < 0x80) { list_out(c); continue; }
      {
        uint16_t k = 0;
        uint8_t t = (uint8_t)(c - 0x80);
        while (t) { while (!(kw[k++] & 0x80)) {} t--; }
        cout(' ');
        while (!(kw[k] & 0x80)) cout(kw[k++]);
        cout((uint8_t)(kw[k] & 0x7F));
        list_out(' ');
      }
    }
    at = link;
  }
  crout();
}

/* SAVE: the program as a BAS file at RES_PATH */
static uint8_t save_program(void)
{
  CreateParms c;
  PathParms d;
  WriteParms w;
  uint8_t e, ref;
  uint16_t at, n;
  d.n = 1, d.path = RES_PATH;
  mli(MLI_DESTROY, &d, sizeof d);
  c.n = 7, c.path = RES_PATH, c.access = 0xC3, c.type = 0xFC, c.aux = 0x0801, c.storage = 0x01, c.cdate = c.ctime = 0;
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
  if (!e) {                           /* it is that file now */
    src_len = (uint16_t)(prog_end - 0x0801);
    hash_acc = 0;
    xfer_far = (text_base | 0x0801) - 1, xfer_n = src_len;
    far_hash();
    src_hash = hash_acc;
    from_file = 1;
  }
  return e;
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

/* the name after i, or 0 if what follows is not a name (RUN 100, RUN) */
static int name_at(uint8_t i)
{
  while (i < len && line[i] == ' ') i++;
  return i < len && ((line[i] >= 'A' && line[i] <= 'Z') || line[i] == '/');
}

/* BASIC.SYSTEM's commands: 1 if the line was one, 2 for BYE */
static int command(void)
{
  uint8_t i, e;
  if (word("BYE", &i)) return 2;
  if (word("CATALOG", &i) || word("CAT", &i)) { catalog(i); return 1; }
  if (word("COMPILE", &i)) {
    if (!set_path(i)) error(0x80);
    else if ((e = load_program()) != 0) error(e);
    else compile_current();
    return 1;
  }
  if (word("LOAD", &i)) {
    if (!set_path(i)) error(0x80);
    else if ((e = load_program()) != 0) error(e);
    return 1;
  }
  if (word("SAVE", &i)) {
    if (!set_path(i)) error(0x80);
    else if ((e = save_program()) != 0) error(e);
    return 1;
  }
  if (word("-", &i) || (word("RUN", &i) && name_at(i))) {
    if (!set_path(i)) error(0x80);
    else if ((e = load_program()) != 0) error(e);
    else run_current();
    return 1;
  }
  return 0;
}

static int is_digit(uint8_t c) { return c >= '0' && c <= '9'; }

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

/* a direct line (its tokens in raw): compiled against the program - which
 * is compiled first, if it has to be - and run with its variables */
static void direct_line(void)
{
  uint16_t entry;
  if (compile_current()) return;
  if (compile_direct(raw, &entry)) { as_error("OUT OF MEMORY"); return; }
  poke0((uint16_t)(vm_dp + VM_DENTRY), (uint8_t)entry);
  poke0((uint16_t)(vm_dp + VM_DENTRY + 1), (uint8_t)(entry >> 8));
  vm_call(vm_read16(VMH_DIRECT));
  after_vm();
}

/* ---- CHR$(4) commands from a running program: vm_command, called by the
 * VM (glue.s cmd_entry) with the command in its buffer; 0 to go on, else
 * the program stops (and a RUN or LOAD is pending) ---- */
uint8_t cmd_answer;

static void port(uint16_t rom, uint8_t at)
{
  uint8_t n = 0;
  while (at < len && line[at] == ' ') at++;
  while (at < len && is_digit(line[at])) n = (uint8_t)(n * 10 + (line[at++] - '0'));
  poke0(RES_A, n);
  rom_call(rom);
}

uint8_t vm_command(void)
{
  uint8_t n, i, k;
  uint16_t b = vm_read16(VMH_CMDBUF);
  xfer_far = (vm_base | b) - 1, xfer_near = (uint16_t)((uint16_t)&n - 1), xfer_n = 1;
  far_in();
  xfer_far = (vm_base | (uint16_t)(b + 1)) - 1, xfer_near = (uint16_t)((uint16_t)raw - 1), xfer_n = n;
  if (n) far_in();
  for (k = 0; k < n && raw[k] == ' '; k++) {}
  len = 0;
  for (i = k; i < n; i++) {
    uint8_t c = raw[i] & 0x7F;
    line[len++] = c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c;
  }
  line[len] = 0;
  if (!len) return 0;
  if (word("CATALOG", &i) || word("CAT", &i)) { catalog(i); return 0; }
  if (word("PR#", &i)) { port(0xFE95, i); return 0; }      /* OUTPORT */
  if (word("IN#", &i)) { port(0xFE8B, i); return 0; }      /* INPORT */
  if (word("NOMON", &i) || word("MON", &i)) return 0;
  if (word("-", &i) || word("RUN", &i)) {
    if (!set_path(i)) { error(0x80); return 1; }
    pending = 1;
    return 1;
  }
  if (word("LOAD", &i)) {
    if (!set_path(i)) { error(0x80); return 1; }
    pending = 2;
    return 1;
  }
  error(0x80);
  return 1;
}

/* an AppleSoft line: numbered, into the program; else at once */
static void basic_line(void)
{
  uint8_t x = 0, y, p;
  uint16_t num = 0;
  int32_t a, b;
  while (raw[x] == ' ') x++;
  if (is_digit(raw[x])) {
    while (is_digit(raw[x]) || raw[x] == ' ') {
      if (raw[x] != ' ') {
        if (num >= 6400) { as_error("SYNTAX"); return; }
        num = (uint16_t)(num * 10 + (raw[x] - '0'));
      }
      x++;
    }
    y = as816_parse(raw, x);
    edit_line(num, raw, (uint8_t)(y - 4));
    return;
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
    if (raw[p]) break;
    list((uint16_t)a, b ? (uint16_t)b : 0xFFFF);
    return;
  case 0xBF:                            /* NEW */
    if (raw[1]) break;
    new_program();
    return;
  case 0xAC:                            /* RUN (RUN n: a direct line) */
    if (raw[1]) { direct_line(); return; }
    run_current();
    return;
  case 0:
    return;
  default:
    direct_line();
    return;
  }
  as_error("SYNTAX");
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
  new_program();
  vm_hook();
  print("APPLESOFT816 0.1");
  crout();
  for (;;) {
    if (peek0(CH)) crout();
    poke0(PROMPT, ']' | 0x80);
    rom_call(GETLN);
    len = peek0(RES_X);
    if (len > 239) len = 239;           /* INLIN's limit */
    from0(raw, INBUF, len);
    for (i = 0; i < len; i++) {
      uint8_t c = raw[i] & 0x7F;
      raw[i] = c;
      line[i] = c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c;
    }
    raw[len] = line[len] = 0;
    if (!len) continue;
    i = (uint8_t)command();
    if (i == 2) return;
    if (!i) basic_line();
  }
}
