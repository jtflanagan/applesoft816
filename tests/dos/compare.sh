#!/bin/bash
# compare.sh SESSION... - each session file (tools/kegs_session.sh) under
# BASIC.SYSTEM 2.4.3 and under applesoft816, in KEGS, on fresh disks named
# alike (/BSYS) with the same test files in /BSYS/T; the two transcripts
# are diffed, the volume's block totals masked.
#   COLS=80 (default) or 40; the cursor cell after a prompt is not compared (it blinks)
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
tools="$root/tools"
AW=$root/../AppleWin
P8=$AW/bin/ProDOS_2_4_3.po
work=${WORK:-/tmp/as816-dos}
mkdir -p "$work"
pd=$tools/prodisk
make -s -C "$tools" prodisk

# the test files both disks get
printf 'HELLO\rWORLD\r' > "$work/NOTES"
head -c 300 "$root/vm/vm.bin" > "$work/CODE"
files() {   # in /BSYS/T: sessions start with PREFIX T, so listings hold only these
  "$pd" "$1" mkdir T
  "$pd" "$1" put T/LEMONADE FC 0801 "$root/bench/lemonade/LEMONADE.tok"
  "$pd" "$1" put T/NOTES 04 0000 "$work/NOTES"
  "$pd" "$1" put T/CODE 06 0300 "$work/CODE"
}

oracle_disk() {
  "$pd" "$1" format BSYS && "$pd" "$1" boot "$P8"
  for f in PRODOS BASIC.SYSTEM; do "$pd" "$P8" get $f "$work/$f"; done
  "$pd" "$1" put PRODOS FF 0000 "$work/PRODOS"
  "$pd" "$1" put BASIC.SYSTEM FF 2000 "$work/BASIC.SYSTEM"
  files "$1"
}
ours_disk() {
  VOL=BSYS "$tools/mkdisk.sh" "$1" >/dev/null
  files "$1"
}

session() {   # session DISK FILE OUT
  "$tools/kegs.sh" start "$1" 2 >/dev/null
  sleep 6
  if [ "${COLS:-80}" = 80 ]; then "$tools/kegs.sh" type $'PR#3\n'; sleep 2; fi
  "$tools/kegs_session.sh" "$2" > "$3"
}

rc=0
for s in "$@"; do
  name=$(basename "$s" .txt)
  oracle_disk "$work/oracle.po" >/dev/null
  ours_disk "$work/ours.po" >/dev/null
  session "$work/oracle.po" "$s" "$work/$name.oracle"
  session "$work/ours.po" "$s" "$work/$name.ours"
  # the volume's totals differ by the system files (BASIC.SYSTEM against ours)
  for f in oracle ours; do
    sed -E 's/^\](.)?$/]/; s/^(BLOCKS FREE:) *[0-9]+( +BLOCKS USED:) *[0-9]+/\1 n\2 n/; s/(TOTAL BLOCKS:) *[0-9]+/\1 n/' \
      "$work/$name.$f" > "$work/$name.$f.n"
  done
  if diff "$work/$name.oracle.n" "$work/$name.ours.n" > "$work/$name.diff"; then
    echo "$name: same"
  else
    echo "$name: differs ($work/$name.diff)"
    rc=1
  fi
done
"$tools/kegs.sh" stop
exit $rc
