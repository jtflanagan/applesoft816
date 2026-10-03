#!/usr/bin/env python3
"""List a tokenized AppleSoft program (the bytes as they sit at $801).

  detok.py PROGRAM [BASE]     BASE defaults to $801 (the link addresses' origin)

Output is one line per program line, keywords spaced the way LIST spaces them
closely enough to read; it is a listing, not a round-trippable source.
"""
import sys

# Tokens $80-$EA, in ROM order (S-C DocuMentor: Applesoft, TOKEN.NAME.TABLE).
TOKENS = (
    'END FOR NEXT DATA INPUT DEL DIM READ GR TEXT PR# IN# CALL PLOT HLIN VLIN '
    'HGR2 HGR HCOLOR= HPLOT DRAW XDRAW HTAB HOME ROT= SCALE= SHLOAD TRACE NOTRACE '
    'NORMAL INVERSE FLASH COLOR= POP VTAB HIMEM: LOMEM: ONERR RESUME RECALL STORE '
    'SPEED= LET GOTO RUN IF RESTORE & GOSUB RETURN REM STOP ON WAIT LOAD SAVE DEF '
    'POKE PRINT CONT LIST CLEAR GET NEW TAB( TO FN SPC( THEN AT NOT STEP + - * / ^ '
    'AND OR > = < SGN INT ABS USR FRE SCRN( PDL( POS SQR RND LOG EXP COS SIN TAN '
    'ATN PEEK LEN STR$ VAL ASC CHR$ LEFT$ RIGHT$ MID$').split()
assert len(TOKENS) == 0xEB - 0x80


def lines(prog, base=0x801):
    i = 0
    while i + 4 <= len(prog):
        link = prog[i] | prog[i + 1] << 8
        if link == 0:
            return
        num = prog[i + 2] | prog[i + 3] << 8
        j, out = i + 4, []
        while prog[j]:
            b = prog[j]
            out.append(f' {TOKENS[b - 0x80]} ' if 0x80 <= b < 0xEB else chr(b & 0x7F))
            j += 1
        yield num, ' '.join(''.join(out).split(' ')).replace('  ', ' ').strip()
        i = link - base


def main(argv):
    if len(argv) not in (2, 3):
        raise SystemExit(__doc__)
    prog = open(argv[1], 'rb').read()
    base = int(argv[2].lstrip('$'), 16) if len(argv) == 3 else 0x801
    for num, text in lines(prog, base):
        print(num, text)


if __name__ == '__main__':
    main(sys.argv)
