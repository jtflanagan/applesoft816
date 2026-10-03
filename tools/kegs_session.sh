#!/bin/bash
# kegs_session.sh FILE - for each line of FILE ("command" or "command<TAB>seconds"),
# on the KEGS that kegs.sh started: HOME, type it, wait, print the screen
# (trailing blank lines dropped) under "=== command".  The same FILE run
# under BASIC.SYSTEM and under applesoft816 gives two transcripts to diff.
set -eu
k="$(dirname "$0")/kegs.sh"
while IFS=$'\t' read -r cmd wait; do
  [ -z "$cmd" ] && continue
  "$k" type $'HOME\n'
  sleep 0.7
  "$k" type "$cmd"$'\n'
  sleep "${wait:-2}"
  echo "=== $cmd"
  "$k" text | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}'
done < "$1"
