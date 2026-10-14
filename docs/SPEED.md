# Speed

The missing-features list is done (M5), so speed is the work now *(user,
2026-10-13)*.  Every step stays bit-exact: `tests/all.sh` (52 programs, the
GS ROM's AppleSoft as the oracle) and, for fp816, `make -C fp816 test`
(fptest against the ROM's own routines).  A step that changes a result is
wrong, whatever it saves.

Cycles are emu816's (`tests/all.sh`, `gsrun --cc`).  `tools/vmprof.py`
charges a run's cycles to the VM's labels (`GSRUN_HIST=FILE`, then
`tools/vmprof.py FILE`).

## Baseline (b41c50e)

| Program | ROM | VM | Speed-up |
|---|---|---|---|
| BM1 | 1,244,472 | 1,207,813 | 1.03x |
| BM2 | 8,457,259 | 1,972,825 | 4.29x |
| BM3 | 16,085,479 | 7,270,951 | 2.21x |
| BM4 | 17,933,811 | 7,583,864 | 2.36x |
| BM5 | 19,327,811 | 7,805,864 | 2.48x |
| BM6 | 28,775,465 | 14,397,330 | 2.00x |
| BM7 | 45,422,465 | 21,660,330 | 2.10x |
| BM8 | 10,466,027 | 9,622,831 | 1.09x |
| Lemonade | 84,039,008 | 65,084,540 | 1.29x |

Compiling Lemonade: 14,761,369 cycles.

## Where the time goes (baseline)

- **NEXT** (`next_common`): 21% of BM1, 10% of Lemonade, 6% of BM7.  Four
  fp816 calls per iteration (load the step, FADD, store rounded, FCOMP), each
  with its own entry and exit.
- **fp816's public entry and exit**: about 70 cycles a call (the VM's `FP`
  macro, `ENTER`, `LEAVE`), charged to the `fp_*` labels: ~10% of BM7.
- **fp816's 8-bit loops**: the multiply's bit loop is 60% of BM8; the divide
  loop, the alignment shifts (`shift_right_*`), packing and unpacking take
  most of the rest of BM3-BM7.
- **The expression stack**: `op_push`, `poparg`, `op_ldv`, `op_ldc`: ~15% of
  BM3 and BM7.
- **Subscripts**: `mul16` and `elem_addr` ~7% of BM7.
- Lemonade: 45% is its own 6502 music routine (`CALL`, bank 0), which must
  keep its timing.  Most of the rest is FOR/NEXT delay loops.

## Plan

1. **Cheaper fp816 calls from the VM**: near entries (JSR, no DBR switch,
   the direct-page pointer set once), and the trivial cases inline in the VM
   (rounding without a carry, packing a store).
2. **NEXT**: one pass with the step loaded inline, then an exact integer fast
   path when the variable, the step and the limit are whole numbers in range.
3. **fp816 stage 2**: 16-bit versions of the hot internals (alignment
   shift, add, normalize, multiply, divide), held to fptest.
4. **The expression stack and subscripts**: fused operations from the
   compiler, `mul16` out of one-dimension subscripts.
5. **Compile speed** *(user, 2026-10-13: single pass, then lazy)*: DONE -
   one pass after a counting scan, then a lazy compile (RUN lays the
   program out; a run of 16 lines compiles when control first reaches its
   first line).  Left: a FIN fast path for small integers (FIN is 17% of a
   compile), MVN for the far reads, the per-line C in the scan.

## Results

| Step | BM1 | BM3 | BM7 | BM8 | Lemonade | Compile |
|---|---|---|---|---|---|---|
| baseline | 1,207,813 | 7,270,951 | 21,660,330 | 9,622,831 | 65,084,540 | 14,761,369 |
| 1: round and store inline, NEXT's step inline | 974,567 | 6,634,845 | 19,103,224 | 9,575,091 | 59,242,343 | |
| 2: integer NEXT (fp_next, frame cache) | 560,784 | 6,634,845 | 18,082,224 | 9,575,091 | 48,866,171 | |
| 3: READ from the text; one-pass compile | 560,802 | 6,624,870 | 18,055,242 | 9,573,416 | 49,703,940 | 10,625,980 |
| 4: lazy compile (RUN: scan + layout; lines compile when reached) | 560,914 | 6,631,982 | 18,068,884 | 9,574,228 | 49,710,745 | 2,213,024 at RUN (cold call) |
| 5: the scan's text in windows, its byte skipping in assembly | 560,914 | 6,631,982 | 18,068,884 | 9,574,228 | 49,710,745 | 1,818,284 at RUN (cold call; ~1.43M warm), 9,908,565 for COMPILE |
| 6: FMULT by quarter squares (fp816 stage 2) | 560,914 | 6,215,854 | 17,767,884 | 6,944,742 | 49,438,014 | |
| 7: FDIV's loop in 16-bit halves | 560,914 | 5,943,854 | 17,452,526 | 6,804,628 | 49,396,078 | |
| 8: PUSH + load + operation fused (ARGV/ARGK/ARGKI, the A forms) | 560,914 | 5,421,854 | 16,930,526 | 6,778,528 | 49,311,563 | |
| 9: subscripts: fp_ayint16 (fast AYINT), one-dimension element address by shifts | 560,914 | 5,421,854 | 13,448,210 | 6,778,528 | 49,122,115 | |
| 10: FOUT's digit loop and MAF in 16-bit | 560,914 | 5,421,854 | 13,448,210 | 6,756,528 | 48,996,873 | |
| 11: NEXT's usual case (own frame on top, cached) without find_for; packing via the direct page | 428,002 | 5,421,854 | 12,871,210 | 6,756,528 | 45,810,081 | |
| 12: FIN's plain whole numbers (fin_fast) | 428,002 | 5,421,854 | 12,871,210 | 6,756,528 | 45,281,111 | 9,215,216 COMPILE |
