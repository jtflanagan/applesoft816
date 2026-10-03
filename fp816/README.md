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
bytes, and the error outcome.  Mutation-checked: a dropped borrow, an
off-by-one exponent difference and a wrong rounding shift each fail it.

| routine | status |
|---|---|
| load FAC / load ARG | ported, matches |
| store FAC rounded | ported, matches (incl. ?OVERFLOW on rounding) |
| FADD / FSUB / FADDT / FSUBT | ported, matches (incl. ?OVERFLOW) |
| FMULT, FDIV, FCOMP, INT/QINT, FIN, FOUT, functions | to do |

ROM details the port keeps (they change result bits): FADD with equal
exponents adds one to the rounding byte (the exponent compare leaves carry
set); the sign byte is kept raw; whole-byte right shifts fill from `$A4`,
which is 0 except inside QINT.
