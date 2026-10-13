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
5. **Compile speed**: one pass instead of two, a FIN fast path for small
   integers.  *Considering (user, 2026-10-13):* compile lazily, a line (or
   a run of lines) the first time it is reached, so RUN starts at once and
   lines that never run are never compiled.  One pass is the prerequisite.

## Results

| Step | BM1 | BM3 | BM7 | BM8 | Lemonade | Compile |
|---|---|---|---|---|---|---|
| baseline | 1,207,813 | 7,270,951 | 21,660,330 | 9,622,831 | 65,084,540 | 14,761,369 |
| 1: round and store inline, NEXT's step inline | 974,567 | 6,634,845 | 19,103,224 | 9,575,091 | 59,242,343 | |
| 2: integer NEXT (fp_next, frame cache) | 560,784 | 6,634,845 | 18,082,224 | 9,575,091 | 48,866,171 | |
