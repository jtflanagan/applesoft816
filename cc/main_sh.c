/* main_sh.c - the shell image (AS816.SH): the loader's block (its address is
 * in the code bank's header), the banks it names, then the shell. */
#include <stdint.h>

/* the shell runs between programs: small before fast */
#pragma clang attribute push (__attribute__((minsize)), apply_to = function)

#include "dos.h"
#include "machine.h"
#include "shell.h"

struct ShParams {
  uint8_t vm_bank;      /* +0  the VM image's bank */
  uint8_t text_bank;    /* +1  the program text's ($0801) */
  uint16_t vm_dp;       /* +2  the VM's direct page (bank 0) */
  uint8_t str_bank;     /* +4  the string heap's */
  uint8_t xfer_bank;    /* +5  CHAIN's */
  uint8_t stage_bank;   /* +6  the variables' table (STORE, CHAIN) */
  uint8_t cc_bank;      /* +7  the compiler image's code bank */
  uint8_t cc_data;      /* +8  ... and data bank */
  uint16_t user_id;     /* +9  the Memory Manager's ID for us */
  uint16_t launch;      /* +11 out: a SYS file's length in the CHAIN bank, to run (0: QUIT) */
};
/* (crt0 names it cc_params in either image) */
__attribute__((section(".noinit.cc_params"))) struct ShParams cc_params;

/* for glue.s, glue_sh.s */
uint8_t vm_bank, cc_bank, cc_data;
uint16_t vm_dp;
uint32_t xfer_far, xfer_far2;   /* far_in/far_out/far_move: one below the first byte */
uint16_t xfer_near, xfer_n;
uint16_t vm_entry;              /* vm_call's */

uint32_t text_base, vm_base, xfer_base, stage_base;

int cc_main(void)
{
  vm_bank = cc_params.vm_bank;
  vm_dp = cc_params.vm_dp;
  vm_base = (uint32_t)vm_bank << 16;
  text_base = (uint32_t)cc_params.text_bank << 16;
  xfer_base = (uint32_t)cc_params.xfer_bank << 16;
  stage_base = (uint32_t)cc_params.stage_bank << 16;
  cc_bank = cc_params.cc_bank;
  cc_data = cc_params.cc_data;
  cc_init();
  if (peek0(RES_RESETF)) {               /* Ctrl-Reset brought us here (sys.s x_reset) */
    poke0(RES_RESETF, 0);
    shell_reset();
  } else
    shell();
  cc_params.launch = dos_launch;
  return 0;
}

#pragma clang attribute pop
