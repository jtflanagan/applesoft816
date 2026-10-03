# fp816: AppleSoft floating point for the 65816

Bit-exact with the ROM: same 40-bit format, same FAC/ARG/rounding-byte
behaviour, same errors.  Native mode, any direct page, long operand
pointers, any M/X on entry (preserved).  See the header of `fp.s`.

**Stage 1 (now): a line-for-line port** of the ROM routines, so every
intermediate state matches; **stage 2**: 16-bit rewrites, held to the same
oracle.

    make test [CASES=n]     # fptest: fp816 vs the IIgs ROM 3's own routines

`fptest` runs both on emu816 - the ROM in emulation mode with FAC on zero
page, fp816 in native mode with FAC on a direct page at `$1000` - from the
same random states (exponents equal/near/far/edge, special mantissas, raw
sign bytes, random rounding bytes), and compares the whole FAC, the stored
bytes, the error outcome, and a byte result where there is one (FCOMP's A).
A quarter of the cases put the operand at or a hair from FAC's own value
(equal compares, rounding-byte ties, cancellation).  Mutation-checked: a
dropped borrow, an off-by-one exponent difference, a wrong rounding shift,
a misplaced FDIV quotient byte, a wrong FMULT add and FCOMP's rounding
compare each fail it.

| routine | status |
|---|---|
| load FAC / load ARG | ported, matches |
| store FAC rounded | ported, matches (incl. ?OVERFLOW on rounding) |
| FADD / FSUB / FADDT / FSUBT | ported, matches (incl. ?OVERFLOW) |
| FMULT / FDIV (incl. ?OVERFLOW, ?DIVISION BY ZERO) | ported, matches |
| FCOMP, SGN, ABS, MUL10, DIV10 | ported, matches |
| INT, QINT, AYINT, GIVAYF, CONINT, GETADR (incl. ?ILLEGAL QUANTITY) | ported, matches |
| FIN (with CHRGET/CHRGOT over a long text pointer), NEGOP | ported, matches (incl. ?OVERFLOW) |
| FOUT (number to text, into a caller's buffer) | ported, matches |
| functions: SQR ^ LOG EXP SIN COS TAN ATN RND (incl. RND's seed, SIN/TAN's quadrant flag) | ported, matches |

**Stage 1 is complete: every AppleSoft float routine, bit-exact** (29 test
families; `make test CASES=50000` runs in ~90 s).  Cost, cycles per call on
emu816 (valid cases): the port is the ROM plus 50-60 cycles of public-entry
overhead (P/DBR/S/D saved, JSL/RTL, long pointers) - e.g. FADD 922 vs 967,
FMULT 1276 vs 1336, FDIV 2204 vs 2370, SIN 16620 vs 17076.  Internal calls
do not pay it.  Speed is stage 2's job.

Two places where a transliteration would be wrong on the 65816: FDIV's
`sta $65,x` (X = $FD..$01) wraps in zero page on the 6502 but not in
native-mode dp,X, so it stores through a long pointer to the direct page;
and FADD's `lda $04,y` is absolute,Y (there is no zp,Y LDA), which ignores
D, so its two cases are written out.  ADD.EXPONENTS's `.byte $2C` skip (a
stray BIT read that can land on `$C010`) is a plain branch.

Every entry sets DBR to the library's bank (restored on exit, also after
an error), so constants and FOUT's decimal table are read absolute as in
the ROM.  FOUT writes where the ROM wrote `$0100` through a long pointer
(`FP_BUF`, standing for the ROM's `$00FF`).

The functions keep the ROM's constant and coefficient tables byte for byte,
including RND's two constants, which overlap each other and end on the
`$20` that starts RND's code; TEMP1/2/3 and the RND seed live at their
AppleSoft offsets in the direct page.

AYINT's -32768 is the ROM's constant at `$E0FE`, whose last byte is the
`$20` of the following JSR - a hair below -32768; the port keeps it.

ROM details the port keeps (they change result bits): FADD with equal
exponents adds one to the rounding byte (the exponent compare leaves carry
set); the sign byte is kept raw; whole-byte right shifts fill from `$A4`,
which is 0 except inside QINT.
