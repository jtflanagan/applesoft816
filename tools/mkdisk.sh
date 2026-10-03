#!/bin/bash
# mkdisk.sh IMAGE.po [PROGRAM.bas|.tok ...] - an 800K ProDOS boot disk for
# the live tests: ProDOS 8 (2.4.3, from AppleWin's disk) and its boot
# blocks, AS816.SYSTEM first (so ProDOS runs it), AS816.VM, AS816.CC, then
# Lemonade, BM1-BM8 and any programs named, tokenized by AppleSoft itself.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/.."
AW=$root/../AppleWin
P8=$AW/bin/ProDOS_2_4_3.po
img=$1
shift
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
make -s -C "$here" prodisk
make -s -C "$root/sys"
make -s -C "$root/vm"
make -s -C "$root/cc"
make -s -C "$root/prof" asprof
pd=$here/prodisk

tok() {     # tok FILE: its tokenized bytes in $work/NAME.tok, NAME on stdout
  local f=$1 name
  name=$(basename "$f"); name=${name%.bas}; name=${name%.tok}; name=$(echo "$name" | tr a-z_ A-Z.)
  if [ "${f%.tok}" != "$f" ]; then
    cp "$f" "$work/$name.tok"
  else
    "$root/prof/asprof" --rom "$AW/resource/Apple2e_Enhanced.rom" --sym "$AW/bin/A2_BASIC.SYM" \
        --sym "$AW/bin/APPLE2E.SYM" --type "$f" --dump-prog "$work/$name.tok" --lines 0 --syms 0 \
        --no-run --idle 50000 >/dev/null 2>&1
  fi
  echo "$name"
}

$pd "$img" format AS816
$pd "$img" boot "$P8"
$pd "$P8" get PRODOS "$work/PRODOS"
$pd "$img" put PRODOS FF 0000 "$work/PRODOS"
$pd "$img" put AS816.SYSTEM FF 2000 "$root/sys/AS816.SYSTEM"
$pd "$img" put AS816.VM 06 0000 "$root/vm/vm.bin"
$pd "$img" put AS816.CC 06 0000 "$root/cc/cc.bin"
for f in "$root/bench/lemonade/LEMONADE.tok" "$root"/bench/rugg_feldman/BM*.bas "$@"; do
  name=$(tok "$f")
  $pd "$img" put "$name" FC 0801 "$work/$name.tok"
done
$pd "$img" ls
