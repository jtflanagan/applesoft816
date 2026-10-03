# Where ROM AppleSoft spends its time

Measured 2026-10-03 with `prof/asprof` (an emulated enhanced IIe running the
real ROM; see `prof/README.md`).  Emulated cycles at 1.0205 MHz; keyboard
waits excluded.

## The model is faithful

| BM | cycles | seconds | published (PCW, via Wikipedia) |
|---|---:|---:|---:|
| 1 | 1,225,547 | 1.20 | 1.3 |
| 2 | 8,440,332 | 8.27 | 8.5 |
| 3 | 16,072,552 | 15.75 | 16.0 |
| 4 | 17,920,884 | 17.56 | 17.8 |
| 5 | 19,332,854 | 18.95 | 19.1 |
| 6 | 28,818,447 | 28.24 | 28.6 |
| 7 | 45,465,447 | 44.55 | 44.8 |
| 8 | 10,465,370 | 10.26 | 55.5 |

BM1-BM7 land 0.1-0.3 s under the stopwatch figures, as stopwatch timing
would.  BM8 does not match: the published listing loops to 100 and runs here
in 10.3 s; 55.5 s fits neither 100 nor 1000 iterations, so the published
figure (or its listing) is suspect.  Not chased further.

The ROM is the enhanced IIe's.  Checked against the IIgs ROM 3 (bank `$FF`,
`strudelworks-host/assets/apple2gs_rom3.bin`): **the float arithmetic, FIN,
FOUT and math functions (`$E7A0-$F10A`) are byte-identical**, so the float
results and costs carry over.  327 bytes of AppleSoft differ, all in the
GS's 80-column / lower-case / no-cassette patches: token-table entries,
`PARSE`, `LIST`, `PRINT` comma and `TAB`, `SCRN`, lo-res `PLOT`/`HLIN`/`VLIN`,
`GR`, `HTAB`, `NAMOK`, and the cassette `LOAD`/`SAVE`/`STORE`/`RECALL`/
`SHLOAD`.  (The monitor `$F800-$FFFF` differs much more.)

## Where the cycles go

Exclusive attribution by the ROM routine the PC is in (S-C DocuMentor
labels), grouped by what a bytecode compiler does to them
(`prof/summarize.py`):

| run | parse + lookup (bytecode removes) | float pack/unpack | float arithmetic | I/O, strings, graphics | user machine code |
|---|---:|---:|---:|---:|---:|
| BM1 | 51.4% | 19.9% | 28.5% | 0.2% | |
| BM2 | 49.3% | 17.6% | 32.9% | 0.2% | |
| BM3 | 49.6% | 12.2% | 38.1% | 0.1% | |
| BM4 | 43.4% | 13.8% | 42.5% | 0.2% | |
| BM5 | 47.5% | 12.8% | 39.4% | 0.3% | |
| BM6 | 51.4% | 14.8% | 33.4% | 0.4% | |
| BM7 | 61.5% | 11.9% | 26.4% | 0.2% | |
| BM8 | 8.5% | 7.5% | 83.8% | 0.2% | |
| Lemonade, 5 days | 32.5% | 9.7% | 19.7% | 6.1% | 32.0% |

"Parse + lookup" is statement dispatch, CHRGET, the expression evaluator's
parsing, variable search (PTRGET), constants re-parsed by FIN every time
they execute, line-number parsing and searching (LINGET, FNDLIN), FOR-frame
search, and skipping REM/DATA/false-IF text.

## What it says

- **Bytecode alone roughly halves BM1-BM7** (removing 43-62% bounds the gain
  at 1.8-2.6x on the same CPU).  BM7's array loop is the best case: PTRGET's
  linear variable search is 25% of it.
- **A better float representation is the next 12-20%**: AppleSoft unpacks
  every variable into FAC/ARG and packs it back (`LOAD.FAC.FROM.YA`,
  `LOAD.ARG.FROM.YA`, `STORE.FAC.AT.YX.ROUNDED`).  Keeping values unpacked in
  the bytecode's slots removes most of it while staying bit-exact.
- **Float arithmetic is what remains, 26-42%** in BM1-BM7, mostly on
  integer-valued operands (`K=K+1`, `K<1000`, loop counters) - the exact
  integer fast path's target.  BM8 (`^`, `LOG`, `SIN`) is 84% arithmetic:
  only faster 65816 float routines help there.
- **Lemonade is a compatibility bench, not a speed bench.**  A third of its
  time is its own music routine (`CALL 770`), much of the rest deliberate
  delay loops.  Its notable AppleSoft cost is **line search: 11.5%**, from
  `IF A$<>"*" THEN 15210`-style loops - a GOTO to the same or an earlier line
  scans from the start of the program, here ~370 lines, every iteration.
  Bytecode makes that free.

## Hot spots by ROM label (Lemonade)

`FL1` (FNDLIN's loop) 11.5%, `NEXT.3` 3.2%, `SHIFT.RIGHT.4` 2.8%,
`STORE.FAC.AT.YX.ROUNDED` 2.4%, `LOAD.ARG.FROM.YA` 2.4%, `LOAD.FAC.FROM.YA`
2.3%; hottest lines 15420 (music `POKE`/`CALL`), 15510 (`FOR I=1 TO 2000`
delay), 15210 (the DATA scan for `"*"`).  Full output: `make lemonade`.
