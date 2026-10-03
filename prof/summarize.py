#!/usr/bin/env python3
"""Group asprof's exclusive categories by what a bytecode compiler does to them."""
import re, sys

GROUPS = [
    ('parse+lookup (bytecode removes)', ['statement dispatch', 'CHRGET scanner', 'expression eval',
        'variables/arrays', 'FIN text->number', 'FNDLIN line search', 'LINGET line# parse',
        'GOTO/GOSUB/IF/ON', 'FOR/NEXT', 'skip REM/DATA/IF', 'LET']),
    ('float pack/unpack (representation)', ['float load/store']),
    ('float arithmetic', ['float arithmetic', 'math functions']),
    ('I/O, strings, graphics', ['PRINT', 'INPUT/GET/READ', 'FOUT number->text', 'strings', 'string GC',
        'monitor screen out', 'monitor keyboard', 'lo-res graphics', 'hi-res graphics', 'Cx firmware',
        'other statements', 'monitor (other)', 'AppleSoft (other)']),
    ('user machine code (untouched)', ['user machine code']),
]

def cats(path):
    out, on = {}, False
    for line in open(path):
        if line.startswith('category'):
            on = True
            continue
        if on:
            if not line.strip():
                break
            m = re.match(r'(.+?)\s+(\d+)\s+[\d.]+%$', line.rstrip())
            out[m.group(1)] = int(m.group(2))
    return out

print(f"{'run':10}" + ''.join(f'{g[0][:22]:>24}' for g in GROUPS))
for path in sys.argv[1:]:
    c = cats(path)
    tot = sum(c.values())
    seen = set()
    row = []
    for _, names in GROUPS:
        row.append(sum(c.get(n, 0) for n in names))
        seen.update(names)
    missing = set(c) - seen
    if missing:
        sys.exit(f'{path}: ungrouped {missing}')
    name = path.split('/')[-1].rsplit('.', 1)[0]
    print(f'{name:10}' + ''.join(f'{100 * v / tot:23.1f}%' for v in row))
