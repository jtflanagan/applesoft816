#!/bin/bash
# all.sh: every comparison - the test programs, BM1-BM8, Lemonade Stand
cd "$(dirname "$0")"
exec ./compare.sh m2/*.bas m3/*.bas ../bench/rugg_feldman/BM*.bas ../bench/lemonade/LEMONADE.tok
