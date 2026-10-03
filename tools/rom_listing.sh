#!/bin/bash
# A labelled da65 listing of the IIgs ROM 3's AppleSoft (bank $FF, $D000-$F7FF),
# labels from AppleWin's S-C DocuMentor tables.  Written to ref/ (gitignored:
# it is Apple's code).
#   tools/rom_listing.sh [LO HI]      default $D000-$F7FF
set -euo pipefail
cd "$(dirname "$0")/.."
ROM=${ROM:-../strudelworks-host/assets/apple2gs_rom3.bin}
SYM=../AppleWin/bin/A2_BASIC.SYM
LO=${1:-D000}; HI=${2:-F7FF}
mkdir -p ref
tail -c 65536 "$ROM" > ref/bankff.bin
{
  echo "GLOBAL { STARTADDR \$$LO; CPU \"6502\"; INPUTOFFS \$$LO; INPUTSIZE \$$(printf '%X' $((0x$HI - 0x$LO + 1))); };"
  # global labels ("D61A FNDLIN") and zero-page names ("  9D FAC"); first name per address
  awk '($0 ~ /^[0-9A-F][0-9A-F][0-9A-F][0-9A-F] / || $0 ~ /^  [0-9A-F][0-9A-F] /) && NF==2 {
         a=sprintf("%04X", strtonum("0x" $1)); n=$2; gsub(/[^A-Za-z0-9_]/,"_",n)
         if (!(n in seen) && !(a in at)) { seen[n]=1; at[a]=1; printf "LABEL { ADDR $%s; NAME \"%s\"; };\n", a, n } }' "$SYM"
} > ref/applesoft.info
da65 -i ref/applesoft.info ref/bankff.bin -o ref/applesoft_$LO-$HI.s 2>&1 | grep -v Warning || true
echo "ref/applesoft_$LO-$HI.s: $(wc -l < ref/applesoft_$LO-$HI.s) lines"
