/* compile.c - tokenized AppleSoft to applesoft816 bytecode (docs/INTERP.md).
 *
 * Two passes over the program: the first lays the image out (code, then the
 * constants, string literals, variables and array descriptors it found),
 * the second writes it with every jump resolved.  Both make the same
 * decisions, so the sizes agree.
 *
 * Bit-exactness lives in expr(): it is FRMEVL's own precedence algorithm
 * (the ROM's MATHTBL precedences, left to right, pending operators of equal
 * or higher precedence performed first), emitting a PUSH wherever the ROM
 * rounds and pushes FAC and an operation wherever it performs one.
 *
 * Errors are AppleSoft's: a syntax error is compiled as an ERR operation
 * where the ROM would have found it, because AppleSoft only reports it when
 * the line runs.
 */
#include "compile.h"

#include <string.h>

/* the big tables are filled before they are read: the 65816 build's crt0
 * need not clear them */
#ifdef __mos__
#define NOINIT __attribute__((section(".noinit.compile")))
#else
#define NOINIT
#endif

/* ---- tokens ---- */
enum {
  T_END = 0x80, T_FOR = 0x81, T_NEXT = 0x82, T_DATA = 0x83, T_INPUT = 0x84, T_DIM = 0x86, T_READ = 0x87,
  T_RESTORE = 0xAE, T_GET = 0xBE, T_LET = 0xAA, T_GOTO = 0xAB,
  T_GR = 0x88, T_TEXT = 0x89, T_PRN = 0x8A, T_INN = 0x8B, T_CALL = 0x8C, T_PLOT = 0x8D, T_HLIN = 0x8E,
  T_VLIN = 0x8F, T_HTAB = 0x96, T_HOME = 0x97, T_TRACE = 0x9B, T_NOTRACE = 0x9C, T_NORMAL = 0x9D,
  T_INVERSE = 0x9E, T_FLASH = 0x9F, T_COLOR = 0xA0, T_POP = 0xA1, T_VTAB = 0xA2, T_HIMEM = 0xA3,
  T_LOMEM = 0xA4, T_ONERR = 0xA5, T_RESUME = 0xA6, T_SPEED = 0xA9, T_RUN = 0xAC, T_STOP = 0xB3,
  T_ON = 0xB4, T_WAIT = 0xB5, T_CONT = 0xBB,
  T_HGR2 = 0x90, T_HGR = 0x91, T_HCOLOR = 0x92, T_HPLOT = 0x93, T_DRAW = 0x94, T_XDRAW = 0x95,
  T_ROT = 0x98, T_SCALE = 0x99, T_DEF = 0xB8, T_POKE = 0xB9, T_CLEAR = 0xBD, T_TABP = 0xC0, T_FN = 0xC2,
  T_SPCP = 0xC3, T_AT = 0xC5, T_SCRN = 0xD7, T_POS = 0xD9, T_PEEK = 0xE2, T_USR = 0xD5, T_PDL = 0xD8,
  T_LIST = 0xBC, T_NEW = 0xBF, T_DEL = 0x85,
  T_SHLOAD = 0x9A, T_RECALL = 0xA7, T_STORE = 0xA8, T_AMPER = 0xAF, T_SAVE = 0xB6, T_LOAD = 0xB7,
  T_IF = 0xAD, T_GOSUB = 0xB0, T_RETURN = 0xB1, T_REM = 0xB2, T_PRINT = 0xBA,
  T_TO = 0xC1, T_THEN = 0xC4, T_NOT = 0xC6, T_STEP = 0xC7, T_PLUS = 0xC8, T_MINUS = 0xC9,
  T_MUL = 0xCA, T_DIV = 0xCB, T_POW = 0xCC, T_AND = 0xCD, T_OR = 0xCE,
  T_GT = 0xCF, T_EQ = 0xD0, T_LT = 0xD1, T_SGN = 0xD2, T_INT = 0xD3, T_ABS = 0xD4,
  T_FRE = 0xD6, T_SQR = 0xDA, T_RND = 0xDB, T_LOG = 0xDC, T_EXP = 0xDD, T_COS = 0xDE, T_SIN = 0xDF,
  T_TAN = 0xE0, T_ATN = 0xE1, T_LEN = 0xE3, T_STR = 0xE4, T_VAL = 0xE5, T_ASC = 0xE6, T_CHR = 0xE7,
  T_LEFT = 0xE8, T_RIGHT = 0xE9, T_MID = 0xEA
};

/* ---- limits ---- */
#define MAX_LINES 4000
#define MAX_VARS 400
#define MAX_ARRAYS 100
#define MAX_CONSTS 600
#define MAX_PENDING 64
#define FSTACK_SIZE 600
#define SSTACK_SIZE 256
#define CSTACK_SIZE 4096
#define HEADER_SIZE 44
#define SV_SPARE 16                  /* string roots kept free for direct lines' variables */
#define SA_SPARE 8                   /* ... and for their string arrays */
#define DESC_SPARE 96                /* descriptor bytes kept free for direct lines' arrays */
/* the direct line's area, at the top of the VM bank (the array heap ends
 * below it): an END, the line's code, its constants and strings; the
 * variables direct lines make, from the top down */
#define DIRECT_BASE 0xF400u
#define DIRECT_DATA 0xFA00u
#define DIRECT_VARS 0xFC00u          /* the lowest a direct line's variable goes */
#define MAX_FNS 64
#define MAX_LITS 3000                /* numeric literals FIN converts in pass 1 */
#define CONST_HASH 128

typedef struct { uint16_t num, addr; } Line;
typedef struct { uint8_t c1, c2, type; } Var;              /* type: 0 real, 1 integer, 2 string */
typedef struct { uint8_t c1, c2, type, ndims; } Array;

static const Platform *pf;
static int pass;
static uint16_t pc;                 /* the emit position (an address in the VM bank) */
static int overflow;                /* the image does not fit */

NOINIT static Line lines[MAX_LINES + 1];      /* + the END after the last line */
static uint16_t nlines, line_i;     /* line_i: the line being compiled */
NOINIT static Var vars[MAX_VARS];
static uint16_t nvars;
NOINIT static Array arrays[MAX_ARRAYS];
static uint16_t narrays;
/* the constant pool: made in pass 1, kept (same order) for pass 2 */
NOINIT static uint8_t consts[MAX_CONSTS][7];
static uint16_t nconsts;
NOINIT static uint16_t const_head[CONST_HASH], const_next[MAX_CONSTS];   /* index + 1, 0 = none */
/* pass 1's literals - where in the text, what FIN made of them - so pass 2
 * neither runs FIN nor looks the constant up again */
typedef struct { uint16_t at, slot; uint8_t used; } Lit;
NOINIT static Lit lits[MAX_LITS];
static uint16_t nlits, lit_i;
static uint16_t line_at;            /* the text address of buf[0] */
static uint16_t str_bytes;          /* string literal pool size, this pass */

/* the layout, fixed after pass 1 */
static Var fns[MAX_FNS];                          /* DEF FN names */
static uint16_t fn_addr[MAX_FNS];                 /* a direct line's FN entries */
static uint16_t nfns, fn_base, desc_copy_base, desc_bytes;
static int use_onerr;                             /* STMT marks needed */
static uint16_t code_base, const_base, str_base, var_base, desc_base, svlist_base, salist_base, fstack_base,
    sstack_base, cstack_base, heap_base;
static uint16_t nsvars, nsarrays, svlist_cap, salist_cap;
static uint16_t stmt_base, nstmts;  /* where each statement starts (CONT after an error) */
NOINIT static uint16_t var_off[MAX_VARS];
static uint16_t desc_off[MAX_ARRAYS];
static uint16_t str_at;             /* pass 2: the next literal's address */
/* direct lines */
static int direct;                  /* compiling one */
static uint16_t dvar_at;            /* the lowest of their variables (0: none yet) */

