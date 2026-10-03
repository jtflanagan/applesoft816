# applesoft816: the interpreter

How the compiler and the VM are built.  Scope and the reasons behind it are
in `DESIGN.md`; this is the engineering.  Decisions marked *(user)*.

## Decisions

- **ProDOS only** *(user)*: no DOS 3.3.  DOS 3.3 programs (Lemonade) are
  copied to a ProDOS disk; the tokenized format is the same.
- **Assembly and C, split as fits** *(user)*: the VM, its runtime and the
  float library in 65816 assembly (ca65); the compiler, line editor,
  immediate mode and the file/`CHR$(4)` commands in C (llvm-mos-65816, as
  the GS control panel uses), which run between program runs where speed
  matters less than getting them right.
- **Program memory, any IIgs** *(user)*: loaded from disk into banks the
  Memory Manager (later our ProDOS) hands out; no ROM, no card.

## Bit-exactness, by construction

The bar is: same program, same input, same output as ROM AppleSoft, to the
bit.  Arithmetic is fp816 (done, checked against the ROM).  The rest comes
from compiling to **the same sequence of float operations the ROM performs**:

- **Expressions.**  FRMEVL is a precedence parser: at every binary operator
  it rounds FAC and pushes it, evaluates the right side into FAC (keeping
  its rounding byte), pops the left side into ARG and runs `FAC = ARG op
  FAC` (FADDT, FSUBT, FMULTT, FDIVT, FPWRT, RELOPS, AND, OR).  Unary minus
  and NOT push too.  The compiler runs **the same algorithm with the same
  precedence table** (`MATHTBL`, `$D0B2`: + - `$79`, * / `$7B`, ^ `$7D`,
  unary - and NOT `$7F`, relations `$64`, AND `$50`, OR `$46`) over the
  tokens and emits a PUSH where the ROM pushes and an OP where it performs.
  The VM keeps **one persistent FAC/ARG** in its direct page, as AppleSoft
  does, so leftover state behaves the same too.
- **Constants** are what FIN makes of their text - computed once at compile
  time by fp816's FIN, including the rounding byte - instead of every time.
- **Variables** load as LOAD.FAC (rounding byte 0) or, for `%`, GIVAYF; they
  store as STORE.FAC rounded or AYINT.
- **Statements** do the ROM's float operations in the ROM's order: NEXT is
  "FAC = step (from the frame); FADD the variable; store rounded; FCOMP the
  limit"; FOR stores limit and step rounded; and so on, each read off the
  ROM listing as it is implemented.
- **Output** is FOUT's text through the monitor's COUT, as AppleSoft's.

## Memory

| Bank | Holds | Registers while the VM runs |
|---|---|---|
| **VM bank** | the VM's handlers, fp816, and the program's **bytecode** | PBR |
| **Data bank** | scalar variable slots, FOR/GOSUB stacks, the constant pool, small arrays | DBR |
| **Text bank** | the tokenized program at `$0801`, exactly the file's bytes (link addresses valid) | - |
| **String/array banks** | string space, large arrays (24-bit) | - |
| **C banks** | the compiler/editor code (PBR) and its data, soft stack and heap (DBR) | while C runs |
| **Bank 0** | the VM's direct page (FAC, ARG, VM registers), one page; C's imaginary registers `$00-$1F`, saved and restored around C; emulation-mode stubs for the ROM monitor and `CALL` | |

All code is bank-independent (fp816 already is; the C image uses `JSR` and
16-bit pointers only), so every bank is just "whatever the allocator gave".

## The VM

- **Direct-threaded code in the VM bank.**  Each operation is the 16-bit
  address of its handler, followed by its operands; IP is X, so dispatch is
  `jmp (0,x)` (JMP (abs,X) reads the PROGRAM bank - which is why bytecode
  lives with the handlers).  Handlers advance X past their operands.
