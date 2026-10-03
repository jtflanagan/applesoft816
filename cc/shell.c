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

/* the name at line[i] (to the end) into RES_PATH; 0 if there is none */
static int set_path(uint8_t i)
{
  uint8_t n = 0, path[65];
  while (i < len && line[i] == ' ') i++;
  while (i < len && n < 64 && line[i] != ' ' && line[i] != ',') path[1 + n++] = line[i++];
  if (!n) return 0;
  path[0] = n;
  to0(RES_PATH, path, (uint16_t)(n + 1));
  return 1;
}

typedef struct { uint8_t n; uint16_t path, buf; uint8_t ref; } OpenParms;
typedef struct { uint8_t n, ref; uint16_t buf, req, trans; } ReadParms;
typedef struct { uint8_t n, ref; } CloseParms;
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
  return 0;
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
