#!/bin/bash
# Run BM1-BM8 under ROM AppleSoft; print emulated time vs the published time.
# Full profiles land in out/BMn.txt.
set -e
cd "$(dirname "$0")"
make -s asprof
AW=../../AppleWin
mkdir -p out
pub=(0 1.3 8.5 16.0 17.8 19.1 28.6 44.8 55.5)
printf '%-4s %12s %9s %9s\n' BM cycles seconds published
for n in 1 2 3 4 5 6 7 8; do
  ./asprof --rom $AW/resource/Apple2e_Enhanced.rom --sym $AW/bin/A2_BASIC.SYM --sym $AW/bin/APPLE2E.SYM \
    --type ../bench/rugg_feldman/BM$n.bas --snap-end --syms 40 > out/BM$n.txt
  grep -q '^|E  ' out/BM$n.txt || { echo "BM$n: no E on the screen"; exit 1; }
  read -r cyc sec < <(sed -n 's/^program: \([0-9]*\) cycles = \([0-9.]*\) s.*/\1 \2/p' out/BM$n.txt)
  printf 'BM%-2s %12s %9s %9s\n' $n "$cyc" "$sec" "${pub[$n]}"
done
