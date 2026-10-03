/* main.c - the compiler on the 65816: its platform (compile.h) over the
 * banks the loader gave it.  cc_params is the loader's block (its address
 * is in the code bank's header); cc_main is crt0's call: one compile of the
 * text bank's program (mode 0, gsrun), or the shell (mode 1, AS816.SYSTEM). */
#include <stdint.h>

/* the shell runs between programs: small before fast (the compiler, -Os) */
#pragma clang attribute push (__attribute__((minsize)), apply_to = function)

#include "compile.h"
#include "shell.h"

uint8_t peek24(uint32_t addr);
int16_t fin_call(uint16_t text, uint8_t *fac);

struct CcParams {
  uint8_t vm_bank;      /* in: the VM image's bank */
  uint8_t text_bank;    /* in: the tokenized program at $0801 of this bank */
  uint16_t vm_dp;       /* in: the VM's direct page (bank 0) */
  int16_t result;       /* out: as816_compile's */
  CompileStats st;      /* out */
  uint8_t mode;         /* in: 0 compile once, 1 the shell */
  uint8_t str_bank;     /* in: the string heap's bank (the shell) */
  uint16_t user_id;     /* in: the Memory Manager's ID for us (the shell) */
};
/* in .noinit: crt0 clears .bss after the loader has filled this in */
__attribute__((section(".noinit.cc_params"))) struct CcParams cc_params;

/* for glue.s */
uint8_t vm_bank;
uint16_t vm_dp, fp_fin_addr;
uint32_t xfer_far, xfer_far2;   /* far_in/far_out/far_move: one below the first byte */
uint16_t xfer_near, xfer_n;
uint16_t hash_acc;              /* far_hash */
uint16_t vm_entry;              /* vm_call's */
void far_in(void);
void far_out(void);

static uint16_t optab[OP_COUNT];
uint32_t text_base, vm_base;
static Platform pf;

static void pf_read(void *ctx, uint16_t a, uint8_t *dst, uint16_t n)
{
  (void)ctx;
  if (!n) return;
  xfer_far = (text_base | a) - 1, xfer_near = (uint16_t)((uint16_t)dst - 1), xfer_n = n;
  far_in();
}
static void pf_write(void *ctx, uint16_t a, const uint8_t *src, uint16_t n)
{
  (void)ctx;
  if (!n) return;
  xfer_far = (vm_base | a) - 1, xfer_near = (uint16_t)((uint16_t)src - 1), xfer_n = n;
  far_out();
}
static int pf_fin(void *ctx, const uint8_t *text, int len, uint8_t fac[7])
{
  (void)ctx, (void)len;
  return fin_call((uint16_t)text, fac);
}

static uint16_t vm_word(uint16_t a) { return (uint16_t)(peek24(vm_base | a) | peek24(vm_base | (uint16_t)(a + 1)) << 8); }

/* compile the text bank's program into the VM bank */
int compile(CompileStats *st, void (*poll)(void *))
{
  pf.poll = poll;
  return as816_compile(&pf, st);
}

/* compile a direct line against the program's tables */
int compile_direct(const uint8_t *tok, uint16_t *entry)
{
  return as816_direct(&pf, tok, entry);
}

int cc_main(void)
{
  uint16_t i, nops, tab;
  vm_bank = cc_params.vm_bank;
  vm_dp = cc_params.vm_dp;
  vm_base = (uint32_t)vm_bank << 16;
  text_base = (uint32_t)cc_params.text_bank << 16;
  nops = vm_word(0x0A);
  if (nops != OP_COUNT) { cc_params.result = -2; cc_params.st.lines = nops; cc_params.st.code_size = vm_word(4); return 0; }
  tab = vm_word(0x0C);
  for (i = 0; i < OP_COUNT; i++) optab[i] = vm_word((uint16_t)(tab + 2 * i));
  fp_fin_addr = (uint16_t)(vm_word(0x16) + 0x50);
  pf.ctx = 0, pf.read = pf_read, pf.write = pf_write, pf.fin = pf_fin;
  pf.prog_start = vm_word(0x0E);
  pf.optable = optab;
  if (cc_params.mode == 1) {
    shell();
    return 0;
  }
  cc_params.result = (int16_t)compile(&cc_params.st, 0);
  return 0;
}

#pragma clang attribute pop
