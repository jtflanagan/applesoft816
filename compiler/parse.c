/* parse.c - AppleSoft's tokenizer, PARSE.INPUT.LINE ($D559 in the IIgs ROM
 * 3), ported step for step: the labels are the ROM's addresses, and every
 * quirk is kept (spaces dropped outside strings, REM and DATA, even inside
 * keywords; "AT" not taken before N or O; lower case raised outside strings,
 * REM and DATA, as the GS ROM does).  tests/parsetest checks it against the
 * ROM itself.
 */
#include "parse.h"

/* TOKEN.NAME.TABLE ($D0D0): each keyword's last character has bit 7 set;
 * the tokens are $80 on, in this order */
static const uint8_t toktab[] = {
  'E', 'N', 'D' | 0x80,                        /* $80 */
  'F', 'O', 'R' | 0x80,                        /* $81 */
  'N', 'E', 'X', 'T' | 0x80,                   /* $82 */
  'D', 'A', 'T', 'A' | 0x80,                   /* $83 */
  'I', 'N', 'P', 'U', 'T' | 0x80,              /* $84 */
  'D', 'E', 'L' | 0x80,                        /* $85 */
  'D', 'I', 'M' | 0x80,                        /* $86 */
  'R', 'E', 'A', 'D' | 0x80,                   /* $87 */
  'G', 'R' | 0x80,                             /* $88 */
  'T', 'E', 'X', 'T' | 0x80,                   /* $89 */
  'P', 'R', '#' | 0x80,                        /* $8A */
  'I', 'N', '#' | 0x80,                        /* $8B */
  'C', 'A', 'L', 'L' | 0x80,                   /* $8C */
  'P', 'L', 'O', 'T' | 0x80,                   /* $8D */
  'H', 'L', 'I', 'N' | 0x80,                   /* $8E */
  'V', 'L', 'I', 'N' | 0x80,                   /* $8F */
  'H', 'G', 'R', '2' | 0x80,                   /* $90 */
  'H', 'G', 'R' | 0x80,                        /* $91 */
  'H', 'C', 'O', 'L', 'O', 'R', '=' | 0x80,    /* $92 */
  'H', 'P', 'L', 'O', 'T' | 0x80,              /* $93 */
  'D', 'R', 'A', 'W' | 0x80,                   /* $94 */
  'X', 'D', 'R', 'A', 'W' | 0x80,              /* $95 */
  'H', 'T', 'A', 'B' | 0x80,                   /* $96 */
  'H', 'O', 'M', 'E' | 0x80,                   /* $97 */
  'R', 'O', 'T', '=' | 0x80,                   /* $98 */
  'S', 'C', 'A', 'L', 'E', '=' | 0x80,         /* $99 */
  'S', 'H', 'L', 'O', 'A', 'D' | 0x80,         /* $9A */
  'T', 'R', 'A', 'C', 'E' | 0x80,              /* $9B */
  'N', 'O', 'T', 'R', 'A', 'C', 'E' | 0x80,    /* $9C */
  'N', 'O', 'R', 'M', 'A', 'L' | 0x80,         /* $9D */
  'I', 'N', 'V', 'E', 'R', 'S', 'E' | 0x80,    /* $9E */
  'F', 'L', 'A', 'S', 'H' | 0x80,              /* $9F */
  'C', 'O', 'L', 'O', 'R', '=' | 0x80,         /* $A0 */
  'P', 'O', 'P' | 0x80,                        /* $A1 */
  'V', 'T', 'A', 'B' | 0x80,                   /* $A2 */
  'H', 'I', 'M', 'E', 'M', ':' | 0x80,         /* $A3 */
  'L', 'O', 'M', 'E', 'M', ':' | 0x80,         /* $A4 */
  'O', 'N', 'E', 'R', 'R' | 0x80,              /* $A5 */
  'R', 'E', 'S', 'U', 'M', 'E' | 0x80,         /* $A6 */
  'R', 'E', 'C', 'A', 'L', 'L' | 0x80,         /* $A7 */
  'S', 'T', 'O', 'R', 'E' | 0x80,              /* $A8 */
  'S', 'P', 'E', 'E', 'D', '=' | 0x80,         /* $A9 */
  'L', 'E', 'T' | 0x80,                        /* $AA */
  'G', 'O', 'T', 'O' | 0x80,                   /* $AB */
  'R', 'U', 'N' | 0x80,                        /* $AC */
  'I', 'F' | 0x80,                             /* $AD */
  'R', 'E', 'S', 'T', 'O', 'R', 'E' | 0x80,    /* $AE */
  '&' | 0x80,                                  /* $AF */
  'G', 'O', 'S', 'U', 'B' | 0x80,              /* $B0 */
  'R', 'E', 'T', 'U', 'R', 'N' | 0x80,         /* $B1 */
  'R', 'E', 'M' | 0x80,                        /* $B2 */
  'S', 'T', 'O', 'P' | 0x80,                   /* $B3 */
  'O', 'N' | 0x80,                             /* $B4 */
  'W', 'A', 'I', 'T' | 0x80,                   /* $B5 */
  'L', 'O', 'A', 'D' | 0x80,                   /* $B6 */
  'S', 'A', 'V', 'E' | 0x80,                   /* $B7 */
  'D', 'E', 'F' | 0x80,                        /* $B8 */
  'P', 'O', 'K', 'E' | 0x80,                   /* $B9 */
  'P', 'R', 'I', 'N', 'T' | 0x80,              /* $BA */
  'C', 'O', 'N', 'T' | 0x80,                   /* $BB */
  'L', 'I', 'S', 'T' | 0x80,                   /* $BC */
  'C', 'L', 'E', 'A', 'R' | 0x80,              /* $BD */
  'G', 'E', 'T' | 0x80,                        /* $BE */
  'N', 'E', 'W' | 0x80,                        /* $BF */
  'T', 'A', 'B', '(' | 0x80,                   /* $C0 */
  'T', 'O' | 0x80,                             /* $C1 */
  'F', 'N' | 0x80,                             /* $C2 */
  'S', 'P', 'C', '(' | 0x80,                   /* $C3 */
  'T', 'H', 'E', 'N' | 0x80,                   /* $C4 */
  'A', 'T' | 0x80,                             /* $C5 */
  'N', 'O', 'T' | 0x80,                        /* $C6 */
  'S', 'T', 'E', 'P' | 0x80,                   /* $C7 */
  '+' | 0x80,                                  /* $C8 */
  '-' | 0x80,                                  /* $C9 */
  '*' | 0x80,                                  /* $CA */
  '/' | 0x80,                                  /* $CB */
  '^' | 0x80,                                  /* $CC */
  'A', 'N', 'D' | 0x80,                        /* $CD */
  'O', 'R' | 0x80,                             /* $CE */
  '>' | 0x80,                                  /* $CF */
  '=' | 0x80,                                  /* $D0 */
  '<' | 0x80,                                  /* $D1 */
  'S', 'G', 'N' | 0x80,                        /* $D2 */
  'I', 'N', 'T' | 0x80,                        /* $D3 */
  'A', 'B', 'S' | 0x80,                        /* $D4 */
  'U', 'S', 'R' | 0x80,                        /* $D5 */
  'F', 'R', 'E' | 0x80,                        /* $D6 */
  'S', 'C', 'R', 'N', '(' | 0x80,              /* $D7 */
  'P', 'D', 'L' | 0x80,                        /* $D8 */
  'P', 'O', 'S' | 0x80,                        /* $D9 */
  'S', 'Q', 'R' | 0x80,                        /* $DA */
  'R', 'N', 'D' | 0x80,                        /* $DB */
  'L', 'O', 'G' | 0x80,                        /* $DC */
  'E', 'X', 'P' | 0x80,                        /* $DD */
  'C', 'O', 'S' | 0x80,                        /* $DE */
  'S', 'I', 'N' | 0x80,                        /* $DF */
  'T', 'A', 'N' | 0x80,                        /* $E0 */
  'A', 'T', 'N' | 0x80,                        /* $E1 */
  'P', 'E', 'E', 'K' | 0x80,                   /* $E2 */
  'L', 'E', 'N' | 0x80,                        /* $E3 */
  'S', 'T', 'R', '$' | 0x80,                   /* $E4 */
  'V', 'A', 'L' | 0x80,                        /* $E5 */
  'A', 'S', 'C' | 0x80,                        /* $E6 */
  'C', 'H', 'R', '$' | 0x80,                   /* $E7 */
  'L', 'E', 'F', 'T', '$' | 0x80,              /* $E8 */
  'R', 'I', 'G', 'H', 'T', '$' | 0x80,         /* $E9 */
  'M', 'I', 'D', '$' | 0x80,                   /* $EA */
  0
};

