# applesoft816: design

A faster AppleSoft for the Apple IIgs: the same language (line numbers and
all), compiled to a cached bytecode and run by a 65816 interpreter that keeps
the program and all its data outside banks 0/1.  Later, a 16-bit ProDOS 8
underneath it.

Status: **design, measurement, and the float library.**  `fp816` (every
AppleSoft float routine, bit-exact with the ROM) is done; the compiler and
interpreter are next.
Decisions marked *(user)* are settled; the rest are proposals.

## Scope

- **Pure AppleSoft** *(user)*: run existing AppleSoft programs, unmodified,
  faster.  No new dialect.  (An earlier plan for a label-based "modern BASIC"
  with an AppleSoft translator was dropped as too large.)
- **Bytecode cached beside the source** *(user)*, like Python's `.pyc`:
  compile on `RUN`, keep it in memory, write it next to the program; no
  re-parsing, variables bound to slots, jumps resolved.
- **Compatibility with AppleSoft's internals is given up** *(user)* in
  exchange for capacity: program text, bytecode, variables, arrays and strings
  all live outside bank 0, so `LOMEM:`/`HIMEM:` stop mattering and bank 0 is
  the program's own (machine code, shape tables, both hi-res pages).
- **65816, IIgs only** follows from that.
- **Runs on any IIgs, card or no card** *(user)*: everything is program
  memory - loaded from disk by our (extended) ProDOS, nothing in ROM, nothing
  that needs the Strudelworks card.  It runs on a stock IIgs and in any IIgs
  emulator (KEGS, GSplus, MAME); the card makes it faster, it does not make
  it work.

### What "pure AppleSoft" covers (proposal)

| Supported | Not supported |
|---|---|
| Every AppleSoft statement and function, graphics and sound included | Reading/patching the variable table or program text (`PEEK(105)`..., `POKE 103/104` tricks) |
| `PEEK`/`POKE`/`CALL` to hardware, the monitor, and the program's own memory | `&` handlers and `USR` routines that call `CHRGET`/`FRMEVL`/`PTRGET` or use the FAC |
| The well-known error cells: `PEEK(222)`, `PEEK(218)+256*PEEK(219)`, `POKE 216,0` (compiled to operations, not memory) | Bit-for-bit string-space layout |
| `LOMEM:`/`HIMEM:` (no-ops), `FRE(0)` (a large positive value) | |
| Data files through DOS / BASIC.SYSTEM `CHR$(4)` commands | |

## Fidelity

- **Results bit-identical to ROM AppleSoft.**  Same 40-bit (5-byte MBF)
  floats, same rounding, same `RND` sequence (including `RND(-n)` reseeding),
  same `STR$`/`PRINT` number formatting.
- Float routines written natively in 65816 (16-bit registers take the 32-bit
  mantissa in two halves) and checked against the ROM's own routines run in
  an emulator, TestFloat-style.
- **Exact integer fast path**: MBF represents integers below 2^32 exactly, so
  where the compiler can show operands are whole numbers in range (loop
  counters, subscripts, counts) integer arithmetic gives exactly the bits the
  float routines would.  Otherwise the float path.
- Expected, accepted difference: `FOR I=1 TO 2000: NEXT` delay loops run
  faster.  Machine-code timing (`CALL`ed tone routines) is unchanged.

## Memory layout (proposal)

| Where | What |
|---|---|
| A bank allocated at load | The interpreter and fp816, loaded by our ProDOS at offset `$0000` of whatever bank it is given.  The code is bank-independent: it finds its own bank with `PHK` (fp816 is tested from banks `$05` and `$7E`). |
| Banks allocated as needed | Program text, bytecode, variables, arrays, strings - from the IIgs Memory Manager (stage 1, under the real ProDOS 8; to be confirmed it is usable there, in KEGS) or from our ProDOS's own allocator (stage 2).  Any RAM bank `$02-$7F` on a stock machine; on a Strudelworks card the extra banks are simply more memory. |
| Bank 0, unavoidably | The interpreter's direct page and stack (the 65816 cannot put them elsewhere), one reserved page below DOS, small emulation-mode stubs for calling the ROM monitor and for `CALL`. |
| Bank 0, the program's | Everything else not used by DOS/ProDOS and the screen. |

## What we own because the program is out of bank 0

The ROM's line editor stores at `$801` and DOS/BASIC.SYSTEM's `LOAD`/`SAVE`/
`RUN file` work AppleSoft's own pointers, so the interpreter supplies:
immediate mode (direct lines compiled and run), the line editor, `LIST`,
`DEL`, `NEW`, `LOAD`/`SAVE`/`RUN file`/`CHAIN`.  Files stay standard AppleSoft
(`$FC` / DOS `A`) plus the cache file.  `PRINT`/`INPUT` go through the
ROM's `COUT`/input hooks so DOS and BASIC.SYSTEM still see `CHR$(4)` commands.

## The cache file

- Compiled on `RUN`, kept in memory, written beside the source.
- Validated by a hash of the tokenized program (DOS 3.3 has no timestamps).
- Editing a line invalidates it; per-line recompilation is a later option.
- Always disposable: the source is the program.

## The OS, later *(user: incremental)*

1. The BASIC, plus our own BASIC.SYSTEM replacement (65816), on the real
   ProDOS 8.
2. A 16-bit ProDOS 8 kernel that supports the BASIC: `$BF00` stub + global
   page in bank 0, kernel native in allocated banks, language card freed, a
   large block cache in spare RAM; on a Strudelworks card it can use the
   card's own disk path, elsewhere the standard block devices.
3. Grow general ProDOS 8 compatibility as we go, measured on a corpus (4cade's
   ProDOS 8 titles, the System 6 P8 apps; ProDOS 2.4.3 as the reference).

### 16-bit MLI *(user: early, alongside the 8-bit MLI)*

Proposal: "ProDOS 8 with wide pointers" - the same calls, numbers and error
codes; `JSL` entry in native mode with call number and 24-bit parameter
pointer inline; 4-byte pointers and 32-bit counts in the parameter blocks;
the kernel owns file buffers (no 1K bank-0 buffer per open file); one open
file table shared with the 8-bit MLI.  Like the BASIC, the OS is loaded
from disk into allocated memory - no ROM banks - so it too runs on any IIgs
and in emulators.  In stage 1 a shim implements it over
the real ProDOS 8 with bank-0 bounce buffers, so the BASIC uses the 16-bit
MLI from day one.  A GS/OS class-1 subset at `$E1/00A8` is a later option.

## Open questions

1. DOS 3.3, ProDOS 8, or both for stage 1 (Lemonade is a DOS 3.3 disk).
2. How it starts: a system file replacing BASIC.SYSTEM, or a loader that
   installs itself and takes over the prompt.
3. Implementation language/assembler for the 65816 side.

## Benches

- **Lemonade Stand** *(user)*: `bench/lemonade/`, the original 1979 program
  (Asimov `images/games/misc/lemonade_stand.dsk`), with a five-day script.
- **Rugg/Feldman BM1-BM8**: `bench/rugg_feldman/`.
- Measured against ROM AppleSoft first: see `docs/PROFILE.md`.
