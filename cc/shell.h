/* shell.h - the shell (shell.c) and what it uses of main.c and glue.s. */
#ifndef AS816_SHELL_H
#define AS816_SHELL_H

#include <stdint.h>

#include "compile.h"

void shell(void);

/* main.c */
int compile(CompileStats *st, void (*poll)(void *));
extern uint32_t text_base, vm_base;
extern uint8_t vm_bank;
extern uint16_t vm_dp;

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
uint8_t vm_call(void);
extern uint16_t hash_acc;
void far_hash(void);                   /* hash_acc over xfer_n bytes at xfer_far */

#endif
