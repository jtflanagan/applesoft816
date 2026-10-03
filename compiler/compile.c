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

/* ---- tokens ---- */
enum {
  T_END = 0x80, T_FOR = 0x81, T_NEXT = 0x82, T_DATA = 0x83, T_DIM = 0x86, T_LET = 0xAA, T_GOTO = 0xAB,
  T_IF = 0xAD, T_GOSUB = 0xB0, T_RETURN = 0xB1, T_REM = 0xB2, T_PRINT = 0xBA,
  T_TO = 0xC1, T_THEN = 0xC4, T_NOT = 0xC6, T_STEP = 0xC7, T_PLUS = 0xC8, T_MINUS = 0xC9,
  T_MUL = 0xCA, T_DIV = 0xCB, T_POW = 0xCC, T_AND = 0xCD, T_OR = 0xCE,
  T_GT = 0xCF, T_EQ = 0xD0, T_LT = 0xD1, T_SGN = 0xD2, T_INT = 0xD3, T_ABS = 0xD4,
  T_SQR = 0xDA, T_RND = 0xDB, T_LOG = 0xDC, T_EXP = 0xDD, T_COS = 0xDE, T_SIN = 0xDF,
  T_TAN = 0xE0, T_ATN = 0xE1
};

/* ---- limits ---- */
#define MAX_LINES 4000
#define MAX_VARS 400
#define MAX_ARRAYS 100
#define MAX_CONSTS 600
#define MAX_PENDING 64
#define FSTACK_SIZE 600
#define CSTACK_SIZE 4096
#define HEADER_SIZE 12

typedef struct { uint16_t num, addr; } Line;
typedef struct { uint8_t c1, c2, type; } Var;              /* type: 0 real, 1 integer, 2 string */
typedef struct { uint8_t c1, c2, type, ndims; } Array;

static const Platform *pf;
static int pass;
static uint16_t pc;                 /* the emit position (an address in the VM bank) */
static int overflow;                /* the image does not fit */

static Line lines[MAX_LINES + 1];      /* + the END after the last line */
static uint16_t nlines, line_i;     /* line_i: the line being compiled */
static Var vars[MAX_VARS];
static uint16_t nvars;
static Array arrays[MAX_ARRAYS];
static uint16_t narrays;
static uint8_t consts[MAX_CONSTS][7];
static uint16_t nconsts;
static uint16_t str_bytes;          /* string literal pool size, this pass */

/* the layout, fixed after pass 1 */
static uint16_t code_base, const_base, str_base, var_base, desc_base, fstack_base, cstack_base, heap_base;
static uint16_t var_off[MAX_VARS], desc_off[MAX_ARRAYS];
static uint16_t str_at;             /* pass 2: the next literal's address */

/* the line being compiled */
static uint8_t buf[256];
static int p, blen;

