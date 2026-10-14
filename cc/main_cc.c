/* main_cc.c - the compiler image (AS816.CC; gsrun --cc): the compiler with
 * its platform (compile.h) over the banks it is given, and the requests the
 * shell (main_sh.c, cccall.c) makes of it.  cc_params is the caller's block
 * (its address is in the code bank's header); cc_main is crt0's call.
 *
 * Requests (cc_params.req): 0 compile once (gsrun: cold, the parameters
 * filled in by it), 1 compile, timed by the video counter, 2 a direct
 * line, 3 adopt a variable (CHAIN), 4 the variables' table into the stage
 * bank, 5 lay the program out for a lazy compile (timed as 1), 6 compile
 * from line-table entry n1, below n2 (the VM's request 7).  After the
 * first, the shell calls warm: the tables stay. */
#include <stdint.h>

#include "compile.h"

uint8_t peek24(uint32_t addr);
int16_t fin_call(uint16_t text, uint8_t *fac);

struct CcParams {
  uint8_t vm_bank;      /* +0  in: the VM image's bank */
  uint8_t text_bank;    /* +1  in: the tokenized program at $0801 of this bank */
  uint16_t vm_dp;       /* +2  in: the VM's direct page (bank 0) */
  int16_t result;       /* +4  out */
  CompileStats st;      /* +6  out (14 bytes) */
  uint8_t req;          /* +20 in: the request */
  uint8_t stage_bank;   /* +21 in: a bank for the variables' table (request 4) */
  uint32_t clk;         /* +22 out: request 1's time, in half-lines of the video counter */
  uint16_t n1, n2;      /* +26 +28: out: direct line's entry, adopt's slot, counts */
  uint8_t name[3];      /* +30 in: adopt's variable */
  uint8_t ndims;        /* +33 in */
  uint8_t tok[256];     /* +34 in: the direct line's tokens */
};
/* in .noinit: crt0 clears .bss after the caller has filled this in */
__attribute__((section(".noinit.cc_params"))) struct CcParams cc_params;

/* for glue.s, glue_cc.s */
uint8_t vm_bank;
uint16_t vm_dp, fp_fin_addr;
uint32_t xfer_far, xfer_far2;   /* far_in/far_out/far_move: one below the first byte */
uint16_t xfer_near, xfer_n;
void far_in(void);
void far_out(void);

static uint16_t optab[OP_COUNT];
static uint32_t text_base, vm_base, stage_base;
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

/* ---- the clock: half-lines of the GS video counter ($C02E, the vertical
 * count over two, $7D-$FF), read at every statement (well under a frame
 * apart) ---- */
static uint32_t clk_half;
static uint8_t clk_last;
static void clk_poll(void *ctx)
{
  uint8_t v = peek24(0xC02E), d = (uint8_t)(v - clk_last);
  (void)ctx;
  if (v < clk_last) d = (uint8_t)(d - 125);     /* past $FF: on from $7D */
  clk_half += d;
  clk_last = v;
}

/* ---- the stage bank, for the variables' table (request 4) ---- */
static uint16_t stage_at;
static void stage_out(void *p, uint16_t n)
{
  xfer_far = (stage_base | stage_at) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  far_out();
  stage_at = (uint16_t)(stage_at + n);
}

int cc_main(void)
{
  uint16_t i, nops, tab, a;
  uint8_t e[6];
  vm_bank = cc_params.vm_bank;
  vm_dp = cc_params.vm_dp;
  vm_base = (uint32_t)vm_bank << 16;
  text_base = (uint32_t)cc_params.text_bank << 16;
  stage_base = (uint32_t)cc_params.stage_bank << 16;
  nops = vm_word(0x0A);
  if (nops != OP_COUNT) { cc_params.result = -2; cc_params.st.lines = nops; cc_params.st.code_size = vm_word(4); return 0; }
  tab = vm_word(0x0C);
  for (i = 0; i < OP_COUNT; i++) optab[i] = vm_word((uint16_t)(tab + 2 * i));
  fp_fin_addr = (uint16_t)(vm_word(0x16) + 0x50);
  pf.ctx = 0, pf.read = pf_read, pf.write = pf_write, pf.fin = pf_fin, pf.poll = 0;
  pf.prog_start = vm_word(0x0E);
  pf.text_bank = cc_params.text_bank;
  pf.optable = optab;
  switch (cc_params.req) {
  case 0:
    cc_params.result = (int16_t)as816_compile(&pf, &cc_params.st);
    break;
  case 1:
    clk_half = 0, clk_last = peek24(0xC02E);
    pf.poll = clk_poll;
    cc_params.result = (int16_t)as816_compile(&pf, &cc_params.st);
    clk_poll(0);
    cc_params.clk = clk_half;
    break;
  case 5:
    clk_half = 0, clk_last = peek24(0xC02E);
    pf.poll = clk_poll;
    cc_params.result = (int16_t)as816_start(&pf, &cc_params.st);
    clk_poll(0);
    cc_params.clk = clk_half;
    break;
  case 6:
    cc_params.result = (int16_t)as816_more(&pf, cc_params.n1, cc_params.n2);
    break;
  case 2:
    cc_params.result = (int16_t)as816_direct(&pf, cc_params.tok, &cc_params.n1);
    break;
  case 3:
    cc_params.n1 = as816_adopt(&pf, cc_params.name, cc_params.ndims);
    break;
  case 4:                                 /* name[3], ndims (0: a scalar), slot/descriptor */
    stage_at = 0;
    cc_params.n1 = as816_count(0), cc_params.n2 = as816_count(1);
    for (i = 0; i < cc_params.n1; i++) {
      as816_var(i, e, &a);
      e[3] = 0, e[4] = (uint8_t)a, e[5] = (uint8_t)(a >> 8);
      stage_out(e, 6);
    }
    for (i = 0; i < cc_params.n2; i++) {
      as816_array(i, e, &e[3], &a);
      e[4] = (uint8_t)a, e[5] = (uint8_t)(a >> 8);
      stage_out(e, 6);
    }
    break;
  }
  return 0;
}
