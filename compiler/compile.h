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
  OP_COUNT
};

/* FN operands - the order of fntab in vm/vm.s */
enum { FN_SGN, FN_INT, FN_ABS, FN_SQR, FN_RND, FN_LOG, FN_EXP, FN_COS, FN_SIN, FN_TAN, FN_ATN };

/* AppleSoft error codes (offsets into its message table) */
enum {
  E_SYNTAX = 0x10, E_UNDEF_STATEMENT = 0x5A, E_BAD_SUBSCRIPT = 0x6B, E_TYPE_MISMATCH = 0xA3,
  E_UNIMPLEMENTED = 0xFE /* ours: a construct the compiler cannot do yet */
};

typedef struct Platform {
  void *ctx;
  /* the tokenized program as loaded at $0801: its byte at address a */
  uint8_t (*prog)(void *ctx, uint16_t a);
  /* write a byte of the VM bank */
  void (*wr)(void *ctx, uint16_t a, uint8_t v);
  /* FIN: the number whose first character is text[0]; fills fac[7] (exp,
   * mantissa 4, sign, rounding byte) and returns how many characters FIN
   * took, or -1 for an error (FIN's ?OVERFLOW) */
  int (*fin)(void *ctx, const uint8_t *text, int len, uint8_t fac[7]);
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

#endif
