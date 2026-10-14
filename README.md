# applesoft816

A faster AppleSoft for the Apple IIgs.  Unmodified AppleSoft programs are
compiled to bytecode and run by a 65816 interpreter that keeps the program
and all its data outside banks 0 and 1.  Results are bit-for-bit those of the
IIgs ROM's own AppleSoft, its known bugs included; most programs run two to
four times faster on the same machine.

It runs on any IIgs (or IIgs emulator) under ProDOS 8, with no special
hardware.  It runs on KEGS and on a real IIgs (a Strudelworks card's GS
core); the card makes it faster, not possible.

## Using it

Boot a disk holding ProDOS 8 and the AS816 files; AS816.SYSTEM starts and
shows the `APPLESOFT816 0.1` prompt.  From there it behaves like AppleSoft
under BASIC.SYSTEM: type numbered lines, `LIST`, `RUN`, `SAVE`, `LOAD`,
`CATALOG`, and so on.  `BYE` returns to the program selector, or to GS/OS
if it launched us.

- `as816_master.po` (`MASTER=1 tools/mkdisk.sh IMAGE.po`) is the bare
  bootable 800K disk - ProDOS 2.4.3 and the four AS816 files, 1367 blocks
  free - a template for a disk of one's own.  Copy it; don't use the
  master itself.
- `tools/mkdisk.sh IMAGE.po` makes the same disk with Lemonade Stand and
  the Rugg/Feldman benchmarks BM1-BM8 on it.

## What it does

### The language: all of AppleSoft

- Every statement and function: arithmetic with AppleSoft's 40-bit floats,
  integer (`%`) and string (`$`) variables and arrays, `DEF FN`, `GOSUB`/
  `RETURN`/`POP`, `ON ... GOTO/GOSUB`, `FOR`/`NEXT` in all its forms, `DATA`/
  `READ`/`RESTORE`, `INPUT` and `GET`, `ONERR GOTO`/`RESUME`, `TRACE`, `STOP`/
  `END`/`CONT`, `CLEAR`, `RUN n`, `LIST`/`NEW`/`DEL` inside programs.
- Text screen: `HOME`, `HTAB`, `VTAB`, `NORMAL`/`INVERSE`/`FLASH`, `SPEED=`,
  `POS`, `TAB(`, `SPC(`, `PR#`, `IN#`.
- Graphics: lo-res (`GR`, `COLOR=`, `PLOT`, `HLIN`, `VLIN`, `SCRN(`) and
  hi-res (`HGR`, `HGR2`, `HCOLOR=`, `HPLOT ... TO`, `DRAW`, `XDRAW`, `ROT=`,
  `SCALE=`).
- The machine: `PEEK`, `POKE`, `WAIT`, `CALL` (the code runs in emulation
  mode, bank 0 its own), `USR`, `&` (the GS's AppleSoft also sends
  `SHLOAD`/`RECALL`/`STORE`/`SAVE`/`LOAD` in programs there), `PDL`.
- Errors as AppleSoft reports them, `BREAK IN n` on Ctrl-C, `CONT` after a
  break or an error.  Ctrl-Reset returns to the prompt with the program
  kept, as BASIC.SYSTEM's does.

### The environment: BASIC.SYSTEM's commands

