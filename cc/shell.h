/* shell.h - the shell (shell.c) and what it uses of main.c and glue.s. */
#ifndef AS816_SHELL_H
#define AS816_SHELL_H

#include <stdint.h>

#include "compile.h"

void shell(void);
void shell_reset(void);                /* Ctrl-Reset: back to the prompt (sys.s x_reset) */

/* main_sh.c */
extern uint32_t text_base, vm_base, xfer_base, stage_base;
extern uint8_t vm_bank, cc_bank, cc_data;
extern uint16_t vm_dp;

/* cccall.c: the compiler image's services */
void cc_init(void);
int compile(CompileStats *st, uint32_t *clk);
int compile_direct(const uint8_t *tok, uint16_t *entry);
uint16_t compile_adopt(const uint8_t name[3], uint8_t ndims);
void cc_tables(uint16_t *vars, uint16_t *arrays);
void cc_entry(uint16_t i, uint8_t name[3], uint8_t *ndims, uint16_t *slot);
extern uint8_t cc_busy;

/* glue.s */
extern uint32_t xfer_far, xfer_far2;   /* one below the first byte */
extern uint16_t xfer_near, xfer_n;
void far_in(void);                     /* xfer_far -> xfer_near */
void far_out(void);                    /* xfer_near -> xfer_far */
void far_move(void);                   /* xfer_far -> xfer_far2, last byte first */
void far_move_up(void);                /* ... first byte first */
uint16_t fix_links(void);              /* FIX.LINKS over the text bank; the end's address */
uint8_t peek0(uint16_t a);
void poke0(uint16_t a, uint8_t v);
uint8_t mli_call(uint8_t cmd);
uint8_t rom_call(uint16_t addr);
uint8_t vm_call(uint16_t entry);
void cc_call(uint8_t warm);            /* the compiler image (cccall.c) */
void vm_hook(void);                    /* the VM's CHR$(4) commands come to vm_command */
uint8_t vm_command(void);       /* vm_run ($0000) or vm_direct; VM_ERR */

#endif
