/* compile.h - the applesoft816 compiler (docs/INTERP.md).
 *
 * Portable C99: the same source is built by llvm-mos-65816 for the IIgs and
 * by gcc for the PC tools.  Everything machine-specific comes through
 * Platform: reading the tokenized program, writing the VM bank, and FIN.
 */
#ifndef AS816_COMPILE_H
#define AS816_COMPILE_H

#include <stdint.h>

/* operation numbers - the order of optable in vm/vm.s */
enum {
  OP_END, OP_LINE, OP_LDC, OP_LDV, OP_STV, OP_PUSH,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_NEG, OP_NOT, OP_AND, OP_OR, OP_REL,
  OP_PRNUM, OP_PRSTR, OP_PRCR, OP_PRCOMMA,
  OP_GOTO, OP_IFNOT, OP_GOSUB, OP_RETURN,
  OP_FORBEGIN, OP_FORLIM, OP_FORSTEP, OP_NEXT, OP_NEXT0,
  OP_IDX, OP_LDA, OP_STA, OP_DIM, OP_ERR, OP_FN,
  OP_SLIT, OP_SLDV, OP_SSTV, OP_SPUSH, OP_SCAT, OP_SREL, OP_SPRINT,
  OP_LEFT, OP_RIGHT, OP_MIDP, OP_MID2, OP_MID3, OP_LEN, OP_ASC, OP_CHR,
  OP_STR, OP_VAL, OP_FRE,
  OP_LDVI, OP_STVI, OP_LDAI, OP_STAI, OP_SLDA, OP_SSTA,
  OP_INPSTART, OP_READSTART, OP_GETSTART, OP_ITEM, OP_INPEND, OP_RESTORE,
  OP_ON, OP_POP, OP_DEFFN, OP_FNCALL, OP_FNRET, OP_POS, OP_PEEK, OP_ADR, OP_POKE,
  OP_WAIT, OP_CALL, OP_PRN, OP_INN, OP_HTAB, OP_VTAB, OP_HOME, OP_NORMAL,
  OP_INVERSE, OP_FLASH, OP_SPEED, OP_TRACE, OP_NOTRACE, OP_TEXT, OP_GR, OP_COLOR,
  OP_PLOT, OP_HLIN, OP_VLIN, OP_SCRN, OP_STOP, OP_CLEAR, OP_RUN, OP_ONERR,
  OP_RESUME, OP_STMT, OP_TAB, OP_SPC, OP_BYTEPUSH, OP_LOMEM, OP_HIMEM,
  OP_CONT,
  OP_HGR, OP_HGR2, OP_HCOLOR, OP_HX, OP_HPLOT, OP_HPLOTTO, OP_HPOSN, OP_SHAPE, OP_DRAW, OP_XDRAW,
  OP_ROT, OP_SCALE, OP_USR, OP_AMPER, OP_LIST, OP_NEW, OP_DEL,
  OP_COUNT
};

/* FN operands - the order of fntab in vm/vm.s */
enum { FN_SGN, FN_INT, FN_ABS, FN_SQR, FN_RND, FN_LOG, FN_EXP, FN_COS, FN_SIN, FN_TAN, FN_ATN };

/* AppleSoft error codes (offsets into its message table) */
enum {
  E_SYNTAX = 0x10, E_UNDEF_STATEMENT = 0x5A, E_BAD_SUBSCRIPT = 0x6B, E_TYPE_MISMATCH = 0xA3,
  E_FORMULA = 0xBF, E_ILLEGAL_DIRECT = 0x95, E_OUT_OF_MEMORY = 0x4D,
  E_UNIMPLEMENTED = 0xFE /* ours: a construct the compiler cannot do yet */
};

typedef struct Platform {
  void *ctx;
  /* the tokenized program as loaded at $0801: n bytes from address a */
  void (*read)(void *ctx, uint16_t a, uint8_t *dst, uint16_t n);
  /* n bytes into the VM bank at address a */
  void (*write)(void *ctx, uint16_t a, const uint8_t *src, uint16_t n);
  /* FIN: the number whose first character is text[0]; fills fac[7] (exp,
   * mantissa 4, sign, rounding byte) and returns how many characters FIN
   * took, or -1 for an error (FIN's ?OVERFLOW) */
  int (*fin)(void *ctx, const uint8_t *text, int len, uint8_t fac[7]);
  /* optional (0 = none): called at every statement, so a few milliseconds
   * apart at most - the shell's clock watches the video counter there */
  void (*poll)(void *ctx);
  /* from the VM image's header */
  uint16_t prog_start;          /* first free byte of the VM bank */
  const uint16_t *optable;      /* handler address of each operation */
} Platform;

typedef struct CompileStats {
  uint16_t code_size, lines, vars, arrays, consts, strings;
  uint16_t end;                 /* first byte after the program image */
} CompileStats;

/* Compile the program; returns 0, or -1 if it does not fit the bank. */
int as816_compile(const Platform *pf, CompileStats *st);

/* Compile a direct line (its tokens, ending in 0) against the last compile's
 * tables, into the top of the VM bank (vm_direct runs it from *entry): its
 * variables are the program's, new ones made there.  0, or -1 if it does
 * not fit (then nothing of it is kept). */
int as816_direct(const Platform *pf, const uint8_t *tok, uint16_t *entry);

/* The compiler's tables for direct lines, to the bytecode cache (save) or
 * back from it: io moves n bytes between p and the file, in order. */
void as816_state(int save, void (*io)(void *ctx, void *p, uint16_t n), void *ctx);

/* The last compile's variables (arrays_too: its arrays): how many; each
 * one's name (two characters, then the type: 0 real, 1 integer, 2 string)
 * and its slot - for an array, its dimensions and its descriptor. */
uint16_t as816_count(int arrays_too);
void as816_var(uint16_t i, uint8_t name[3], uint16_t *addr);
void as816_array(uint16_t i, uint8_t name[3], uint8_t *ndims, uint16_t *desc);
/* a variable (ndims 0) or array the program does not name, made for CHAIN
 * as a direct line makes one: its slot or descriptor, 0 if no room */
uint16_t as816_adopt(const Platform *pf, const uint8_t name[3], uint8_t ndims);

#endif