/* the line being compiled */
static uint8_t buf[256];
static uint8_t p, blen;             /* bytes: buf holds at most 255 characters and its 0 */

/* ---- writing the VM bank: runs of consecutive bytes, one write each ---- */
static uint8_t wbuf[128];
static uint8_t wlen;
static uint16_t wbase, wnext;       /* wnext: the address that continues the run */
static void flush(void)
{
  if (wlen) pf->write(pf->ctx, wbase, wbuf, wlen);
  wlen = 0;
}
static void out(uint16_t a, uint8_t v)
{
  if (!wlen || a != wnext || wlen == sizeof wbuf) {
    flush();
    wbase = a;
  }
  wbuf[wlen++] = v;
  wnext = (uint16_t)(a + 1);
}

/* ---- emitting: pass 2 gathers the code in cbuf, written 256 bytes at a
 * time; cbuf[0] is the byte at pc - clen ---- */
static uint8_t cbuf[256];
static uint8_t clen;
static void flush_code(uint16_t n)
{
  if (n) pf->write(pf->ctx, (uint16_t)(pc - n), cbuf, n);
  clen = 0;
}
static void emit8(uint8_t v)
{
  if (++pc == 0) overflow = 1;
  if (pass == 2) {
    cbuf[clen++] = v;
    if (!clen) flush_code(256);
  }
}
/* a byte of code already emitted */
static void patch8(uint16_t a, uint8_t v)
{
  uint16_t back = (uint16_t)(pc - a);     /* 1 for the last byte */
  if (back <= clen) cbuf[clen - back] = v;
  else out(a, v);
}
static void emit16(uint16_t v) { emit8((uint8_t)v); emit8((uint8_t)(v >> 8)); }
static void op(int o) { emit16(pf->optable[o]); }
static void op16(int o, uint16_t v) { op(o); emit16(v); }

/* ---- scanning: CHRGET skips spaces ---- */
static uint8_t peek(void)
{
  while (p < blen && buf[p] == ' ') p++;
  return p < blen ? buf[p] : 0;
}
static uint8_t next(void) { uint8_t c = peek(); if (p < blen) p++; return c; }
static int at_end(void) { uint8_t c = peek(); return c == 0 || c == ':'; }
static int is_digit(uint8_t c) { return c >= '0' && c <= '9'; }
static int is_letter(uint8_t c) { return c >= 'A' && c <= 'Z'; }

/* skip to the end of the statement (outside string literals) */
static void skip_statement(void)
{
  int q = 0;
  while (p < blen && buf[p] && (q || buf[p] != ':')) {
    if (buf[p] == '"') q = !q;
    p++;
  }
}

/* compile "stop with error e here" and give up on the rest of the line:
 * nothing after it in the line can run */
static void error_here(uint8_t e)
{
  op16(OP_ERR, e);
  p = blen;
}

/* ---- symbols ---- */

/* a direct line's new array: its descriptor (and the pristine copy CLEAR
 * restores) in the room kept after the program's, the header's count of
 * descriptor bytes (+$20) grown; a string array joins the roots (+$14) */
static void direct_array(uint16_t i)
{
  uint8_t size = (uint8_t)(4 + 2 * arrays[i].ndims), k;
  uint8_t el = arrays[i].type == 1 ? 2 : arrays[i].type == 2 ? 4 : 5;
  uint16_t a = (uint16_t)(desc_base + desc_bytes), c = (uint16_t)(desc_copy_base + desc_bytes);
  if ((uint16_t)(desc_bytes + size) > (uint16_t)(desc_copy_base - desc_base) ||
      (arrays[i].type == 2 && nsarrays >= salist_cap)) {
    overflow = 1;
    return;
  }
  desc_off[i] = a;
  for (k = 0; k < size; k++) {
    uint8_t v = k == 2 ? arrays[i].ndims : k == 3 ? el : 0;
    out((uint16_t)(a + k), v), out((uint16_t)(c + k), v);
  }
  desc_bytes = (uint16_t)(desc_bytes + size);
  out((uint16_t)(pf->prog_start + 0x20), (uint8_t)desc_bytes), out((uint16_t)(pf->prog_start + 0x21), (uint8_t)(desc_bytes >> 8));
  if (arrays[i].type == 2) {
    uint16_t r = (uint16_t)(salist_base + 2 * nsarrays);
    out(r, (uint8_t)a), out((uint16_t)(r + 1), (uint8_t)(a >> 8));
    nsarrays++;
    out((uint16_t)(pf->prog_start + 0x14), (uint8_t)nsarrays), out((uint16_t)(pf->prog_start + 0x15), (uint8_t)(nsarrays >> 8));
  }
}

/* a direct line's new variable (or FN entry): size zeroed bytes from the top
 * down; a string's address joins the collector's roots (SV_SPARE kept free).
 * The header's +$22 says where they start, for CLEAR */
static uint16_t direct_var(uint8_t size, int is_string)
{
  uint16_t a = (uint16_t)((dvar_at ? dvar_at : 0) - size), k;
  if (a < DIRECT_VARS || (is_string && nsvars >= svlist_cap)) { overflow = 1; return DIRECT_VARS; }
  for (k = 0; k < size; k++) out((uint16_t)(a + k), 0);
  dvar_at = a;
  out((uint16_t)(pf->prog_start + 0x22), (uint8_t)a), out((uint16_t)(pf->prog_start + 0x23), (uint8_t)(a >> 8));
  if (is_string) {
    uint16_t r = (uint16_t)(svlist_base + 2 * nsvars);
    out(r, (uint8_t)a), out((uint16_t)(r + 1), (uint8_t)(a >> 8));
    nsvars++;
    out((uint16_t)(pf->prog_start + 0x10), (uint8_t)nsvars), out((uint16_t)(pf->prog_start + 0x11), (uint8_t)(nsvars >> 8));
  }
  return a;
}
static uint16_t var_slot(uint8_t c1, uint8_t c2, uint8_t type)
{
  uint16_t i;
  for (i = 0; i < nvars; i++)
    if (vars[i].c1 == c1 && vars[i].c2 == c2 && vars[i].type == type) break;
  if (i == nvars) {
    if (nvars == MAX_VARS) { overflow = 1; return 0; }
    vars[nvars].c1 = c1, vars[nvars].c2 = c2, vars[nvars].type = type;
    nvars++;
    if (direct) var_off[i] = direct_var(type == 1 ? 2 : type == 2 ? 4 : 5, type == 2);
  }
  return pass == 2 ? var_off[i] : 0;
}

/* the array's index (its descriptor), or -1 if used with another number of
 * subscripts than it first had */
static int array_slot(uint8_t c1, uint8_t c2, uint8_t type, uint8_t ndims)
{
  uint16_t i;
  for (i = 0; i < narrays; i++)
    if (arrays[i].c1 == c1 && arrays[i].c2 == c2 && arrays[i].type == type) break;
  if (i == narrays) {
    if (narrays == MAX_ARRAYS) { overflow = 1; return 0; }
    arrays[narrays].c1 = c1, arrays[narrays].c2 = c2, arrays[narrays].type = type;
    arrays[narrays].ndims = ndims;
    narrays++;
    if (direct) direct_array(i);
  }
  return arrays[i].ndims == ndims ? (int)i : -1;
}