- At the prompt and from programs (`PRINT CHR$(4)"..."`): `RUN`, `LOAD`,
  `SAVE`, `CATALOG`/`CAT` (BASIC.SYSTEM's layout), `PREFIX`, `CREATE`,
  `DELETE`, `RENAME`, `LOCK`, `UNLOCK`, `VERIFY`, `BLOAD`, `BRUN`, `BSAVE`,
  `-name` (by its type: RUN a BAS file, BRUN a BIN, EXEC a TXT, start a
  SYS in our place), `CHAIN` (the next program keeps the variables),
  `STORE`/`RESTORE` (BASIC.SYSTEM's VAR files, both ways), `PR#`, `IN#`,
  `BYE`; `FRE`, `MON`, `NOMON` are accepted and do nothing.
- Text files: `OPEN`, `READ`, `WRITE`, `APPEND`, `POSITION`, `FLUSH`,
  `CLOSE`, and `EXEC` (the prompt's lines, and `INPUT`/`GET`, from a file).
- The line editor uses AppleSoft's own tokenizer (ported step for step and
  checked against the ROM's on every corpus line); `LIST` lists as the GS
  ROM does.
- Ours: `COMPILE name` (compile it all, timed) and `TIMING` (RUN says how
  long it took to start).

### How it is fast

- **Compiled, lazily.**  `RUN` scans the program, lays out its memory and
  starts at once; each line is compiled to direct-threaded bytecode the
  first time control reaches it (with the 15 lines after it).  Lines that
  never run are never compiled.  Lemonade Stand (381 lines) starts in about
  half a second on a stock 2.8 MHz IIgs, against 5.2 seconds for the first
  version.  Nothing compiled is kept on disk: the source is the program.
- **No re-parsing, no searching.**  Variables are bound to slots, jumps go
  through a line table, constants are converted once.
- **A 65816 float library (`fp816`)**, every AppleSoft float routine ported
  and then sped up where programs spend their time: multiply by
  quarter-square tables, divide and number printing in 16-bit, a divide by
  a power of two without the loop, plain whole numbers parsed directly.
- **Integer short cuts that give the float result exactly**: `FOR`/`NEXT`
  with whole-number counters adds integers and packs the sum as the float
  routines would; subscripts convert without the general routine.
- **Fewer steps per expression**: an operation on a variable or a constant
  skips the expression stack.

Emulated cycles on the same CPU (emu816), the ROM's AppleSoft against ours:

| Program | Speed-up |
|---|---|
| BM1 (empty `FOR` loop) | 2.9x |
| BM2 | 5.5x |
| BM3 | 3.0x |
| BM4 | 4.2x |
| BM5 | 4.3x |
| BM6 | 3.5x |
| BM7 | 4.0x |
| BM8 (`^`, `LOG`, `SIN`) | 1.6x |
| Lemonade Stand | 1.9x (half its time is its own 6502 music routine, run as is) |

`docs/SPEED.md` has every step's numbers.

### Exactly AppleSoft

Every result is checked against the IIgs ROM's own code:

- `tests/all.sh`: 52 programs (statements, strings and garbage collection,
  input, errors and ONERR, graphics, BM1-BM8, a whole game of Lemonade
  Stand), run under the ROM's AppleSoft and under ours on emu816: output and
  final screen identical, both with the lazy compile and with `GSRUN_FULL=1`.
- `tests/dos/all.sh`: BASIC.SYSTEM 2.4.3 itself against ours in KEGS, screen
  for screen (commands, CATALOG, text files, EXEC, CHAIN, STORE/RESTORE,
  launching SYS files, LIST, Ctrl-Reset).
- `make -C fp816 test`: fptest, fp816 against the ROM's float routines, 30
  families (arithmetic, conversions, FIN/FOUT, every function, NEXT's integer
  path) on random and edge-case operands; mutation-checked.
- `gsrun --cc`: the compiler built for the 65816 produces the same image as
  the PC build, byte for byte.

`docs/KNOWN_BUGS.md` lists AppleSoft's own bugs, which we reproduce on
purpose (fixes would come behind a switch, off by default).

### Where it differs from ROM AppleSoft

- **AppleSoft's internals are not there.**  The program, its variables,
  arrays and strings live in their own banks, so `PEEK`/`POKE` of
  AppleSoft's zero page and tables, `LOMEM:`/`HIMEM:` (accepted, no effect)
  and `FRE(0)` (a large number) are not the ROM's.  The well-known cells
  (`PEEK(222)`, `PEEK(218)+256*PEEK(219)`, `POKE 216,0`) work.  Bank 0 below
  `$9600` is the program's own: machine code, shape tables, both hi-res
  pages.
- **Delay loops run faster** (`FOR I=1 TO 500: NEXT`).  Machine code reached
  by `CALL` keeps its timing.
- **`IF` on a string expression** is not implemented (the ROM's `IF A$ THEN`
  tests a leftover exponent: KNOWN_BUGS.md #3).
- ProDOS only: DOS 3.3 programs are copied to ProDOS disks first.

## Building

Needs `ca65`/`ld65` (cc65), the llvm-mos 65816 toolchain
(`~/opt/llvm-mos-65816-root`), a C++17 compiler, and these sibling
checkouts in `~/repos`: `ares` (the 65816 core for emu816), `AppleWin`
(ProDOS 2.4.3's disk, the IIe ROM and symbols for tokenizing and
profiling), `strudelworks-host` (the IIgs ROM 3 image, `assets/
apple2gs_rom3.bin`).  The DOS tests also need KEGS 1.38 (`~/repos/kegs.1.38`),
Xvfb and xdotool.

    tools/mkdisk.sh build/as816.po           # builds everything, makes the disk
    MASTER=1 tools/mkdisk.sh build/as816_master.po
    tests/all.sh                             # the ROM comparison
    tests/dos/all.sh                         # BASIC.SYSTEM in KEGS
    make -C fp816 test                       # fptest

## The pieces

| On the disk | What |
|---|---|
| `AS816.SYSTEM` (`sys/`) | the ProDOS 8 system file: checks for a IIgs, gets nine banks from the Memory Manager (given back at BYE), loads the rest, sets up bank 0 |
| `AS816.VM` (`vm/` + `fp816/`) | the bytecode interpreter and the float library, one bank |
| `AS816.SH` (`cc/`) | the shell: editor, BASIC.SYSTEM's commands, text files, CHAIN, STORE |
| `AS816.CC` (`cc/` + `compiler/`) | the compiler, called by the shell and, for the lazy compile, by the running program |

| In the repo | What |
|---|---|
| `compiler/` | the compiler and tokenizer (portable C: llvm-mos on the GS, gcc on the PC) |
| `vm/`, `fp816/` | the VM and fp816 (65816 assembly); `fp816/fptest.cpp` the float oracle |
| `cc/`, `sys/` | the shell and compiler images, the system file |
| `gsrun/` | a program under the GS ROM's AppleSoft or under ours, same virtual monitor; `parsetest` for the tokenizer |
| `emu816/` | the ares 65816 core standalone |
| `tests/` | `all.sh`/`compare.sh` and the programs; `dos/` the KEGS tests |
| `bench/` | Lemonade Stand (1979) and BM1-BM8 |
| `prof/`, `tools/` | `asprof` (ROM AppleSoft profiled), `vmprof.py`/`ccprof.py` (where our cycles go), disk and KEGS tools |
| `docs/` | `DESIGN.md` (decisions), `INTERP.md` (the compiler and VM), `M5.md` (the system on the machine), `SPEED.md`, `PROFILE.md`, `KNOWN_BUGS.md`, `AMPER_SURVEY.md` |

Later: extensions (a superset of what Peersoft offers), AppleSoft's bug
fixes behind a switch, and a 16-bit ProDOS 8 underneath.
