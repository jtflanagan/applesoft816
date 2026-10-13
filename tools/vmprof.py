#!/usr/bin/env python3
"""Where the VM spends a program's cycles.

    GSRUN_HIST=hist.txt gsrun ROM --prog P.tok --vm vm/vm.bin
    tools/vmprof.py hist.txt [vm/vm.lbl] [N]

hist.txt is gsrun's "PC cycles" lines; each PC in the VM bank ($05) is
charged to the label at or before it (vm.lbl, ld65 -Ln: vm.s and fp816's
symbols, exclusive); bank 0 is the stubs and the user's own code, bank $FF
the ROM (COUT, CALLs into the monitor).
"""
import bisect
import re
import sys


def main() -> int:
    hist = sys.argv[1]
    lbl = sys.argv[2] if len(sys.argv) > 2 else "vm/vm.lbl"
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    raw = []
    for line in open(lbl):
        m = re.match(r"al ([0-9A-F]+) \.(\S+)", line)
        if m and not m.group(2).startswith("__"):
            raw.append((int(m.group(1), 16), m.group(2)))
    raw.sort(key=lambda t: (t[0], t[1].startswith("@")))
    syms, scope = {}, "?"            # a cheap local (@x) is named scope/@x
    for a, name in raw:
        if name.startswith("@"):
            name = f"{scope}/{name}"
        else:
            scope = name
        syms.setdefault(a, name)
    addrs = sorted(syms)
    total, by = 0, {}
    for line in open(hist):
        pc, n = line.split()
        pc, n = int(pc, 16), int(n)
        total += n
        if pc >> 16 == 0x05:
            i = bisect.bisect_right(addrs, pc & 0xFFFF) - 1
            name = syms[addrs[i]] if i >= 0 else "?"
        else:
            name = f"(bank ${pc >> 16:02X})"
        by[name] = by.get(name, 0) + n
    print(f"{total} cycles")
    for name, n in sorted(by.items(), key=lambda kv: -kv[1])[:top]:
        print(f"  {100 * n / total:5.1f}%  {n:10d}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