static uint8_t const_hash(const uint8_t fac[7])
{
  return (uint8_t)((fac[0] + fac[1] + fac[2] + fac[3] + fac[4] + fac[5] + fac[6]) & (CONST_HASH - 1));
}
/* the constant's index in the pool + 1, or 0 */
static uint16_t const_find(const uint8_t fac[7])
{
  uint16_t i;
  uint8_t k;
  for (i = const_head[const_hash(fac)]; i; i = const_next[i - 1]) {
    const uint8_t *c = consts[i - 1];
    for (k = 0; k < 7 && c[k] == fac[k]; k++) {}
    if (k == 7) return i;
  }
  return 0;
}
/* the constant's index in the pool, added if new */
static uint16_t const_index(const uint8_t fac[7])
{
  uint8_t h = const_hash(fac);
  uint16_t i = const_find(fac);
  if (i) return (uint16_t)(i - 1);
  if (nconsts == MAX_CONSTS) { overflow = 1; return 0; }
  memcpy(consts[nconsts], fac, 7);
  const_next[nconsts] = const_head[h];
  const_head[h] = ++nconsts;
  return (uint16_t)(nconsts - 1);
}
static uint16_t const_slot(const uint8_t fac[7])
{
  uint16_t i, k;
  if (!direct) return (uint16_t)(const_base + 7 * const_index(fac));
  if ((i = const_find(fac)) != 0) return (uint16_t)(const_base + 7 * (i - 1));
  if ((uint16_t)(str_at + 7) > DIRECT_VARS) { overflow = 1; return DIRECT_DATA; }
  for (k = 0; k < 7; k++) out((uint16_t)(str_at + k), fac[k]);
  str_at = (uint16_t)(str_at + 7);
  return (uint16_t)(str_at - 7);
}

/* a numeric literal at buf[p]: its constant's slot, and p past it; 0 for
 * FIN's ?OVERFLOW (p not moved) */
static int literal(uint16_t *slot)
{
  uint16_t at = (uint16_t)(line_at + p);
  uint8_t fac[7];
  uint16_t i;
  int used;
  if (pass == 2 && !direct) {       /* pass 1 saw it (parsing is the same in both passes) */
    while (lit_i < nlits && lits[lit_i].at < at) lit_i++;
    if (lit_i < nlits && lits[lit_i].at == at) {
      *slot = (uint16_t)(const_base + 7 * lits[lit_i].slot);
      p += lits[lit_i].used;
      return 1;
    }
  }
  used = pf->fin(pf->ctx, buf + p, blen - p, fac);
  if (used <= 0) return 0;
  if (direct) {
    *slot = const_slot(fac);
    p += used;
    return 1;
  }
  i = const_index(fac);
  *slot = (uint16_t)(const_base + 7 * i);
  if (pass == 1 && nlits < MAX_LITS) {
    lits[nlits].at = at, lits[nlits].slot = i, lits[nlits].used = (uint8_t)used;
    nlits++;
  }
  p += used;
  return 1;
}

/* a DEF FN's table entry (4 bytes, among the variables: CLEAR forgets it) */
static uint16_t fn_slot(uint8_t c1, uint8_t c2)
{
  uint16_t i;
  for (i = 0; i < nfns; i++)
    if (fns[i].c1 == c1 && fns[i].c2 == c2) break;
  if (i == nfns) {
    if (nfns == MAX_FNS) { overflow = 1; return 0; }
    fns[nfns].c1 = c1, fns[nfns].c2 = c2, fns[nfns].type = 0;
    nfns++;
    if (direct) { fns[i].type = 1; fn_addr[i] = direct_var(4, 0); }   /* (type 1: fn_addr has it) */
  }
  if (pass != 2) return 0;
  return fns[i].type ? fn_addr[i] : (uint16_t)(fn_base + 4 * i);
}

/* a string literal: length byte, then the text */
static uint16_t str_literal(const uint8_t *s, uint8_t n)
{
  uint16_t at = str_at, i;
  if (pass == 2) {
    out(str_at, n);
    for (i = 0; i < n; i++) out((uint16_t)(str_at + 1 + i), s[i]);
    str_at = (uint16_t)(str_at + 1 + n);
  }
  str_bytes = (uint16_t)(str_bytes + 1 + n);
  return at;
}

/* the address of line n's code; 0 if there is no such line */
static uint16_t line_addr(uint16_t n)
{
  uint16_t lo = 0, hi = nlines;
  while (lo < hi) {
    uint16_t mid = (uint16_t)((lo + hi) / 2);
    if (lines[mid].num < n) lo = (uint16_t)(mid + 1);
    else hi = mid;
  }
  return lo < nlines && lines[lo].num == n ? lines[lo].addr : 0;
}

/* where control goes after this line: the next line, or the final END */
static uint16_t next_line_addr(void)
{
  if (direct) return DIRECT_BASE;
  return line_i + 1 < nlines ? lines[line_i + 1].addr : lines[nlines].addr;
}

/* ---- names: PTRGET's reading (first two characters count) ---- */
typedef struct { uint8_t c1, c2, type; int is_array; } Name;

static int read_name(Name *n)
{
  uint8_t c = peek();
  if (!is_letter(c)) return 0;
  p++;
  n->c1 = c, n->c2 = 0, n->type = 0;
  c = peek();                             /* PTRGET4 reads on with CHRGET: spaces skipped */
  if (is_letter(c) || is_digit(c)) {
    n->c2 = c;
    p++;
    while (c = peek(), is_letter(c) || is_digit(c)) p++;
  }
  c = peek();
  if (c == '$') n->type = 2, p++;
  else if (c == '%') n->type = 1, p++;
  n->is_array = peek() == '(';
  return 1;
}

/* ---- line numbers: LINGET ---- */
static int read_linenum(uint16_t *out)
{
  uint32_t v = 0;
  uint8_t c = peek();
  if (!is_digit(c)) return 0;
  while (c = peek(), is_digit(c)) {
    v = v * 10 + (uint32_t)(c - '0');
    if (v > 63999) return -1;            /* LINGET: ?SYNTAX ERROR past 63999 */
    p++;
  }
  *out = (uint16_t)v;
  return 1;
}

/* ---- expressions: FRMEVL ---- */
enum { TY_NUM, TY_STR, TY_ERR };

typedef struct { uint8_t prec, opcode, mask, str; } Pending;

static int expr(void);

/* MATHTBL ($D0B2) precedences */
#define P_ADD 0x79
#define P_MUL 0x7B
#define P_POW 0x7D
#define P_AND 0x50
#define P_OR 0x46
#define P_UNARY 0x7F
#define P_REL 0x64

/* perform a pending operator, its right operand of type t now in FAC/SACC;
 * returns the result's type.  The ROM's checks, at the same point:
 * FRM.PERFORM.1's CHKNUM for arithmetic, RELOPS's CHKVAL for relations. */
static int perform(const Pending *x, int t)
{
  if (x->opcode == OP_REL) {
    if (t != (x->str ? TY_STR : TY_NUM)) { error_here(E_TYPE_MISMATCH); return TY_ERR; }
    op(x->str ? OP_SREL : OP_REL);
    emit8(x->mask);
    return TY_NUM;
  }
  if (t != TY_NUM) { error_here(E_TYPE_MISMATCH); return TY_ERR; }
  op(x->opcode);
  return TY_NUM;
}

/* subscripts "( e [, e]... )" as IDX pushes; returns how many, 0 on error */
static int subscripts(void)
{
  int n = 0;
  next();                                 /* '(' */
  for (;;) {
    if (expr() != TY_NUM) return 0;
    op(OP_IDX);
    n++;
    if (peek() == ',') { p++; continue; }
    if (peek() == ')') { p++; return n; }
    return 0;
  }
}

/* a variable or array element by name: emits its load; returns its type */
static int load_name(const Name *nm)
{
  if (nm->is_array) {
    int n = subscripts(), a;
    if (!n) { error_here(E_SYNTAX); return TY_ERR; }
    a = array_slot(nm->c1, nm->c2, nm->type, (uint8_t)n);
    if (a < 0) { error_here(E_BAD_SUBSCRIPT); return TY_ERR; }
    op16(nm->type == 2 ? OP_SLDA : nm->type == 1 ? OP_LDAI : OP_LDA, pass == 2 ? desc_off[a] : 0);
  } else
    op16(nm->type == 2 ? OP_SLDV : nm->type == 1 ? OP_LDVI : OP_LDV, var_slot(nm->c1, nm->c2, nm->type));
  return nm->type == 2 ? TY_STR : TY_NUM;
}