const uint8_t *as816_token_table(void) { return toktab; }

static uint8_t *b;                  /* INPUT.BUFFER ($0200) */
static uint8_t endchr, dataflg;     /* ENDCHR ($0E), DATAFLG ($13) */

static uint8_t up(uint8_t c) { return c >= 0x61 ? (uint8_t)(c & 0x5F) : c; }

/* $D8B5: the character at x, raised unless in a string, a REM or DATA */
static uint8_t get(uint8_t x)
{
  if (endchr == 0 || endchr == '"' || dataflg == 0x49) return b[x];
  return up(b[x]);
}

#define OUT(y) b[(uint8_t)((y) - 5)]   /* the ROM stores at $01FB,Y */

uint8_t as816_parse(uint8_t *buf, uint8_t x0)
{
  uint8_t x, y, serpnt = 0, eol = 0, start = 0, a, c;
  uint16_t t = 0;                   /* (DSCTMP),Y: the keyword table */
  b = buf;
  x = (uint8_t)(x0 - 1);
  y = 4;
  dataflg = 4;
  endchr = 0xFF;                    /* (whatever it was: not 0 or '"' here) */
parse:                              /* $D56C */
  x++;
d56d:
  c = get(x);
  if (!(dataflg & 0x40) && c == ' ') goto parse;
  endchr = c;                       /* $D578 */
  if (c == '"') { a = c; goto d5f2; }
  if (dataflg & 0x40) { a = c; goto d5cd; }
  if (c == '?') { a = 0xBA; goto d5cd; }
  if (c >= '0' && c < '<') { a = c; goto d5cd; }
  serpnt = y;                       /* $D590: try every keyword from here */
  eol = 0;
  start = x;
  x--;
  t = 0xFFFF;
d5a2:
  t++;
d5a7:
  x++;
d5a8:
  c = get(x);
  if (c == ' ') goto d5a7;
  a = (uint8_t)(c - toktab[t]);
  if (a == 0) goto d5a2;
  if (a != 0x80) goto d5f9;
  a = (uint8_t)(0x80 | eol);        /* the whole keyword: its token */
  if (a == 0xC5) {                  /* AT, unless ATN or A TO */
    c = up(b[(uint8_t)(x + 1)]);
    if (c == 'N' || c == 'O') goto d5f9;
  }
d5cb:
  y = serpnt;
d5cd:
  x++;
  y++;
  OUT(y) = a;
  if (a == 0) goto d610;
  a = (uint8_t)(a - 0x3A);
  if (a == 0 || a == 0x49) dataflg = a;     /* ':' ends a DATA, DATA ($83) starts one */
  if ((uint8_t)(a - 0x78) != 0) goto d56d;  /* REM ($B2): the rest of the line as it is */
  endchr = 0;
d5e9:
  c = get(x);
  if (c == 0) { a = 0; goto d5cd; }
  if (c == endchr) { a = c; goto d5cd; }
  a = c;
d5f2:
  y++;
  OUT(y) = a;
  x++;
  goto d5e9;
d5f9:                               /* not this keyword: the next one, from the start */
  x = start;
  eol++;
  do a = toktab[t++]; while (!(a & 0x80));
  if (toktab[t]) goto d5a8;
  a = up(b[x]);                     /* no keyword: the character itself */
  goto d5cb;
d610:
  OUT(y + 2) = 0;
  return y;
}