- **Variables are 16-bit addresses in the data bank**, resolved by the
  compiler: a load is `lda abs` with DBR = the data bank - no search.
  Numeric slots are unpacked (exponent, mantissa, sign byte as stored) so a
  load is a copy, not an unpack.
- **Jumps are resolved**: GOTO/GOSUB/THEN n/ON..GOTO are bytecode addresses
  (a missing line is still `?UNDEF'D STATEMENT` at run time, where the ROM
  reports it).
- **Lines**: a LINE op at each line start sets CURLIN (errors, ONERR, TRACE)
  and does the Ctrl-C check AppleSoft does between statements.
- **DATA**: the compiler lists every DATA item's text span; READ takes the
  next and converts it (FIN) as AppleSoft does.
- **Errors** unwind to the VM's handler with AppleSoft's code and line;
  ONERR/RESUME work on the same `$D8`/`$DE`/`$DA` semantics.

## The compiler

C, line by line: copy a line from the text bank into a near buffer, compile
its statements into bytecode written to the VM bank, record the line's
bytecode address; patch forward jumps at the end.  Variables are assigned
slots on first sight (AppleSoft's two-significant-character names, `%` and
`$` kept apart).  The result is cached beside the source (DESIGN.md).

## Testing (as built)

- `gsrun ROM --prog TOKENS [--vm vm/vm.bin]`: the program under the IIgs
  ROM's AppleSoft, or compiled and run by the VM, both on emu816 behind one
  virtual monitor (COUT/GETLN/RDKEY/WAIT and the screen calls taken over:
  the GS monitor needs firmware state only a real boot sets up).
- `tests/compare.sh PROGRAMS.bas`: tokenizes each with AppleSoft itself
  (asprof `--no-run`), runs both, compares the bytes, prints the cycles.

Found on the way: the GS's PRINT comma differs from the IIe's only in 80
columns (its column helper ends with `BIT $C01F`, which hands the comma N =
the 80-column switch); AppleSoft's cold start sets only four of RND's five
seed bytes (`$CD` is whatever memory held).

## Testing (plan)

- **Output oracle**: the same program and key script under ROM AppleSoft
  (`prof/asprof`, which already captures the screen, gains a COUT stream)
  and under our interpreter on emu816 (output trapped at the VM's character
  routine); the two streams must be identical.
- **Speed**: emulated cycles for BM1-BM8 and Lemonade, ROM vs ours.
- **Then** KEGS and the real machine.

## Milestones

| | | gate |
|---|---|---|
| M1 | build: the VM image (vm.s + fp816); the compiler (portable C); gsrun runs either the ROM's AppleSoft or the VM on emu816 through one virtual monitor | "PRINT 2+2" prints 4 - **done** |
| M2 | numeric core: PRINT, LET, expressions, IF, GOTO, GOSUB/RETURN, FOR/NEXT, DIM and arrays, END, REM, the numeric functions, AppleSoft's errors | BM1-BM8 and `tests/m2` (arithmetic, loops, GOSUB, arrays, functions, PRINT layout, eight errors): output identical to the GS ROM's AppleSoft - **done**; 1.03-4.3x before any optimisation |
| M3 | strings, arrays, DIM, INPUT/GET, DATA/READ/RESTORE, DEF FN, all functions, ONERR | Lemonade's computing lines; a string/array corpus - **done** |
| M4 | screen and machine: HOME, VTAB/HTAB, TEXT/GR/COLOR/PLOT/HLIN/VLIN, HGR/HPLOT, PEEK/POKE/CALL (bank-0 stubs, emulation mode) | Lemonade, whole game, identical screens - **done (2026-10-03): byte-identical output and final screen, 84.0M vs 65.0M cycles**; HGR/HPLOT/DRAW, USR, &, 80-column double lo-res still to do |
| M5 | the system file, immediate mode, line editor, LOAD/SAVE/RUN/CATALOG/CHR$(4) commands on ProDOS, the bytecode cache | KEGS, then the machine |