/* "( e )" of the type wanted; 0 on error (already compiled) */
static int paren_arg(int want)
{
  int t;
  if (peek() != '(') { error_here(E_SYNTAX); return 0; }
  p++;
  t = expr();
  if (t == TY_ERR) return 0;
  if (t != want) { error_here(E_TYPE_MISMATCH); return 0; }
  if (peek() != ')') { error_here(E_SYNTAX); return 0; }
  p++;
  return 1;
}

/* the string functions with arguments: LEFT$ RIGHT$ MID$ (UNARY's string
 * path: the string, then GETBYT on each numeric argument in order) */
static int string_fn(uint8_t tok)
{
  int t;
  if (peek() != '(') { error_here(E_SYNTAX); return TY_ERR; }
  p++;
  t = expr();
  if (t == TY_ERR) return t;
  if (t != TY_STR) { error_here(E_TYPE_MISMATCH); return TY_ERR; }
  if (peek() != ',') { error_here(E_SYNTAX); return TY_ERR; }
  p++;
  op(OP_SPUSH);
  if ((t = expr()) != TY_NUM) { if (t != TY_ERR) error_here(E_TYPE_MISMATCH); return TY_ERR; }
  if (tok == T_MID) {
    op(OP_MIDP);
    if (peek() == ',') {
      p++;
      if ((t = expr()) != TY_NUM) { if (t != TY_ERR) error_here(E_TYPE_MISMATCH); return TY_ERR; }
      if (peek() != ')') { error_here(E_SYNTAX); return TY_ERR; }
      p++;
      op(OP_MID3);
      return TY_STR;
    }
    if (peek() != ')') { error_here(E_SYNTAX); return TY_ERR; }
    p++;
    op(OP_MID2);
    return TY_STR;
  }
  if (peek() != ')') { error_here(E_SYNTAX); return TY_ERR; }
  p++;
  op(tok == T_LEFT ? OP_LEFT : OP_RIGHT);
  return TY_STR;
}

/* FRM.ELEMENT: one operand into FAC (or SACC), with the unary operators it
 * starts with left on the pending stack */
static int element(Pending *pend, int *np)
{
  for (;;) {
    uint8_t c = peek();
    if (is_digit(c) || c == '.') {
      uint16_t slot;
      if (!literal(&slot)) { error_here(0x45); return TY_ERR; }  /* FIN's ?OVERFLOW, at run time */
      op16(OP_LDC, slot);
      return TY_NUM;
    }
    if (is_letter(c)) {
      Name nm;
      read_name(&nm);
      return load_name(&nm);
    }
    if (c == T_MINUS || c == T_NOT) {     /* MIN / NOT: an operator of precedence $7F */
      p++;
      if (*np == MAX_PENDING) { error_here(E_FORMULA); return TY_ERR; }
      op(OP_PUSH);
      pend[*np].prec = P_UNARY, pend[*np].opcode = c == T_MINUS ? OP_NEG : OP_NOT, pend[*np].mask = 0;
      pend[*np].str = 0;
      (*np)++;
      continue;
    }
    if (c == T_PLUS) { p++; continue; }   /* unary plus: skipped */
    if (c == '(') {                        /* PARCHK */
      int t;
      p++;
      t = expr();
      if (t == TY_ERR) return t;
      if (peek() != ')') { error_here(E_SYNTAX); return TY_ERR; }
      p++;
      return t;
    }
    if (c == '"') {                        /* STRTXT: a literal, used in place */
      int s0 = ++p, q = s0;
      while (q < blen && buf[q] && buf[q] != '"') q++;
      if (direct) {                        /* a direct line's text is gone after it: the */
        op16(OP_SLIT, str_literal(buf, 0));        /* string goes to string space ("" + it), */
        op(OP_SPUSH);                              /* as AppleSoft copies one from the input */
      }                                            /* buffer */
      op16(OP_SLIT, str_literal(buf + s0, (uint8_t)(q - s0)));
      if (direct) op(OP_SCAT);
      p = q < blen && buf[q] == '"' ? q + 1 : q;
      return TY_STR;
    }
    if (c == T_FN) {                       /* FUNCT: FN name(argument) */
      Name nm;
      p++;
      if (!read_name(&nm) || nm.type != 0 || !nm.is_array) { error_here(E_SYNTAX); return TY_ERR; }
      if (!paren_arg(TY_NUM)) return TY_ERR;
      op16(OP_FNCALL, fn_slot(nm.c1, nm.c2));
      return TY_NUM;
    }
    if (c == T_SCRN) {                     /* SCRN(x,y): PLOTFNS */
      int t;
      p++;
      if ((t = expr()) != TY_NUM) { if (t != TY_ERR) error_here(E_TYPE_MISMATCH); return TY_ERR; }
      op(OP_BYTEPUSH), emit8(1);
      if (next() != ',') { error_here(E_SYNTAX); return TY_ERR; }
      if ((t = expr()) != TY_NUM) { if (t != TY_ERR) error_here(E_TYPE_MISMATCH); return TY_ERR; }
      if (next() != ')') { error_here(E_SYNTAX); return TY_ERR; }
      op(OP_SCRN);
      return TY_NUM;
    }
    if (c == T_POS || c == T_PEEK) {
      int t;
      p++;
      if (peek() != '(') { error_here(E_SYNTAX); return TY_ERR; }
      p++;
      if ((t = expr()) == TY_ERR) return t;
      if (c == T_PEEK && t != TY_NUM) { error_here(E_TYPE_MISMATCH); return TY_ERR; }
      if (next() != ')') { error_here(E_SYNTAX); return TY_ERR; }
      op(c == T_POS ? OP_POS : OP_PEEK);
      return TY_NUM;
    }
    if (c >= T_SGN && c <= 0xEA) {         /* functions */
      static const uint8_t fn_of[] = {     /* T_SGN.. : FN number, or $FF not one of these */
        FN_SGN, FN_INT, FN_ABS, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, FN_SQR, FN_RND, FN_LOG, FN_EXP,
        FN_COS, FN_SIN, FN_TAN, FN_ATN };
      uint8_t f = (uint8_t)(c - T_SGN) < sizeof fn_of ? fn_of[c - T_SGN] : 0xFF;
      p++;
      if (f != 0xFF) {
        if (!paren_arg(TY_NUM)) return TY_ERR;
        op(OP_FN);
        emit8(f);
        return TY_NUM;
      }
      switch (c) {
      case T_USR: if (!paren_arg(TY_NUM)) return TY_ERR; op(OP_USR); return TY_NUM;
      case T_PDL: if (!paren_arg(TY_NUM)) return TY_ERR; op(OP_PDL); return TY_NUM;
      case T_LEN: if (!paren_arg(TY_STR)) return TY_ERR; op(OP_LEN); return TY_NUM;
      case T_ASC: if (!paren_arg(TY_STR)) return TY_ERR; op(OP_ASC); return TY_NUM;
      case T_VAL: if (!paren_arg(TY_STR)) return TY_ERR; op(OP_VAL); return TY_NUM;
      case T_CHR: if (!paren_arg(TY_NUM)) return TY_ERR; op(OP_CHR); return TY_STR;
      case T_STR: if (!paren_arg(TY_NUM)) return TY_ERR; op(OP_STR); return TY_STR;
      case T_FRE: {                        /* FRE(any) */
        int t;
        if (peek() != '(') { error_here(E_SYNTAX); return TY_ERR; }
        p++;
        if ((t = expr()) == TY_ERR) return t;
        if (peek() != ')') { error_here(E_SYNTAX); return TY_ERR; }
        p++;
        op(OP_FRE);
        return TY_NUM;
      }
      case T_LEFT: case T_RIGHT: case T_MID: return string_fn(c);
      }
      error_here(E_UNIMPLEMENTED);
      return TY_ERR;
    }
    error_here(E_SYNTAX);
    return TY_ERR;
  }
}

