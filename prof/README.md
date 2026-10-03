# asprof: ROM AppleSoft, profiled

An emulated enhanced Apple IIe (vrEmu6502 65C02 core, 64K RAM, the real
16K ROM with internal `$Cx` switching, empty slots, a scripted keyboard)
that boots to `]`, loads or types a program, runs it, and reports where the
cycles went.

Needs two sibling clones: `~/repos/vrEmu6502` (CPU core) and
`~/repos/AppleWin` (the ROM image and the S-C DocuMentor symbol tables).

    make                      # build
    ./run_bms.sh              # BM1-BM8 vs the published times; out/BMn.txt
    make lemonade             # five days of Lemonade Stand
    python3 summarize.py out/*.txt

## Attribution

- **Exclusive** (the category table): by the ROM routine the PC is in,
  address ranges from the S-C DocuMentor labels.  AppleSoft reaches most
  work by tail jumps and RTS dispatch, so a call stack cannot see it.
  ROM routines called from machine code in RAM count as that machine code.
- **Inclusive** (the routine table): a shadow call stack following `JSR` and
  stack unwinding; only routines entered by `JSR` appear, and `FL1` covers
  GOTO's forward search, which enters FNDLIN's loop directly.
- **Lines**: cycles by `CURLIN`, sampled at statement dispatch (`NEXT`
  rewrites it a byte at a time).
- Only cycles after `RUN`'s Return, while a program line runs, are counted.
  A key-wait is a poll from the firmware's KEYIN; AppleSoft's Ctrl-C check
  and the monitor's Ctrl-S check also read `$C000` and are not waits.

## Key scripts

Literal characters; newlines ignored; `#` lines are comments; `{CR}`,
`{ESC}`, `{SP}`; `{SNAP}` prints the text screen at the next key request.
