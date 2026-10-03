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
#include <string.h>

#include "dos.h"
#include "machine.h"
#include "parse.h"

/* the video counter: $C02E is the vertical count over two, $7D-$FF (262
 * lines a frame, 15734 a second) */
#define VERTCNT 0xC02E

/* the VM's direct page (vm/vm.s) and image header */
#define VM_DENTRY 0xE6
#define VM_FRESH 0xE8
#define VM_ONERR 0xE4
#define VM_CBREQ 0x12
#define VM_CURLIN 0x26
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
} CacheHdr;

#define CACHE_DIR "AS816.CACHE"
#define CACHE_VERSION 3             /* 2: the compiler's tables after the image; 3: salist_cap */

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
  ReadParms w;
  uint16_t end = vm_read16((uint16_t)(prog_start + 2)), at, n;   /* the header's fstack */
  memcpy(h.magic, "A816", 4);
  h.kind = 'C', h.version = CACHE_VERSION, h.vm_print = vm_print, h.src_len = src_len, h.src_hash = src_hash;
  h.start = prog_start, h.len = (uint16_t)(end - prog_start), h.lines = st->lines, h.code = st->code_size;
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
  clk_start();
  e = (uint8_t)compile(&st, clk_poll);
  clk_poll(0);
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
 * 2 LOAD, 3 BYE */
static uint8_t pending, pending_path[65];
static int32_t pending_line;
static int bye;
static uint8_t ran;                  /* a program ran: FILE(S) STILL OPEN is due */

void prog_pending(uint8_t what, const uint8_t *p, int32_t line)
{
  pending = what, pending_line = line;
  if (p) memcpy(pending_path, p, (size_t)p[0] + 1);
}

static void direct_tokens(void);

/* RUN [n] of the program in memory */
static void run_from(int32_t line)
{
  say_time = timing;
  ran = 1;
  crout();                                /* (BASIC.SYSTEM's RUN starts a line) */
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

static void after_vm(void)
{
  uint8_t what, e;
  while ((what = pending) != 0) {
    pending = 0;
    if (what == 3) { bye = 1; return; }
    if ((e = prog_load(pending_path)) != 0) { dos_message(dos_error(e)); return; }
    if (what == 1) run_from(pending_line);
  }
}

void prog_run(int32_t line)
{
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
    if (raw[p]) break;
    list((uint16_t)a, b ? (uint16_t)b : 0xFFFF);
    return 1;
  case 0xBF:                            /* NEW */
    if (raw[1]) break;
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
    direct_tokens();
    after_vm();
    return 1;
  }
  as_error("SYNTAX");
  return 1;
}

void shell(void)
{
  uint8_t i, k;
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
  while (!bye) {
    poke0(PROMPT, ']' | 0x80);
    rom_call(GETLN);
    len = peek0(RES_X);
    if (len > 239) len = 239;           /* INLIN's limit */
    from0(raw, INBUF, len);
    for (i = 0; i < len; i++) raw[i] &= 0x7F;
    raw[len] = 0;
    for (i = 0; i < len && raw[i] == ' '; i++) {}
    if (i == len) continue;
    if (compile_command()) continue;
    k = dos_command(raw, len, 0, 0);
    if (k == DOS_BYE) return;
    if (k == DOS_NOT && basic_line()) {   /* RESTART's CRDO: to WRITE's file if on */
      if (dos_writing()) dos_put_cr();
      else crout();
    }
    dos_prompt(ran);
    ran = 0;
  }
}
