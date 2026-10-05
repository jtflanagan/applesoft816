/* cccall.c - the shell's calls into the compiler image (main_cc.c): its
 * parameter block in its data bank, written and read across banks; its
 * tables through the stage bank. */
#include <stdint.h>
#include <string.h>

#pragma clang attribute push (__attribute__((minsize)), apply_to = function)

#include "shell.h"

/* the compiler image's parameter block (main_cc.c, struct CcParams) */
#define CCP_VM_BANK 0
#define CCP_TEXT_BANK 1
#define CCP_VM_DP 2
#define CCP_RESULT 4
#define CCP_ST 6
#define CCP_REQ 20
#define CCP_STAGE 21
#define CCP_CLK 22
#define CCP_N1 26
#define CCP_N2 28
#define CCP_NAME 30
#define CCP_NDIMS 33
#define CCP_TOK 34

static uint32_t ccp;                /* the block, long */
static uint8_t warm;
static uint16_t nv, na;             /* the last table's counts (request 4) */

static void put(uint16_t at, const void *p, uint16_t n)
{
  xfer_far = (ccp + at) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  far_out();
}
static void get(void *p, uint16_t at, uint16_t n)
{
  xfer_far = (ccp + at) - 1, xfer_near = (uint16_t)((uint16_t)p - 1), xfer_n = n;
  far_in();
}

void cc_init(void)
{
  uint16_t a;
  uint8_t b[4];
  xfer_far = ((uint32_t)cc_bank << 16 | 8) - 1, xfer_near = (uint16_t)((uint16_t)&a - 1), xfer_n = 2;
  far_in();                                     /* the header's +8: the block's address */
  ccp = (uint32_t)cc_data << 16 | a;
  b[0] = vm_bank, b[1] = (uint8_t)(text_base >> 16), b[2] = (uint8_t)vm_dp, b[3] = (uint8_t)(vm_dp >> 8);
  put(CCP_VM_BANK, b, 4);
  b[0] = (uint8_t)(stage_base >> 16);
  put(CCP_STAGE, b, 1);
}

uint8_t cc_busy;                /* inside the compiler: a Ctrl-Reset here leaves its tables half made */

static int16_t req(uint8_t r)
{
  int16_t res;
  put(CCP_REQ, &r, 1);
  cc_busy = 1;
  cc_call(warm);
  cc_busy = 0;
  warm = 1;
  get(&res, CCP_RESULT, 2);
  return res;
}
static uint16_t n1(void)
{
  uint16_t v;
  get(&v, CCP_N1, 2);
  return v;
}

int compile(CompileStats *st, uint32_t *clk)
{
  int16_t r = req(1);
  get(st, CCP_ST, sizeof *st);
  get(clk, CCP_CLK, 4);
  return r;
}

int compile_direct(const uint8_t *tok, uint16_t *entry)
{
  uint16_t n = 0;
  while (tok[n] && n < 255) n++;
  put(CCP_TOK, tok, (uint16_t)(n + 1));
  if (req(2)) return -1;
  *entry = n1();
  return 0;
}

uint16_t compile_adopt(const uint8_t name[3], uint8_t ndims)
{
  put(CCP_NAME, name, 3);
  put(CCP_NDIMS, &ndims, 1);
  req(3);
  return n1();
}

/* the variables' table (request 4) into the stage bank: entry i is 6 bytes,
 * the scalars then the arrays: name[3], dimensions (0: a scalar), slot */
void cc_tables(uint16_t *vars, uint16_t *arrays)
{
  req(4);
  get(&nv, CCP_N1, 2);
  get(&na, CCP_N2, 2);
  *vars = nv, *arrays = na;
}
void cc_entry(uint16_t i, uint8_t name[3], uint8_t *ndims, uint16_t *slot)
{
  uint8_t e[6];
  xfer_far = (stage_base | (uint16_t)(6 * i)) - 1, xfer_near = (uint16_t)((uint16_t)e - 1), xfer_n = 6;
  far_in();
  memcpy(name, e, 3);
  *ndims = e[3];
  *slot = (uint16_t)(e[4] | e[5] << 8);
}

/* the compiler's tables to the stage bank (its length) and back (0 or -1) */
uint16_t cc_state_save(void)
{
  req(5);
  return n1();
}
int cc_state_load(uint16_t len)
{
  put(CCP_N1, &len, 2);
  return req(6);
}

#pragma clang attribute pop