/* FRMEVL: an expression into FAC (or SACC); returns its type */
static int expr(void)
{
  Pending pend[MAX_PENDING];
  int np = 0, t;
  t = element(pend, &np);
  if (t == TY_ERR) return t;
  for (;;) {
    uint8_t c = peek(), prec = 0, opcode = 0, mask = 0;
    if (c == T_PLUS && t == TY_STR) {      /* CAT: at once, with the next element */
      p++;
      op(OP_SPUSH);
      {
        Pending none[MAX_PENDING];
        int nn = 0, t2 = element(none, &nn);
        if (t2 == TY_ERR) return t2;
        if (nn || t2 != TY_STR) { error_here(E_TYPE_MISMATCH); return TY_ERR; }
      }
      op(OP_SCAT);
      continue;
    }
    if (c == T_GT || c == T_EQ || c == T_LT) {        /* relations: their bits accumulate */
      while (c = peek(), c == T_GT || c == T_EQ || c == T_LT) {
        uint8_t bit = c == T_GT ? 1 : c == T_EQ ? 2 : 4, m = (uint8_t)(mask ^ bit);
        if (m < mask) { error_here(E_SYNTAX); return TY_ERR; }
        mask = m;
        p++;
      }
      prec = P_REL, opcode = OP_REL;
    } else if (c >= T_PLUS && c <= T_OR) {
      static const uint8_t precs[] = {P_ADD, P_ADD, P_MUL, P_MUL, P_POW, P_AND, P_OR};
      static const uint8_t ops[] = {OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_AND, OP_OR};
      prec = precs[c - T_PLUS], opcode = ops[c - T_PLUS];
      p++;
    } else
      break;
    while (np > 0 && pend[np - 1].prec >= prec)
      if ((t = perform(&pend[--np], t)) == TY_ERR) return t;
    if (t == TY_STR && opcode != OP_REL) { error_here(E_TYPE_MISMATCH); return TY_ERR; }   /* CHKNUM */
    if (np == MAX_PENDING) { error_here(E_FORMULA); return TY_ERR; }
    op(t == TY_STR ? OP_SPUSH : OP_PUSH);
    pend[np].prec = prec, pend[np].opcode = opcode, pend[np].mask = mask, pend[np].str = t == TY_STR;
    np++;
    t = element(pend, &np);
    if (t == TY_ERR) return t;
  }
  while (np > 0)
    if ((t = perform(&pend[--np], t)) == TY_ERR) return t;
  return t;
}

/* ---- statements ---- */
static int num_then(int o);
static int want(uint8_t c);

static void st_print(void)
{
  int last_was_item = 0;
  if (at_end()) { op(OP_PRCR); return; }
  while (!at_end()) {
    uint8_t c = peek();
    int t;
    if (c == ';') { p++; last_was_item = 0; continue; }
    if (c == ',') { p++; op(OP_PRCOMMA); last_was_item = 0; continue; }
    if (c == T_TABP || c == T_SPCP) {       /* GTBYTC, ')', the spaces; no CR after */
      p++;
      if (!num_then(-1) || !want(')')) return;
      op(c == T_TABP ? OP_TAB : OP_SPC);
      last_was_item = 0;
      continue;
    }
    t = expr();
    if (t == TY_ERR) return;
    op(t == TY_STR ? OP_SPRINT : OP_PRNUM);
    last_was_item = 1;
  }
  if (last_was_item) op(OP_PRCR);          /* PR.STRING: at the statement's end, CRDO */
}

/* LET (or implied): the variable first - its subscripts are evaluated
 * before the value, as LET's PTRGET runs first */
static void st_let(void)
{
  Name nm;
  int a = -1, t;
  if (!read_name(&nm)) { error_here(E_SYNTAX); return; }
  if (nm.is_array) {
    int n = subscripts();
    if (!n) { error_here(E_SYNTAX); return; }
    a = array_slot(nm.c1, nm.c2, nm.type, (uint8_t)n);
    if (a < 0) { error_here(E_BAD_SUBSCRIPT); return; }
  }
  if (next() != T_EQ) { error_here(E_SYNTAX); return; }
  t = expr();
  if (t == TY_ERR) return;
  if (t != (nm.type == 2 ? TY_STR : TY_NUM)) { error_here(E_TYPE_MISMATCH); return; }
  if (nm.is_array)
    op16(nm.type == 2 ? OP_SSTA : nm.type == 1 ? OP_STAI : OP_STA, pass == 2 ? desc_off[a] : 0);
  else
    op16(nm.type == 2 ? OP_SSTV : nm.type == 1 ? OP_STVI : OP_STV, var_slot(nm.c1, nm.c2, nm.type));
}

static void jump_to(int opcode, uint16_t n)
{
  uint16_t a = line_addr(n);
  if (pass == 2 && !a) op16(OP_ERR, E_UNDEF_STATEMENT);   /* same size as the jump */
  else op16(opcode, a);
}

static void st_goto(int opcode)
{
  uint16_t n;
  int r = read_linenum(&n);
  if (r <= 0) { error_here(E_SYNTAX); return; }
  jump_to(opcode, n);
}

static void statement(void);
static int num_then(int o);
static int want(uint8_t c);

static void st_if(void)
{
  uint8_t c;
  int t = expr();
  if (t == TY_ERR) return;
  if (t != TY_NUM) { error_here(E_UNIMPLEMENTED); return; }   /* string IF: M3 */
  c = peek();
  if (c != T_GOTO && c != T_THEN) { error_here(E_SYNTAX); return; }
  p++;
  op16(OP_IFNOT, next_line_addr());
  if (c == T_GOTO || is_digit(peek())) { st_goto(OP_GOTO); return; }
  /* THEN statements: the rest of the line runs only when true */
  statement();
}

static void st_for(void)
{
  Name nm;
  uint16_t v;
  int t;
  if (!read_name(&nm) || nm.is_array || nm.type != 0) { error_here(E_SYNTAX); return; }
  v = var_slot(nm.c1, nm.c2, 0);
  if (next() != T_EQ) { error_here(E_SYNTAX); return; }
  if ((t = expr()) != TY_NUM) { if (t != TY_ERR) error_here(E_TYPE_MISMATCH); return; }
  op16(OP_STV, v);
  op16(OP_FORBEGIN, v);
  if (next() != T_TO) { error_here(E_SYNTAX); return; }
  if ((t = expr()) != TY_NUM) { if (t != TY_ERR) error_here(E_TYPE_MISMATCH); return; }
  op(OP_FORLIM);
  if (peek() == T_STEP) {
    p++;
    if ((t = expr()) != TY_NUM) { if (t != TY_ERR) error_here(E_TYPE_MISMATCH); return; }
  } else {
    static const uint8_t one[7] = {0x81, 0x80, 0, 0, 0, 0, 0};   /* CON.ONE as LOAD.FAC leaves it */
    op16(OP_LDC, const_slot(one));
  }
  op(OP_FORSTEP);
}

static void st_next(void)
{
  if (at_end()) { op(OP_NEXT0); return; }
  for (;;) {
    Name nm;
    if (!read_name(&nm) || nm.is_array || nm.type != 0) { error_here(E_SYNTAX); return; }
    op16(OP_NEXT, var_slot(nm.c1, nm.c2, 0));
    if (peek() != ',') return;
    p++;
  }
}

