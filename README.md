# applesoft816

A faster AppleSoft for the Apple IIgs: unmodified AppleSoft programs,
compiled to a cached bytecode, run by a 65816 interpreter that lives (with
all its data) outside banks 0/1.  Later, a 16-bit ProDOS 8 underneath.

- `docs/DESIGN.md` - scope, decisions, proposals, open questions
- `docs/PROFILE.md` - where ROM AppleSoft spends its time (measured)
- `prof/` - `asprof`, ROM AppleSoft on an emulated IIe, profiled
- `bench/lemonade/` - Lemonade Stand (1979), the compatibility bench
- `bench/rugg_feldman/` - BM1-BM8
- `fp816/` - AppleSoft floating point for the 65816, bit-exact (fptest)
- `vm/` - the bytecode VM (assembly), linked with fp816 into one bank image
- `compiler/` - the compiler (portable C: llvm-mos-65816 on the GS, gcc on the PC)
- `emu816/` - the ares 65816 core standalone, for the tests
- `gsrun/` - a program under the GS ROM's AppleSoft or under the VM, same virtual monitor
- `tests/` - `compare.sh` and the test programs
- `tools/dsk33.py` - DOS 3.3 image catalog/extract; `tools/detok.py` - list a tokenized program
