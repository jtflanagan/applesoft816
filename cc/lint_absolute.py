#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 John Flanagan
"""Refuse a panel image with a 16-bit operand into page zero, or an indexed
indirect jump.

While C runs, DBR = the data bank (link.ld): a THREE-byte instruction whose operand is
below $0100 reads or writes the data bank, not the imaginary registers in page zero
it meant.  The compiler reaches page zero through two-byte direct-page modes;
a three-byte one there is a symbol the linker placed wrong, or a compiler
choice this memory model cannot take - either way the image is not safe.

JMP/JSR (abs,X) read their table from the PROGRAM bank, but .rodata lives in
the data bank: a switch jump table there jumps through whatever code sits at
the same address (build with -fno-jump-tables).

A four-byte (long) operand other than JSL/JML names the link-time bank, never
the one the image runs in: an asm reference to a data symbol needs mos16().

    lint_absolute.py LLVM-OBJDUMP IMAGE.elf
"""
import re
import subprocess
import sys

# "  f0001a: ad 12 00     \tlda\t$12" - address, bytes, mnemonic, operand.
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(\w+)\t?(.*)$")
# JSR/JMP operands are program-bank addresses; JSL/JML and immediates never match.
SKIP = {"jsr", "jmp", "jsl", "jml", "pea", "per", "brl", "mvn", "mvp"}


def main() -> int:
    objdump, elf = sys.argv[1], sys.argv[2]
    out = subprocess.run([objdump, "--mcpu=mosw65816", "-d", elf],
                         check=True, capture_output=True, text=True).stdout
    bad = []
    for line in out.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        nbytes = len(m.group(2).split())
        mnem, operand = m.group(3), m.group(4).strip()
        if mnem in ("jmp", "jsr") and re.match(r"\(\$[0-9a-f]+,x\)", operand):
            bad.append(line.strip())
            continue
        if nbytes == 4 and mnem not in ("jsl", "jml"):
            bad.append(line.strip())
            continue
        if nbytes != 3 or mnem in SKIP or operand.startswith("#"):
            continue
        v = re.match(r"\(?\$([0-9a-f]+)", operand)
        if v and int(v.group(1), 16) < 0x100:
            bad.append(line.strip())
    for b in bad:
        print(f"lint_absolute: page-zero operand, (abs,X) jump or long operand: {b}", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
