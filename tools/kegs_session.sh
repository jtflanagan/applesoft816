#!/bin/bash
# kegs_session.sh FILE - for each line of FILE ("command" or "command<TAB>seconds"),
# on the KEGS that kegs.sh started (KEGS_DISPLAY): HOME, type it, wait for
# it to finish, print the screen (trailing blank lines dropped) under
# "=== command".  The same FILE run under BASIC.SYSTEM and under
# applesoft816 gives two transcripts to diff.
#
# Waits poll the screen (kegs_wait.py): a command is done when its screen
# ends in a bare prompt and holds still.  The seconds on a line are only
# its ceiling (default 10): a program that never returns to the prompt
# (before a <RESET>) is captured then.
# The line <RESET> is Ctrl-Reset (no HOME first: a running program would
# take the keys).
set -eu
here="$(dirname "$0")"
k="$here/kegs.sh"
w="$here/kegs_wait.py"
export DISPLAY=${KEGS_DISPLAY:-:99}
while IFS=$'\t' read -r cmd wait; do
  [ -z "$cmd" ] && continue
  if [ "$cmd" = "<RESET>" ]; then
    xdotool keydown Control_L; sleep 0.1
    xdotool key Pause; sleep 0.1
    xdotool keyup Control_L
    echo "=== $cmd"
    python3 "$w" prompt "${wait:-5}"
    continue
  fi
  "$k" type $'HOME\n'
  python3 "$w" home 5 >/dev/null
  "$k" type "$cmd"$'\n'
  echo "=== $cmd"
  python3 "$w" cmd "${wait:-10}"
done < "$1"
