#!/usr/bin/env python3
"""ampscan.py - what a machine-language file does to AppleSoft, statically.

  ampscan.py FILE#06AAAA...      one report per file (the load address from the
                                 name a2img.py gives: #TTAAAA)
  ampscan.py --tsv FILE...       one summary line per file

From the load address (and every vector the code installs) a recursive
descent over 65C02 code, with immediate values tracked through each block,
records:

  vectors   stores to & ($3F6/7), USR ($0B/C), the IRQ user vector ($3FE/F),
            the language card's IRQ ($FFFE/F), CHRGET's code ($B1-$C8 but
            TXTPTR), the output/input hooks ($36-$39), and ProDOS 8
            ALLOC_INTERRUPT ($40) calls with their handler
  rom       calls into AppleSoft ($D000-$F7FF), by the ROM listing's labels
  zp        AppleSoft's page zero it reads or writes, by name
  saves     a variable's address kept: VARPNT/FORPNT (or A,Y after PTRGET)
            stored outside page zero - a pointer that outlives the call
  irq       for each interrupt handler: what its code (and what it calls)
            touches - AppleSoft page zero, the saved pointers, stores
            through page-zero pointers, AppleSoft calls

The verdict per file:
  HAZARD    interrupt code loads a saved variable pointer, or writes through
            a pointer that VARPNT/PTRGET filled
  IRQ+BASIC interrupt code touches AppleSoft's page zero or calls AppleSoft
  IRQ       an interrupt handler that does neither
  -         no interrupt handler found (static: relocated code is missed)
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- the ROM's labels (ref/applesoft.info) ----
LABELS = {}
for m in re.finditer(r'ADDR \$([0-9A-F]+); NAME "([^"]+)"', open(os.path.join(HERE, '../ref/applesoft.info')).read()):
    LABELS.setdefault(int(m.group(1), 16), m.group(2).rstrip('_'))
ROM_ENTRIES = sorted(a for a in LABELS if 0xD000 <= a <= 0xF7FF)


def rom_name(a):
    if a in LABELS:
        return LABELS[a]
    import bisect
    i = bisect.bisect_right(ROM_ENTRIES, a) - 1
    return f'{LABELS[ROM_ENTRIES[i]]}+{a - ROM_ENTRIES[i]}' if i >= 0 else f'${a:04X}'


# AppleSoft's page zero: the names worth reporting (the rest by label)
ZP_AS = {a: n for a, n in LABELS.items() if 0x50 <= a <= 0xFF}
VARPNT, FORPNT = 0x83, 0x85
PTRGET = {0xDFE3, 0xDFE8, 0xDFEA, 0xE007, 0xE053}     # PTRGET and its entries
TXTPTR = (0xB8, 0xB9)

# ---- 65C02 opcodes: mnemonic, mode ----
MODES = {'imp': 0, 'acc': 0, 'imm': 1, 'zp': 1, 'zpx': 1, 'zpy': 1, 'izx': 1, 'izy': 1, 'izp': 1,
         'rel': 1, 'abs': 2, 'abx': 2, 'aby': 2, 'ind': 2, 'iax': 2, 'zpr': 2}
OPS = {}
for line in """
00 BRK imp|01 ORA izx|04 TSB zp|05 ORA zp|06 ASL zp|08 PHP imp|09 ORA imm|0A ASL acc|0C TSB abs|0D ORA abs|0E ASL abs
10 BPL rel|11 ORA izy|12 ORA izp|14 TRB zp|15 ORA zpx|16 ASL zpx|18 CLC imp|19 ORA aby|1A INC acc|1C TRB abs|1D ORA abx|1E ASL abx
20 JSR abs|21 AND izx|24 BIT zp|25 AND zp|26 ROL zp|28 PLP imp|29 AND imm|2A ROL acc|2C BIT abs|2D AND abs|2E ROL abs
30 BMI rel|31 AND izy|32 AND izp|34 BIT zpx|35 AND zpx|36 ROL zpx|38 SEC imp|39 AND aby|3A DEC acc|3C BIT abx|3D AND abx|3E ROL abx
40 RTI imp|41 EOR izx|45 EOR zp|46 LSR zp|48 PHA imp|49 EOR imm|4A LSR acc|4C JMP abs|4D EOR abs|4E LSR abs
50 BVC rel|51 EOR izy|52 EOR izp|55 EOR zpx|56 LSR zpx|58 CLI imp|59 EOR aby|5A PHY imp|5D EOR abx|5E LSR abx
60 RTS imp|61 ADC izx|64 STZ zp|65 ADC zp|66 ROR zp|68 PLA imp|69 ADC imm|6A ROR acc|6C JMP ind|6D ADC abs|6E ROR abs
70 BVS rel|71 ADC izy|72 ADC izp|74 STZ zpx|75 ADC zpx|76 ROR zpx|78 SEI imp|79 ADC aby|7A PLY imp|7C JMP iax|7D ADC abx|7E ROR abx
80 BRA rel|81 STA izx|84 STY zp|85 STA zp|86 STX zp|88 DEY imp|89 BIT imm|8A TXA imp|8C STY abs|8D STA abs|8E STX abs
90 BCC rel|91 STA izy|92 STA izp|94 STY zpx|95 STA zpx|96 STX zpy|98 TYA imp|99 STA aby|9A TXS imp|9C STZ abs|9D STA abx|9E STZ abx
A0 LDY imm|A1 LDA izx|A2 LDX imm|A4 LDY zp|A5 LDA zp|A6 LDX zp|A8 TAY imp|A9 LDA imm|AA TAX imp|AC LDY abs|AD LDA abs|AE LDX abs
B0 BCS rel|B1 LDA izy|B2 LDA izp|B4 LDY zpx|B5 LDA zpx|B6 LDX zpy|B8 CLV imp|B9 LDA aby|BA TSX imp|BC LDY abx|BD LDA abx|BE LDX aby
C0 CPY imm|C1 CMP izx|C4 CPY zp|C5 CMP zp|C6 DEC zp|C8 INY imp|C9 CMP imm|CA DEX imp|CC CPY abs|CD CMP abs|CE DEC abs
D0 BNE rel|D1 CMP izy|D2 CMP izp|D5 CMP zpx|D6 DEC zpx|D8 CLD imp|D9 CMP aby|DA PHX imp|DD CMP abx|DE DEC abx
E0 CPX imm|E1 SBC izx|E4 CPX zp|E5 SBC zp|E6 INC zp|E8 INX imp|E9 SBC imm|EA NOP imp|EC CPX abs|ED SBC abs|EE INC abs
F0 BEQ rel|F1 SBC izy|F2 SBC izp|F5 SBC zpx|F6 INC zpx|F8 SED imp|F9 SBC aby|FA PLX imp|FD SBC abx|FE INC abx
""".replace('\n', '|').split('|'):
    if line.strip():
        h, m, mode = line.split()
        OPS[int(h, 16)] = (m, mode)

STORES = {'STA', 'STX', 'STY', 'STZ', 'INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR', 'TSB', 'TRB'}
VECTORS = {0x3F6: '&', 0x3F7: '&', 0x0B: 'USR', 0x0C: 'USR', 0x3FE: 'IRQ', 0x3FF: 'IRQ',
           0xFFFE: 'IRQ-LC', 0xFFFF: 'IRQ-LC', 0x36: 'CSW', 0x37: 'CSW', 0x38: 'KSW', 0x39: 'KSW'}
for a in range(0xB1, 0xC9):
    if a not in TXTPTR:
        VECTORS[a] = 'CHRGET-wedge'


class Scan:
    def __init__(self, data, org):
        self.m, self.org = data, org
        self.end = org + len(data)
        self.seen = {}                 # pc -> (mnemonic, mode, operand, length)
        self.vectors = []              # (kind, addr, value or None, at)
        self.rom = {}                  # rom address -> count
        self.zp = {}                   # AppleSoft zp -> 'r'/'w' set
        self.saves = {}                # absolute address -> what
        self.jsr_rom_at = {}           # pc -> rom target
        self.irq = []                  # handler addresses
        self.alloc_int = []
        self.calls = {}                # pc -> set of call targets (for reachability)
        self.notes = set()

    def byte(self, a):
        return self.m[a - self.org] if self.org <= a < self.end else None

    def word(self, a):
        lo, hi = self.byte(a), self.byte(a + 1)
        return None if lo is None or hi is None else lo | hi << 8

    def trace(self, entry, kind='entry'):
        work = [entry]
        while work:
            pc = work.pop()
            if pc in self.seen or not (self.org <= pc < self.end):
                continue
            regs = {'A': None, 'X': None, 'Y': None}
            last_ptrget = False         # A,Y = a variable's address (PTRGET just returned)
            src = {'A': None, 'X': None, 'Y': None}   # 'varpnt' / 'ptrget' when loaded from there
            while self.org <= pc < self.end and pc not in self.seen:
                op = self.byte(pc)
                if op not in OPS:
                    self.notes.add(f'bad opcode ${op:02X} at ${pc:04X}')
                    break
                mn, mode = OPS[op]
                n = MODES[mode]
                if pc + n >= self.end:
                    break
                v = self.byte(pc + 1) if n == 1 else self.word(pc + 1) if n == 2 else None
                self.seen[pc] = (mn, mode, v, n + 1)
                nxt = pc + 1 + n
                # page zero / absolute targets
                tgt = v if mode in ('zp', 'zpx', 'zpy', 'abs', 'abx', 'aby', 'izy', 'izx', 'izp') else None
                if tgt is not None and tgt in ZP_AS:
                    rw = 'w' if mn in STORES else 'r'
                    self.zp.setdefault(tgt, set()).add(rw)
                # register tracking
                if mode == 'imm' and mn in ('LDA', 'LDX', 'LDY'):
                    regs[mn[2]] = v
                    src[mn[2]] = None
                elif mn in ('LDA', 'LDX', 'LDY') and tgt is not None:
                    regs[mn[2]] = None
                    src[mn[2]] = 'varpnt' if tgt in (VARPNT, VARPNT + 1) else 'forpnt' if tgt in (FORPNT, FORPNT + 1) else None
                elif mn in ('TAX', 'TAY', 'TXA', 'TYA'):
                    regs[mn[2]], src[mn[2]] = regs[mn[1]], src[mn[1]]
                elif mn in ('INX', 'INY', 'DEX', 'DEY', 'INC', 'DEC') and mode in ('imp', 'acc'):
                    r = mn[2] if mn[2] in 'XY' else 'A'
                    regs[r] = None if regs[r] is None else (regs[r] + (1 if mn.startswith('IN') else -1)) & 0xFF
                elif mn in ('ADC', 'SBC', 'AND', 'ORA', 'EOR', 'ASL', 'LSR', 'ROL', 'ROR', 'PLA') and mode in ('imm', 'acc', 'imp', 'zp', 'abs', 'abx', 'aby', 'zpx', 'izy', 'izx', 'izp'):
                    if mn in ('PLA',) or mode == 'acc' or mn in ('ADC', 'SBC', 'AND', 'ORA', 'EOR'):
                        regs['A'] = None
                        src['A'] = None
                if mn in ('PLX', 'PLY'):
                    regs[mn[2]] = None
                # stores: vectors, saved pointers
                if mn in ('STA', 'STX', 'STY', 'STZ') and mode in ('zp', 'abs'):
                    r = {'STA': 'A', 'STX': 'X', 'STY': 'Y', 'STZ': None}[mn]
                    val = 0 if r is None else regs[r]
                    if v in VECTORS:
                        self.vectors.append((VECTORS[v], v, val, pc))
                    s = src[r] if r else None
                    if r and last_ptrget and r in 'AY':
                        s = s or 'ptrget'
                    if s and v >= 0x100:              # outside page zero: it outlives the call
                        self.saves[v] = (s, pc)
                if mn in ('STA', 'STX', 'STY') and mode in ('abx', 'aby', 'izy', 'izx') and v in VECTORS:
                    self.vectors.append((VECTORS[v], v, None, pc))
                # control flow
                if mn == 'JSR':
                    self.calls.setdefault(pc, set()).add(v)
                    last_ptrget = v in PTRGET
                    if v == 0xBF00:                         # MLI: command, parameters inline
                        cmd, parm = self.byte(nxt), self.word(nxt + 1)
                        if cmd == 0x40 and parm is not None:
                            h = self.word(parm + 2)
                            self.alloc_int.append((pc, h))
                            if h is not None:
                                self.irq.append(h)
                        nxt += 3
                        regs = {'A': None, 'X': None, 'Y': None}
                    elif 0xD000 <= v <= 0xF7FF:
                        self.rom[v] = self.rom.get(v, 0) + 1
                        self.jsr_rom_at[pc] = v
                        regs = {'A': None, 'X': None, 'Y': None}
                        src = {'A': 'ptrget' if v in PTRGET else None, 'X': None, 'Y': 'ptrget' if v in PTRGET else None}
                    else:
                        work.append(v)
                        regs = {'A': None, 'X': None, 'Y': None}
                        src = {'A': None, 'X': None, 'Y': None}
                    pc = nxt
                    continue
                if mn not in ('STA', 'STX', 'STY', 'CMP', 'CPX', 'CPY', 'BIT') and mn != 'JSR':
                    if not (mn.startswith('LD') or mn.startswith('T')):
                        last_ptrget = False if mn not in ('PHA', 'PHX', 'PHY', 'PLA', 'PLX', 'PLY') else last_ptrget
                if mode == 'rel':
                    off = v - 256 if v >= 128 else v
                    work.append(nxt + off)
                    if mn == 'BRA':
                        break
                    pc = nxt
                    continue
                if mn == 'JMP':
                    if mode == 'abs':
                        if 0xD000 <= v <= 0xF7FF:
                            self.rom[v] = self.rom.get(v, 0) + 1
                            self.jsr_rom_at[pc] = v
                        else:
                            self.calls.setdefault(pc, set()).add(v)
                            work.append(v)
                    elif mode == 'ind':
                        self.notes.add(f'JMP (${v:04X}) at ${pc:04X}')
                    break
                if mn in ('RTS', 'RTI', 'BRK'):
                    break
                pc = nxt

    def run(self):
        self.trace(self.org)
        # vectors pointing back into the file: trace them too
        changed = True
        while changed:
            changed = False
            targets = self.vector_targets()
            for kind, t in targets:
                if t is not None and self.org <= t < self.end and t not in self.seen:
                    self.trace(t, kind)
                    changed = True
                if kind.startswith('IRQ') and t is not None and t not in self.irq:
                    self.irq.append(t)
            for h in list(self.irq):
                if h is not None and self.org <= h < self.end and h not in self.seen:
                    self.trace(h, 'IRQ')
                    changed = True

    def vector_targets(self):
        """& / USR / IRQ targets from lo/hi stores of known values"""
        out = []
        by = {}
        for kind, a, val, at in self.vectors:
            by.setdefault(kind, {})[a] = val
        for kind, base in (('&', 0x3F6), ('USR', 0x0B), ('IRQ', 0x3FE), ('IRQ-LC', 0xFFFE)):
            d = by.get(kind, {})
            lo, hi = d.get(base), d.get(base + 1)
            if base in d or base + 1 in d:
                out.append((kind, (lo | hi << 8) if lo is not None and hi is not None else None))
        return out

    def reach(self, entry):
        """code reachable from entry (in-file calls followed), as a set of pcs"""
        out, work = set(), [entry]
        while work:
            pc = work.pop()
            while pc in self.seen and pc not in out:
                out.add(pc)
                mn, mode, v, n = self.seen[pc]
                if mn == 'JSR' and v is not None and self.org <= v < self.end:
                    work.append(v)
                if mode == 'rel':
                    off = v - 256 if v >= 128 else v
                    work.append(pc + n + off)
                    if mn == 'BRA':
                        break
                if mn == 'JMP':
                    if mode == 'abs' and self.org <= v < self.end:
                        work.append(v)
                    break
                if mn in ('RTS', 'RTI', 'BRK'):
                    break
                pc += n + (3 if mn == 'JSR' and v == 0xBF00 else 0)
        return out

    def irq_report(self, h):
        pcs = self.reach(h)
        zp, rom, saved, ind = set(), set(), set(), set()
        for pc in sorted(pcs):
            mn, mode, v, n = self.seen[pc]
            if mode in ('zp', 'zpx', 'zpy', 'abs', 'abx', 'aby') and v in ZP_AS:
                zp.add(f'{LABELS[v].rstrip("_")}{"<-" if mn in STORES else ""}')
            if mode in ('abs', 'abx', 'aby') and v in self.saves:
                saved.add(f'${v:04X}')
            if mn in ('STA', 'INC', 'DEC') and mode in ('izy', 'izx', 'izp'):
                ind.add(f'(${v:02X})')
            if pc in self.jsr_rom_at:
                rom.add(rom_name(self.jsr_rom_at[pc]))
        return pcs, zp, rom, saved, ind


def load(path):
    m = re.search(r'#([0-9A-F]{2})([0-9A-F]{4})$', path)
    typ, aux = (int(m.group(1), 16), int(m.group(2), 16)) if m else (6, 0x300)
    org = 0x2000 if typ == 0xFF else aux
    return open(path, 'rb').read(), org


def analyse(path):
    data, org = load(path)
    s = Scan(data, org)
    s.run()
    irqs = []
    hazard, basic = False, False
    for h in dict.fromkeys(s.irq):
        if h is None or not (s.org <= h < s.end):
            irqs.append((h, None))
            continue
        pcs, zp, rom, saved, ind = s.irq_report(h)
        irqs.append((h, (len(pcs), zp, rom, saved, ind)))
        if saved or (ind and any(v in ('ptrget', 'varpnt') for v, _ in s.saves.values())):
            hazard = True
        if zp or rom:
            basic = True
    verdict = 'HAZARD' if hazard else 'IRQ+BASIC' if basic else 'IRQ' if s.irq else '-'
    return s, irqs, verdict


def report(path):
    s, irqs, verdict = analyse(path)
    print(f'== {path}  (org ${s.org:04X}, {len(s.m)} bytes, {len(s.seen)} instructions traced)  {verdict}')
    for kind, a, val, at in s.vectors:
        print(f'  vector {kind:12s} ${a:04X} <- {"$%02X" % val if val is not None else "?"}  at ${at:04X}')
    for kind, t in s.vector_targets():
        print(f'  {kind} -> {"$%04X" % t if t is not None else "computed"}')
    for at, h in s.alloc_int:
        print(f'  ALLOC_INTERRUPT at ${at:04X} -> {"$%04X" % h if h is not None else "?"}')
    if s.rom:
        print('  rom: ' + ', '.join(f'{rom_name(a)}x{c}' for a, c in sorted(s.rom.items())))
    if s.zp:
        print('  zp: ' + ', '.join(f'{LABELS[a].rstrip("_")}({"".join(sorted(rw))})' for a, rw in sorted(s.zp.items())))
    for a, (what, at) in sorted(s.saves.items()):
        print(f'  SAVES {what} -> ${a:04X} at ${at:04X}')
    for h, r in irqs:
        if r is None:
            print(f'  irq handler {"$%04X" % h if h is not None else "?"}: outside the file')
            continue
        n, zp, rom, saved, ind = r
        print(f'  irq handler ${h:04X}: {n} instr; zp {sorted(zp) or "-"}; rom {sorted(rom) or "-"}; saved-ptr loads {sorted(saved) or "-"}; indirect stores {sorted(ind) or "-"}')
    for note in sorted(s.notes)[:6]:
        print(f'  note: {note}')


def tsv(path):
    s, irqs, verdict = analyse(path)
    kinds = sorted({k for k, *_ in s.vectors})
    print('\t'.join([verdict, ','.join(kinds) or '-', str(len(s.saves)), str(len(s.rom)), f'{len(s.seen)}/{len(s.m)}', path]))


if __name__ == '__main__':
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    if args[0] == '--tsv':
        for p in args[1:]:
            try:
                tsv(p)
            except Exception as e:
                print(f'ERROR\t{e}\t\t\t\t{p}')
    else:
        for p in args:
            report(p)