static void st_dim(void)
{
  for (;;) {
    Name nm;
    int n, a;
    if (!read_name(&nm) || !nm.is_array) { error_here(E_SYNTAX); return; }
    n = subscripts();
    if (!n) { error_here(E_SYNTAX); return; }
    a = array_slot(nm.c1, nm.c2, nm.type, (uint8_t)n);
    if (a < 0) { error_here(E_BAD_SUBSCRIPT); return; }
    op16(OP_DIM, pass == 2 ? desc_off[a] : 0);
    if (peek() != ',') return;
    p++;
  }
}


/* the variables of INPUT / READ / GET, each an ITEM */
static void input_items(void)
{
  for (;;) {
    Name nm;
    if (!read_name(&nm)) { error_here(E_SYNTAX); return; }
    if (nm.is_array) {
      int n = subscripts(), a;
      if (!n) { error_here(E_SYNTAX); return; }
      a = array_slot(nm.c1, nm.c2, nm.type, (uint8_t)n);
      if (a < 0) { error_here(E_BAD_SUBSCRIPT); return; }
      op(OP_ITEM), emit8((uint8_t)(nm.type | 0x80)), emit16(pass == 2 ? desc_off[a] : 0);
    } else
      op(OP_ITEM), emit8(nm.type), emit16(var_slot(nm.c1, nm.c2, nm.type));
    if (peek() != ',') break;
    p++;
  }
  op(OP_INPEND);
}

static void st_input(void)
{
  uint16_t prompt = 0;
  if (peek() == '"') {                     /* a prompt: printed instead of "?" */
    int s0 = ++p, q = s0;
    while (q < blen && buf[q] && buf[q] != '"') q++;
    prompt = str_literal(buf + s0, (uint8_t)(q - s0));
    p = q < blen && buf[q] == '"' ? q + 1 : q;
    if (next() != ';') { error_here(E_SYNTAX); return; }
  }
  op16(OP_INPSTART, prompt);
  input_items();
}

/* a numeric expression then an operation (COLOR= HTAB VTAB ...) */
static int num_then(int o)
{
  int t = expr();
  if (t == TY_ERR) return 0;
  if (t != TY_NUM) { error_here(E_TYPE_MISMATCH); return 0; }
  if (o >= 0) op(o);
  return 1;
}

static int want(uint8_t c)
{
  if (next() != c) { error_here(E_SYNTAX); return 0; }
  return 1;
}

static void st_on(void)
{
  uint8_t tok, k = 0;
  uint16_t at, nums[256];
  if (!num_then(-1)) return;
  tok = next();
  if (tok != T_GOTO && tok != T_GOSUB) { error_here(E_SYNTAX); return; }
  for (;;) {
    int r = read_linenum(&nums[k]);
    if (r <= 0) { error_here(E_SYNTAX); return; }
    k++;
    if (peek() != ',' || k == 255) break;
    p++;
  }
  op(OP_ON), emit8(k), emit8(tok == T_GOSUB);
  for (at = 0; at < k; at++) emit16(line_addr(nums[at]));
}

static void st_def(void)
{
  Name fn, prm;
  uint16_t entry, skip_at;
  int t;
  if (!want(T_FN)) return;
  if (!read_name(&fn) || fn.type != 0 || !fn.is_array) { error_here(E_SYNTAX); return; }
  p++;                                     /* '(' */
  if (!read_name(&prm) || prm.type != 0 || prm.is_array) { error_here(E_SYNTAX); return; }
  if (!want(')') || !want(T_EQ)) return;
  entry = fn_slot(fn.c1, fn.c2);
  op16(OP_DEFFN, entry);
  emit16(var_slot(prm.c1, prm.c2, 0));
  skip_at = pc;
  emit16(0);                               /* past the body: patched below */
  t = expr();
  if (t == TY_ERR) return;
  if (t != TY_NUM) { error_here(E_TYPE_MISMATCH); return; }
  op(OP_FNRET);
  if (pass == 2) patch8(skip_at, (uint8_t)pc), patch8((uint16_t)(skip_at + 1), (uint8_t)(pc >> 8));
}

/* PLOT/HLIN/VLIN's coordinates: e (checked < 80 when r), pushed */
static int coord(int r)
{
  if (!num_then(-1)) return 0;
  op(OP_BYTEPUSH), emit8((uint8_t)r);
  return 1;
}

/* HFNS ($F6B9): x (GETADR, < 280, checked before y is looked at), ','
 * and y left in FAC (the operation after takes it: GETBYT, < 192) */
static int hfns(void)
{
  return num_then(OP_HX) && want(',') && num_then(-1);
}

/* HPLOT x,y [TO x,y]... / HPLOT TO x,y...: HPLOT0 then HGLIN for each TO */
static void st_hplot(void)
{
  if (peek() != T_TO) {
    if (!hfns()) return;
    op(OP_HPLOT);
  }
  while (peek() == T_TO) {
    p++;
    if (!hfns()) return;
    op(OP_HPLOTTO);
  }
}

/* DRAW/XDRAW n [AT x,y] (DRWPNT): the shape, then the place, then it */
static void st_draw(int o)
{
  if (!num_then(OP_SHAPE)) return;
  if (peek() == T_AT) {
    p++;
    if (!hfns()) return;
    op(OP_HPOSN);
  }
  op(o);
}

/* INPUT, GET and DEF in a direct line: ?ILLEGAL DIRECT when it runs */
static void illegal_direct(void)
{
  op16(OP_ERR, E_ILLEGAL_DIRECT);
  skip_statement();
}

