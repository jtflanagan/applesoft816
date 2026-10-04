#!/bin/bash
# all.sh: every BASIC.SYSTEM session (KEGS, ~2 minutes each)
cd "$(dirname "$0")"
exec ./compare.sh *.txt