/* ---- emitting ---- */
static void emit8(uint8_t v)
{
  if (pass == 2) pf->wr(pf->ctx, pc, v);
  if (++pc == 0) overflow = 1;
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
static uint16_t var_slot(uint8_t c1, uint8_t c2, uint8_t type)
{
  uint16_t i;
  for (i = 0; i < nvars; i++)
    if (vars[i].c1 == c1 && vars[i].c2 == c2 && vars[i].type == type) break;
  if (i == nvars) {
    if (nvars == MAX_VARS) { overflow = 1; return 0; }
    vars[nvars].c1 = c1, vars[nvars].c2 = c2, vars[nvars].type = type;
    nvars++;
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
  }
  return arrays[i].ndims == ndims ? (int)i : -1;
}

static uint16_t const_slot(const uint8_t fac[7])
{
  uint16_t i;
  for (i = 0; i < nconsts; i++)
    if (!memcmp(consts[i], fac, 7)) break;
  if (i == nconsts) {
    if (nconsts == MAX_CONSTS) { overflow = 1; return 0; }
    memcpy(consts[nconsts++], fac, 7);
  }
  return (uint16_t)(const_base + 7 * i);
}

/* a string literal: length byte, then the text */
static uint16_t str_literal(const uint8_t *s, uint8_t n)
{
  uint16_t at = str_at, i;
  if (pass == 2) {
    pf->wr(pf->ctx, str_at, n);
    for (i = 0; i < n; i++) pf->wr(pf->ctx, (uint16_t)(str_at + 1 + i), s[i]);
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

typedef struct { uint8_t prec, opcode, mask; } Pending;

static int expr(void);

/* MATHTBL ($D0B2) precedences */
#define P_ADD 0x79
#define P_MUL 0x7B
#define P_POW 0x7D
#define P_AND 0x50
#define P_OR 0x46
#define P_UNARY 0x7F
#define P_REL 0x64

static void perform(const Pending *x)
{
  if (x->opcode == OP_REL) { op(OP_REL); emit8(x->mask); }
  else op(x->opcode);
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

/* FRM.ELEMENT: one operand into FAC, with the unary operators it starts
 * with left on the pending stack */
static int element(Pending *pend, int *np)
{
  for (;;) {
    uint8_t c = peek();
    if (is_digit(c) || c == '.') {
      uint8_t fac[7];
      int used = pf->fin(pf->ctx, buf + p, blen - p, fac);
      if (used <= 0) { error_here(0x45); return TY_ERR; }  /* FIN's ?OVERFLOW, at run time */
      p += used;
      op16(OP_LDC, const_slot(fac));
      return TY_NUM;
    }
    if (is_letter(c)) {
      Name nm;
      read_name(&nm);
      if (nm.type != 0) { error_here(E_UNIMPLEMENTED); return TY_ERR; }   /* % and $: M3 */
      if (nm.is_array) {
        int n = subscripts(), a;
        if (!n) { error_here(E_SYNTAX); return TY_ERR; }
        a = array_slot(nm.c1, nm.c2, nm.type, (uint8_t)n);
        if (a < 0) { error_here(E_BAD_SUBSCRIPT); return TY_ERR; }
        op16(OP_LDA, pass == 2 ? desc_off[a] : 0);
        return TY_NUM;
      }
      op16(OP_LDV, var_slot(nm.c1, nm.c2, nm.type));
      return TY_NUM;
    }
    if (c == T_MINUS || c == T_NOT) {     /* MIN / NOT: an operator of precedence $7F */
      p++;
      if (*np == MAX_PENDING) { error_here(0xBF); return TY_ERR; }   /* FORMULA TOO COMPLEX */
      op(OP_PUSH);
      pend[*np].prec = P_UNARY, pend[*np].opcode = c == T_MINUS ? OP_NEG : OP_NOT, pend[*np].mask = 0;
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
    if (c >= T_SGN && c <= 0xEA) {         /* functions */
      static const uint8_t fn_of[] = {     /* T_SGN.. : FN number, or $FF not yet */
        FN_SGN, FN_INT, FN_ABS, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, FN_SQR, FN_RND, FN_LOG, FN_EXP,
        FN_COS, FN_SIN, FN_TAN, FN_ATN };
      uint8_t f = (uint8_t)(c - T_SGN) < sizeof fn_of ? fn_of[c - T_SGN] : 0xFF;
      int t;
      if (f == 0xFF) { error_here(E_UNIMPLEMENTED); return TY_ERR; }
      p++;
      if (peek() != '(') { error_here(E_SYNTAX); return TY_ERR; }
      p++;
      t = expr();
      if (t == TY_ERR) return t;
      if (t != TY_NUM) { error_here(E_TYPE_MISMATCH); return TY_ERR; }
      if (peek() != ')') { error_here(E_SYNTAX); return TY_ERR; }
      p++;
      op(OP_FN);
      emit8(f);
      return TY_NUM;
    }
    if (c == '"') { error_here(E_UNIMPLEMENTED); return TY_ERR; }   /* strings: M3 */
    error_here(E_SYNTAX);
    return TY_ERR;
  }
}

/* FRMEVL: an expression into FAC; returns its type */
static int expr(void)
{
  Pending pend[MAX_PENDING];
  int np = 0, t;
  t = element(pend, &np);
  if (t == TY_ERR) return t;
  for (;;) {
    uint8_t c = peek(), prec = 0, opcode = 0, mask = 0;
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
    if (t != TY_NUM) { error_here(E_UNIMPLEMENTED); return TY_ERR; }   /* string operators: M3 */
    while (np > 0 && pend[np - 1].prec >= prec) perform(&pend[--np]);
    if (np == MAX_PENDING) { error_here(0xBF); return TY_ERR; }
    op(OP_PUSH);
    pend[np].prec = prec, pend[np].opcode = opcode, pend[np].mask = mask;
    np++;
    t = element(pend, &np);
    if (t == TY_ERR) return t;
  }
  while (np > 0) perform(&pend[--np]);
  return t;
}

/* ---- statements ---- */
static void st_print(void)
{
  int last_was_item = 0;
  if (at_end()) { op(OP_PRCR); return; }
  while (!at_end()) {
    uint8_t c = peek();
    if (c == ';') { p++; last_was_item = 0; continue; }
    if (c == ',') { p++; op(OP_PRCOMMA); last_was_item = 0; continue; }
    if (c == '"') {                        /* a literal on its own: STRLIT / STRPRT */
      int s = ++p, q = s;
      while (q < blen && buf[q] && buf[q] != '"') q++;
      op16(OP_PRSTR, str_literal(buf + s, (uint8_t)(q - s)));
      p = q < blen && buf[q] == '"' ? q + 1 : q;
      last_was_item = 1;
      continue;
    }
    {
      int t = expr();
      if (t == TY_ERR) return;
      if (t != TY_NUM) { error_here(E_UNIMPLEMENTED); return; }
      op(OP_PRNUM);
      last_was_item = 1;
    }
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
  if (nm.type != 0) { error_here(E_UNIMPLEMENTED); return; }
  if (nm.is_array) {
    int n = subscripts();
    if (!n) { error_here(E_SYNTAX); return; }
    a = array_slot(nm.c1, nm.c2, nm.type, (uint8_t)n);
    if (a < 0) { error_here(E_BAD_SUBSCRIPT); return; }
  }
  if (next() != T_EQ) { error_here(E_SYNTAX); return; }
  t = expr();
  if (t == TY_ERR) return;
  if (t != TY_NUM) { error_here(E_TYPE_MISMATCH); return; }
  if (nm.is_array) op16(OP_STA, pass == 2 ? desc_off[a] : 0);
  else op16(OP_STV, var_slot(nm.c1, nm.c2, nm.type));
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
    if (nm.type != 0) { error_here(E_UNIMPLEMENTED); return; }
    n = subscripts();
    if (!n) { error_here(E_SYNTAX); return; }
    a = array_slot(nm.c1, nm.c2, nm.type, (uint8_t)n);
    if (a < 0) { error_here(E_BAD_SUBSCRIPT); return; }
    op16(OP_DIM, pass == 2 ? desc_off[a] : 0);
    if (peek() != ',') return;
    p++;
  }
}

static void statement(void)
{
  uint8_t c = peek();
  if (c == 0 || c == ':') return;
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
  case T_DATA: skip_statement(); return;    /* READ: M3 */
  default:
    op16(OP_ERR, c < 0x80 ? E_SYNTAX : E_UNIMPLEMENTED);
    skip_statement();
    return;
  }
}

/* ---- the program ---- */
static void read_line(uint16_t at, uint16_t *link, uint16_t *num)
{
  uint16_t a = (uint16_t)(at + 4);
  *link = (uint16_t)(pf->prog(pf->ctx, at) | pf->prog(pf->ctx, (uint16_t)(at + 1)) << 8);
  *num = (uint16_t)(pf->prog(pf->ctx, (uint16_t)(at + 2)) | pf->prog(pf->ctx, (uint16_t)(at + 3)) << 8);
  blen = 0;
  while (blen < (int)sizeof buf - 1 && (buf[blen] = pf->prog(pf->ctx, a++)) != 0) blen++;
  buf[blen] = 0;
  p = 0;
}

static void run_pass(void)
{
  uint16_t at = 0x0801, link, num;
  pc = code_base;
  nvars = narrays = nconsts = 0;
  str_bytes = 0;
  str_at = str_base;
  line_i = 0;
  for (;;) {
    if (pf->prog(pf->ctx, at) == 0 && pf->prog(pf->ctx, (uint16_t)(at + 1)) == 0) break;
    read_line(at, &link, &num);
    if (line_i == MAX_LINES) { overflow = 1; break; }
    lines[line_i].num = num, lines[line_i].addr = pc;
    op16(OP_LINE, num);
    for (;;) {
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
}

int as816_compile(const Platform *platform, CompileStats *st)
{
  uint16_t i, a;
  pf = platform;
  overflow = 0;
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
    a = (uint16_t)(a + (vars[i].type == 1 ? 2 : vars[i].type == 2 ? 3 : 5));
  }
  desc_base = a;
  for (i = 0; i < narrays; i++) {
    desc_off[i] = a;
    a = (uint16_t)(a + 4 + 2 * arrays[i].ndims);
  }
  fstack_base = a;
  cstack_base = (uint16_t)(fstack_base + FSTACK_SIZE);
  heap_base = (uint16_t)(cstack_base + CSTACK_SIZE);
  if (heap_base < fstack_base || heap_base > 0xFF00) return -1;

  pass = 2;
  run_pass();
  if (overflow) return -1;

  /* constants; variables zero; descriptors: no storage yet, their count */
  for (i = 0; i < nconsts; i++)
    for (a = 0; a < 7; a++) pf->wr(pf->ctx, (uint16_t)(const_base + 7 * i + a), consts[i][a]);
  for (a = var_base; a < desc_base; a++) pf->wr(pf->ctx, a, 0);
  for (i = 0; i < narrays; i++) {
    uint16_t d = desc_off[i];
    pf->wr(pf->ctx, d, 0), pf->wr(pf->ctx, (uint16_t)(d + 1), 0);
    pf->wr(pf->ctx, (uint16_t)(d + 2), arrays[i].ndims), pf->wr(pf->ctx, (uint16_t)(d + 3), 0);
  }
  /* the header (vm_run reads it) */
  {
    uint16_t h = pf->prog_start, vals[6];
    vals[0] = code_base, vals[1] = fstack_base, vals[2] = cstack_base;
    vals[3] = (uint16_t)(cstack_base + CSTACK_SIZE), vals[4] = heap_base, vals[5] = 0xFFFF;
    for (i = 0; i < 6; i++) {
      pf->wr(pf->ctx, (uint16_t)(h + 2 * i), (uint8_t)vals[i]);
      pf->wr(pf->ctx, (uint16_t)(h + 2 * i + 1), (uint8_t)(vals[i] >> 8));
    }
  }
  if (st) {
    st->code_size = (uint16_t)(const_base - code_base);
    st->lines = nlines, st->vars = nvars, st->arrays = narrays, st->consts = nconsts;
    st->strings = str_bytes;
    st->end = heap_base;
  }
  return 0;
}
