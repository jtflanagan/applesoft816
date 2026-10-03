/* main.c - the compiler on the 65816: its platform (compile.h) over the
 * banks the loader gave it.  cc_params is the loader's block (its address
 * is in the code bank's header); cc_main is crt0's call. */
#include <stdint.h>

#include "compile.h"

uint8_t peek24(uint32_t addr);
int16_t fin_call(uint16_t text, uint8_t *fac);

struct CcParams {
  uint8_t vm_bank;      /* in: the VM image's bank */
  uint8_t text_bank;    /* in: the tokenized program at $0801 of this bank */
  uint16_t vm_dp;       /* in: the VM's direct page (bank 0) */
  int16_t result;       /* out: as816_compile's */
  CompileStats st;      /* out */
};
/* in .noinit: crt0 clears .bss after the loader has filled this in */
__attribute__((section(".noinit.cc_params"))) struct CcParams cc_params;

/* for glue.s */
uint8_t vm_bank;
uint16_t vm_dp, fp_fin_addr;
uint32_t xfer_far;              /* far_in/far_out: both one below the first byte */
uint16_t xfer_near, xfer_n;
void far_in(void);
void far_out(void);

static uint16_t optab[OP_COUNT];
static uint32_t text_base, vm_base;

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

int cc_main(void)
{
  Platform pf;
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
  cc_params.result = (int16_t)as816_compile(&pf, &cc_params.st);
  return 0;
}
