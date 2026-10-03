# emu816: a standalone 65816 for tests

The ares WDC65816 core (`~/repos/ares`, sparse clone of
`ares/component/processor/wdc65816`, ISC licence), compiled from its own
sources unmodified through `nall_shim.hpp` (the few ares integer and
bit-range types it uses).  `Cpu816` puts it over a flat 16 MB memory with
optional read/write hooks and counts bus cycles.

    make sst        # SingleStepTests/65816 (the firmware's sampled set)

Result 2026-10-03: **981,610 tests, 0 failed**, 10,283 known divergences,
none reachable from native-mode code:

- MVN/MVP: the vectors stop at 100 cycles mid-move; not comparable.
- WAI/STP: return at once here (no interrupts), so cycles differ.
- Emulation mode, `(dp,X)` with DL != 0 and the pointer at `$xxFF` or past
  `$FFFF` (94 cases): ares wraps the pointer differently from the chip.
- Emulation mode, `JSR (abs,X)` with S at `$0100/$0101` (5 cases).

`sst_run` counts these apart and fails on anything else.
