#!/bin/bash
# compare.sh PROGRAM.bas... - each program under the IIgs ROM's AppleSoft and
# under applesoft816, through the same virtual monitor (gsrun): the outputs
# must be identical.  Prints both cycle counts and the speed-up.
#   KEYS=script  key script for every program (else PROGRAM.keys if present)
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/.."
ROM=${ROM:-$root/../strudelworks-host/assets/apple2gs_rom3.bin}
AW=$root/../AppleWin
work=${WORK:-/tmp/as816-compare}
mkdir -p "$work"
make -s -C "$root/prof" asprof >/dev/null && make -s -C "$root/gsrun" >/dev/null && make -s -C "$root/vm" >/dev/null || exit 2
pass=0 fail=0
printf '%-28s %12s %12s %8s  %s\n' program "ROM cycles" "VM cycles" speedup result
for bas in "$@"; do
  name=$(basename "$bas" .bas)
  keys=${KEYS:-}
  [ -z "$keys" ] && [ -f "${bas%.bas}.keys" ] && keys="${bas%.bas}.keys"
  karg=(); [ -n "$keys" ] && karg=(--keys "$keys")
  # tokenized by AppleSoft itself (typed in under asprof)
  "$root/prof/asprof" --rom "$AW/resource/Apple2e_Enhanced.rom" --sym "$AW/bin/A2_BASIC.SYM" \
      --sym "$AW/bin/APPLE2E.SYM" --type "$bas" --dump-prog "$work/$name.tok" --lines 0 --syms 0 \
      --no-run --idle 50000 >/dev/null 2>&1
  rc=$("$root/gsrun/gsrun" "$ROM" --prog "$work/$name.tok" "${karg[@]}" --out "$work/$name.rom" 2>&1 | sed -n 's/.* \([0-9]*\) cycles in the program/\1/p')
  # the VM gets at most the ROM's run in instructions, and some: a hang shows
  max=$(( ${rc:-0} + 20000000 ))
  vc=$("$root/gsrun/gsrun" "$ROM" --prog "$work/$name.tok" --vm "$root/vm/vm.bin" "${karg[@]}" --max "$max" --out "$work/$name.vm" 2>&1 | tee "$work/$name.vmlog" | sed -n 's/.* \([0-9]*\) cycles in the program/\1/p')
  if cmp -s "$work/$name.rom" "$work/$name.vm"; then r=same; pass=$((pass+1)); else r=DIFFERENT; fail=$((fail+1)); fi
  sp=$(awk -v a="${rc:-0}" -v b="${vc:-0}" 'BEGIN { if (b > 0) printf "%.2fx", a / b; else print "-" }')
  printf '%-28s %12s %12s %8s  %s\n' "$name" "${rc:-?}" "${vc:-?}" "$sp" "$r"
done
echo "$pass same, $fail different"
[ "$fail" = 0 ]
