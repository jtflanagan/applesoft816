/* parse.h - AppleSoft's tokenizer (parse.c). */
#ifndef AS816_PARSE_H
#define AS816_PARSE_H

#include <stdint.h>

/* PARSE.INPUT.LINE: buf is the input buffer ($0200's 256 bytes) holding
 * the typed line as 7-bit characters ending in 0; x0 is where parsing
 * starts (after a line number, as CHRGET left it).  The tokens are
 * written from buf[0], in place, ending in 0 (and buf[y - 3] = 0 too).
 * Returns the ROM's Y (EOL.PNTR): the line, tokens and its 0, is y - 4
 * bytes. */
uint8_t as816_parse(uint8_t *buf, uint8_t x0);

/* TOKEN.NAME.TABLE: keywords from token $80, last character | $80, 0 after */
const uint8_t *as816_token_table(void);

#endif
