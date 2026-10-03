#!/usr/bin/env python3
"""Where the 65816 compiler spends its cycles.

    GSRUN_CCPROF=prof.txt gsrun ROM --prog P.tok --vm vm/vm.bin --cc cc/cc.bin
    tools/ccprof.py prof.txt cc/cc.map [CC_BANK]

prof.txt is gsrun's "PC cycles" lines; each PC in the compiler's bank is
charged to the function that contains it (cc.map's symbols, code linked at
$010000); PCs elsewhere are the VM bank's fp816 (FIN) or bank 0.
"""
import re
import sys


def main() -> int:
    prof, mapf = sys.argv[1], sys.argv[2]
    cc_bank = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x07
    syms = []
    for line in open(mapf):
        m = re.match(r"^\s+([0-9a-f]+)\s+[0-9a-f]+\s+[0-9a-f]+\s+\d+\s+([A-Za-z_][\w.]*)$", line)
        if m:
            a = int(m.group(1), 16)
            if 0x10000 <= a < 0x20000:
                syms.append((a & 0xFFFF, m.group(2)))
    syms.sort()
    addrs = [a for a, _ in syms]
    import bisect
    total, by = 0, {}
    for line in open(prof):
        pc, n = line.split()
        pc, n = int(pc, 16), int(n)
        total += n
        if pc >> 16 == cc_bank:
            i = bisect.bisect_right(addrs, pc & 0xFFFF) - 1
            name = syms[i][1] if i >= 0 else "?"
        elif pc >> 16 == 0:
            name = "(bank 0)"
        else:
            name = f"(bank ${pc >> 16:02X}: fp816 FIN)"
        by[name] = by.get(name, 0) + n
    print(f"{total} cycles")
    for name, n in sorted(by.items(), key=lambda kv: -kv[1])[:30]:
        print(f"{n:>12} {100 * n / total:5.1f}%  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