static void statement(void)
{
  uint8_t c = peek();
  if (c == 0 || c == ':') return;
  if (use_onerr) op(OP_STMT);
  if (is_letter(c)) { st_let(); return; }
  p++;
  switch (c) {
  case T_LET: st_let(); return;
  case T_PRINT: st_print(); return;
  case T_END: op(OP_END); return;
  case T_GOTO: st_goto(OP_GOTO); return;
  case T_GOSUB: st_goto(OP_GOSUB); return;
  case T_RETURN: op(OP_RETURN); return;
  case T_IF: st_if(); return;
  case T_FOR: st_for(); return;
  case T_NEXT: st_next(); return;
  case T_DIM: st_dim(); return;
  case T_REM: p = blen; return;
  case T_DATA: skip_statement(); return;    /* READ reads it from the program's text */
  case T_INPUT: if (direct) { illegal_direct(); return; } st_input(); return;
  case T_READ: op(OP_READSTART); input_items(); return;
  case T_GET: if (direct) { illegal_direct(); return; } op(OP_GETSTART); input_items(); return;
  case T_RESTORE: op(OP_RESTORE); return;
  case T_ON: st_on(); return;
  case T_POP: op(OP_POP); return;
  case T_DEF: if (direct) { illegal_direct(); return; } st_def(); return;
  case T_POKE: if (num_then(OP_ADR) && want(',')) num_then(OP_POKE); return;
  case T_WAIT:
    if (!num_then(OP_ADR) || !want(',') || !coord(0)) return;
    if (peek() == ',') { p++; if (!coord(0)) return; }
    else {
      static const uint8_t zero[7] = {0};
      op16(OP_LDC, const_slot(zero));
      op(OP_BYTEPUSH), emit8(0);
    }
    op(OP_WAIT);
    return;
  case T_CALL: num_then(OP_CALL); return;
  case T_PRN: num_then(OP_PRN); return;
  case T_INN: num_then(OP_INN); return;
  case T_HTAB: num_then(OP_HTAB); return;
  case T_VTAB: num_then(OP_VTAB); return;
  case T_COLOR: num_then(OP_COLOR); return;
  case T_SPEED: num_then(OP_SPEED); return;
  case T_HOME: op(OP_HOME); return;
  case T_NORMAL: op(OP_NORMAL); return;
  case T_INVERSE: op(OP_INVERSE); return;
  case T_FLASH: op(OP_FLASH); return;
  case T_TRACE: op(OP_TRACE); return;
  case T_NOTRACE: op(OP_NOTRACE); return;
  case T_TEXT: op(OP_TEXT); return;
  case T_GR: op(OP_GR); return;
  case T_CLEAR: op(OP_CLEAR); return;
  case T_STOP: op(OP_STOP); return;
  case T_HGR: op(OP_HGR); return;
  case T_HGR2: op(OP_HGR2); return;
  case T_HCOLOR: num_then(OP_HCOLOR); return;
  case T_HPLOT: st_hplot(); return;
  case T_DRAW: st_draw(OP_DRAW); return;
  case T_XDRAW: st_draw(OP_XDRAW); return;
  case T_ROT: num_then(OP_ROT); return;
  case T_SCALE: num_then(OP_SCALE); return;
  case T_CONT: op(OP_CONT); return;
  case T_LIST: {                          /* LIST [a][-,][b] ($D6A5): the shell lists */
    uint16_t a = 0, b;
    uint8_t d = peek();
    if (!is_digit(d) && d && d != ':' && d != T_MINUS && d != ',') { error_here(E_SYNTAX); return; }
    if (read_linenum(&a) < 0) { error_here(E_SYNTAX); return; }
    b = a;
    if ((d = peek()) == T_MINUS || d == ',') {
      p++;
      b = 0;
      if (read_linenum(&b) < 0) { error_here(E_SYNTAX); return; }
    }
    if ((d = peek()) != 0 && d != ':') { error_here(E_SYNTAX); return; }
    op16(OP_LIST, a);
    emit16(b ? b : 0xFFFF);
    return;
  }
  case T_NEW:                             /* NEW: the program gone, back to the prompt */
    if (peek() && peek() != ':') { error_here(E_SYNTAX); return; }
    op(OP_NEW);
    return;
  case T_DEL: {                           /* DEL a,b ($F331): lines out, back to the prompt */
    uint16_t a, b = 0;
    if (read_linenum(&a) <= 0) { error_here(E_SYNTAX); return; }
    if (!want(',')) return;
    if (read_linenum(&b) < 0) { error_here(E_SYNTAX); return; }
    op16(OP_DEL, a);
    emit16(b);
    return;
  }
  case T_AMPER: case T_SHLOAD: case T_RECALL: case T_STORE: case T_SAVE: case T_LOAD: {
    /* & (the GS's AppleSoft sends the cassette's statements there too): the
     * statement's text, the token first, for the routine at $3F5 to read */
    uint16_t at = p, k;
    skip_statement();
    op(OP_AMPER);
    emit8((uint8_t)(p - at + 1));
    emit8(c);
    for (k = at; k < p; k++) emit8(buf[k]);
    return;
  }
  case T_RESUME: op(OP_RESUME); return;
  case T_LOMEM: num_then(OP_LOMEM); return;
  case T_HIMEM: num_then(OP_HIMEM); return;
  case T_PLOT: if (coord(1) && want(',')) num_then(OP_PLOT); return;
  case T_HLIN: if (coord(1) && want(',') && coord(0) && want(T_AT)) num_then(OP_HLIN); return;
  case T_VLIN: if (coord(1) && want(',') && coord(0) && want(T_AT)) num_then(OP_VLIN); return;
  case T_RUN: {
    uint16_t n;
    int r = read_linenum(&n);
    if (r < 0) { error_here(E_SYNTAX); return; }
    if (r == 0) op16(OP_RUN, code_base);
    else jump_to(OP_RUN, n);
    return;
  }
  case T_ONERR: {
    uint16_t n;
    if (!want(T_GOTO) || read_linenum(&n) <= 0) { error_here(E_SYNTAX); return; }
    jump_to(OP_ONERR, n);
    p = blen;                              /* REMN: the rest of the line is skipped */
    return;
  }
  default:
    op16(OP_ERR, c < 0x80 ? E_SYNTAX : E_UNIMPLEMENTED);
    skip_statement();
    return;
  }
}

/* ---- the program ---- */
/* the line at at into buf; its link, 0 after the last line */
static uint16_t read_line(uint16_t at, uint16_t *num)
{
  uint8_t h[4];
  uint16_t link, n;
  pf->read(pf->ctx, at, h, 4);
  link = (uint16_t)(h[0] | h[1] << 8);
  if (!link) return 0;
  *num = (uint16_t)(h[2] | h[3] << 8);
  line_at = (uint16_t)(at + 4);
  n = (uint16_t)(link - line_at);       /* the text and its 0 */
  if (n > sizeof buf - 1) n = sizeof buf - 1;
  pf->read(pf->ctx, line_at, buf, n);
  buf[n] = 0;
  for (blen = 0; buf[blen]; blen++) {}
  p = 0;
  return link;
}

static void run_pass(void)
{
  uint16_t at = 0x0801, link, num;
  pc = code_base;
  clen = 0;
  nstmts = 0;
  nvars = narrays = nfns = 0;
  if (pass == 1) {
    nconsts = nlits = 0;
    memset(const_head, 0, sizeof const_head);
  }
  lit_i = 0;
  str_bytes = 0;
  str_at = str_base;
  line_i = 0;
  for (;;) {
    if (!(link = read_line(at, &num))) break;
    if (line_i == MAX_LINES) { overflow = 1; break; }
    lines[line_i].num = num, lines[line_i].addr = pc;
    op16(OP_LINE, num);
    for (;;) {
      if (pf->poll) pf->poll(pf->ctx);
      if (pass == 2) out((uint16_t)(stmt_base + 2 * nstmts), (uint8_t)pc), out((uint16_t)(stmt_base + 2 * nstmts + 1), (uint8_t)(pc >> 8));
      nstmts++;
      statement();
      if (p >= blen || peek() == 0) break;
      if (peek() == ':') { p++; continue; }
      error_here(E_SYNTAX);                 /* something left that is not a statement */
    }
    line_i++;
    at = link;
  }
  nlines = line_i;
  lines[nlines].num = 0xFFFF, lines[nlines].addr = pc;   /* running off the end: END */
  op(OP_END);
  if (pass == 2) flush_code(clen);
}

/* does the program use ONERR, RESUME or TRACE (statement marks needed)?
 * (tokens appear only as tokens: string, REM and DATA text is ASCII) */
static int scan_onerr(void)
{
  uint16_t at = 0x0801, num;
  int i;
  while ((at = read_line(at, &num)) != 0)
    for (i = 0; i < blen; i++)
      if (buf[i] == T_ONERR || buf[i] == T_RESUME || buf[i] == T_TRACE) return 1;
  return 0;
}

int as816_compile(const Platform *platform, CompileStats *st)
{
  uint16_t i, a;
  pf = platform;
  overflow = 0;
  direct = 0;
  dvar_at = 0;
  use_onerr = scan_onerr();
  code_base = (uint16_t)(pf->prog_start + HEADER_SIZE);
  const_base = str_base = 0;
  pass = 1;
  run_pass();
  if (overflow) return -1;

  /* the layout */
  const_base = pc;
  str_base = (uint16_t)(const_base + 7 * nconsts);
  var_base = (uint16_t)(str_base + str_bytes);
  for (i = 0, a = var_base; i < nvars; i++) {
    var_off[i] = a;
    a = (uint16_t)(a + (vars[i].type == 1 ? 2 : vars[i].type == 2 ? 4 : 5));
  }
  fn_base = a;
  a = (uint16_t)(a + 4 * nfns);
  desc_base = a;
  for (i = 0; i < narrays; i++) {
    desc_off[i] = a;
    a = (uint16_t)(a + 4 + 2 * arrays[i].ndims);
  }
  desc_bytes = (uint16_t)(a - desc_base);
  a = (uint16_t)(a + DESC_SPARE);              /* room for direct lines' arrays */
  desc_copy_base = a;
  a = (uint16_t)(a + desc_bytes + DESC_SPARE);
  nsvars = nsarrays = 0;
  for (i = 0; i < nvars; i++) nsvars += vars[i].type == 2;
  for (i = 0; i < narrays; i++) nsarrays += arrays[i].type == 2;
  svlist_base = a;
  svlist_cap = (uint16_t)(nsvars + SV_SPARE);
  salist_base = (uint16_t)(svlist_base + 2 * svlist_cap);
  salist_cap = (uint16_t)(nsarrays + SA_SPARE);
  stmt_base = (uint16_t)(salist_base + 2 * salist_cap);
  fstack_base = (uint16_t)(stmt_base + 2 * nstmts);
  sstack_base = (uint16_t)(fstack_base + FSTACK_SIZE);
  cstack_base = (uint16_t)(sstack_base + SSTACK_SIZE);
  heap_base = (uint16_t)(cstack_base + CSTACK_SIZE);
  if (heap_base < fstack_base || heap_base > DIRECT_BASE) return -1;

  pass = 2;
  run_pass();
  if (overflow) return -1;

  /* constants; variables zero; descriptors: no storage yet, their count */
  for (i = 0; i < nconsts; i++)
    for (a = 0; a < 7; a++) out((uint16_t)(const_base + 7 * i + a), consts[i][a]);
  for (a = var_base; a < desc_base; a++) out(a, 0);
  for (i = 0; i < narrays; i++) {
    uint16_t d = desc_off[i];
    uint8_t size = arrays[i].type == 1 ? 2 : arrays[i].type == 2 ? 4 : 5;
    out(d, 0), out((uint16_t)(d + 1), 0);
    out((uint16_t)(d + 2), arrays[i].ndims), out((uint16_t)(d + 3), size);
    /* the pristine copy CLEAR restores from */
    out((uint16_t)(d - desc_base + desc_copy_base), 0);
    out((uint16_t)(d - desc_base + desc_copy_base + 1), 0);
    out((uint16_t)(d - desc_base + desc_copy_base + 2), arrays[i].ndims);
    out((uint16_t)(d - desc_base + desc_copy_base + 3), size);
  }
  /* the collector's roots: the string variables, the string arrays */
  {
    uint16_t at = svlist_base;
    for (i = 0; i < nvars; i++)
      if (vars[i].type == 2) {
        out(at, (uint8_t)var_off[i]), out((uint16_t)(at + 1), (uint8_t)(var_off[i] >> 8));
        at = (uint16_t)(at + 2);
      }
    at = salist_base;                     /* (after the string variables' spare roots) */
    for (i = 0; i < narrays; i++)
      if (arrays[i].type == 2) {
        out(at, (uint8_t)desc_off[i]), out((uint16_t)(at + 1), (uint8_t)(desc_off[i] >> 8));
        at = (uint16_t)(at + 2);
      }
  }
  /* the header (vm_run reads it) */
  {
    uint16_t h = pf->prog_start, vals[22];
    vals[0] = code_base, vals[1] = fstack_base, vals[2] = cstack_base;
    vals[3] = (uint16_t)(cstack_base + CSTACK_SIZE), vals[4] = heap_base, vals[5] = DIRECT_BASE;
    vals[6] = sstack_base, vals[7] = svlist_base, vals[8] = nsvars, vals[9] = salist_base, vals[10] = nsarrays;
    vals[11] = 0x0800;                     /* DATA: READ from the text's start, as RESTORE */
    vals[12] = heap_base, vals[13] = var_base, vals[14] = desc_base, vals[15] = desc_copy_base;
    vals[16] = desc_bytes;
    vals[17] = 0;                          /* direct lines' variables: none yet */
    vals[18] = stmt_base, vals[19] = nstmts, vals[20] = const_base;   /* statements; the code's end */
    vals[21] = pf->text_bank;              /* the program's text (READ's DATA) */
    for (i = 0; i < 22; i++) {
      out((uint16_t)(h + 2 * i), (uint8_t)vals[i]);
      out((uint16_t)(h + 2 * i + 1), (uint8_t)(vals[i] >> 8));
    }
  }
  if (st) {
    st->code_size = (uint16_t)(const_base - code_base);
    st->lines = nlines, st->vars = nvars, st->arrays = narrays, st->consts = nconsts;
    st->strings = str_bytes;
    st->end = heap_base;
  }
  flush();
  return 0;
}

/* ---- direct lines ---- */

int as816_direct(const Platform *platform, const uint8_t *tok, uint16_t *entry)
{
  uint16_t nv = nvars, nf = nfns, nsv = nsvars, dv = dvar_at, na = narrays, nsa = nsarrays, db = desc_bytes;
  pf = platform;
  overflow = 0;
  direct = 1;
  pass = 2;
  for (blen = 0; tok[blen] && blen < sizeof buf - 1; blen++) buf[blen] = tok[blen];
  buf[blen] = 0;
  p = 0;
  line_at = 0;
  line_i = 0;
  str_at = DIRECT_DATA;
  pc = DIRECT_BASE;
  clen = 0;
  op(OP_END);                               /* where IF false and ON past its list go */
  *entry = pc;
  for (;;) {
    statement();
    if (p >= blen || peek() == 0) break;
    if (peek() == ':') { p++; continue; }
    error_here(E_SYNTAX);
  }
  op(OP_END);
  if (pc > DIRECT_DATA) overflow = 1;
  flush_code(clen);
  flush();
  direct = 0;
  if (overflow) {                          /* forget what it made: it never runs */
    nvars = nv, nfns = nf, dvar_at = dv, narrays = na;
    nsvars = nsv, nsarrays = nsa, desc_bytes = db;
    out((uint16_t)(pf->prog_start + 0x10), (uint8_t)nsvars), out((uint16_t)(pf->prog_start + 0x11), (uint8_t)(nsvars >> 8));
    out((uint16_t)(pf->prog_start + 0x14), (uint8_t)nsarrays), out((uint16_t)(pf->prog_start + 0x15), (uint8_t)(nsarrays >> 8));
    out((uint16_t)(pf->prog_start + 0x20), (uint8_t)desc_bytes), out((uint16_t)(pf->prog_start + 0x21), (uint8_t)(desc_bytes >> 8));
    flush();
    return -1;
  }
  return 0;
}

/* ---- the program's variables, by name (CHAIN carries them across) ---- */
uint16_t as816_count(int arrays_too)
{
  return arrays_too ? narrays : nvars;
}
void as816_var(uint16_t i, uint8_t name[3], uint16_t *addr)
{
  name[0] = vars[i].c1, name[1] = vars[i].c2, name[2] = vars[i].type;
  *addr = var_off[i];
}
void as816_array(uint16_t i, uint8_t name[3], uint8_t *ndims, uint16_t *desc)
{
  name[0] = arrays[i].c1, name[1] = arrays[i].c2, name[2] = arrays[i].type;
  *ndims = arrays[i].ndims;
  *desc = desc_off[i];
}

/* a variable (ndims 0) or array CHAIN brings that the program does not
 * name: made as a direct line makes one, so it stays for the next CHAIN or
 * a direct line; its slot (an array's descriptor), 0 if there is no room */
uint16_t as816_adopt(const Platform *platform, const uint8_t name[3], uint8_t ndims)
{
  uint16_t a = 0;
  int i;
  pf = platform;
  overflow = 0;
  direct = 1;
  pass = 2;
  if (ndims) {
    i = array_slot(name[0], name[1], name[2], ndims);
    if (i >= 0 && !overflow) a = desc_off[i];
  } else {
    a = var_slot(name[0], name[1], name[2]);
    if (overflow) a = 0;
  }
  flush();
  direct = 0;
  return a;
}
