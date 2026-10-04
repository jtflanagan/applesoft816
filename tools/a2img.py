#!/usr/bin/env python3
"""Apple II 140K disk images: every file out, DOS 3.3 or ProDOS, either sector order.

  a2img.py ls IMAGE            the files: type, aux (load address), length, name
  a2img.py x IMAGE DIR         each file into DIR, named NAME#TTAAAA (ProDOS type
                               and aux in hex, CiderPress style), and DIR/index.tsv

DOS 3.3 types map to ProDOS ones: T -> $04 TXT, I -> $FA INT, A -> $FC BAS
(aux $0801), B -> $06 BIN (aux = the load address, the 4-byte header off).
AppleSoft and Integer files lose their 2-byte length header, so a BAS file is
the tokenized program as it sits at $0801.  Text files keep their bytes.
"""
import os
import sys

# ProDOS block n of a track, as two DOS 3.3 logical sectors
P2D = [(0, 14), (13, 12), (11, 10), (9, 8), (7, 6), (5, 4), (3, 2), (1, 15)]


class Image:
    def __init__(self, path):
        d = open(path, 'rb').read()
        if d[:4] == b'2IMG':                       # .2mg: the header says where the data is
            off = int.from_bytes(d[0x18:0x1C], 'little')
            n = int.from_bytes(d[0x1C:0x20], 'little')
            fmt = int.from_bytes(d[0x0C:0x10], 'little')
            d, self.order = d[off:off + n], ('dos' if fmt == 0 else 'prodos')
        else:
            self.order = 'prodos' if path.lower().endswith('.po') else 'dos'
        self.d = d

    def sector(self, t, s):                        # DOS 3.3 logical sector
        if self.order == 'dos':
            o = (t * 16 + s) * 256
            return self.d[o:o + 256]
        b = t * 8 + next(i for i, p in enumerate(P2D) if s in p)
        half = P2D[b % 8].index(s)
        return self.block(b)[half * 256:half * 256 + 256]

    def block(self, n):                           # ProDOS block
        if self.order == 'prodos':
            return self.d[n * 512:n * 512 + 512]
        t, (s0, s1) = n // 8, P2D[n % 8]
        o0, o1 = (t * 16 + s0) * 256, (t * 16 + s1) * 256
        return self.d[o0:o0 + 256] + self.d[o1:o1 + 256]


def is_prodos(img):
    b = img.block(2)
    return len(b) == 512 and b[4] >> 4 == 0xF and b[0x23] == 0x27 and b[0x24] == 0x0D


def is_dos33(img):
    v = img.sector(17, 0)
    return len(v) == 256 and v[0x27] == 122 and 1 <= v[1] < 35 and v[2] < 16 and v[0x34] in (35, 40)


def dos33_files(img):
    v = img.sector(17, 0)
    t, s, seen = v[1], v[2], set()
    while t and (t, s) not in seen and t < 35:
        seen.add((t, s))
        c = img.sector(t, s)
        for e in range(7):
            o = 0x0B + e * 35
            ts_t, ts_s, typ = c[o], c[o + 1], c[o + 2]
            if ts_t in (0, 0xFF):
                continue
            name = bytes(x & 0x7F for x in c[o + 3:o + 33]).decode('latin1').rstrip()
            data, tt, ss, guard = b'', ts_t, ts_s, set()
            while tt and (tt, ss) not in guard and tt < 35:
                guard.add((tt, ss))
                tsl = img.sector(tt, ss)
                for k in range(122):
                    pt, ps = tsl[0x0C + 2 * k], tsl[0x0D + 2 * k]
                    data += img.sector(pt, ps) if pt or ps else bytes(256)
                tt, ss = tsl[1], tsl[2]
            kind = typ & 0x7F
            if kind == 0x04 and len(data) >= 4:      # B: address, length
                aux, n = data[0] | data[1] << 8, data[2] | data[3] << 8
                yield name, 0x06, aux, data[4:4 + n]
            elif kind in (0x01, 0x02) and len(data) >= 2:
                n = data[0] | data[1] << 8
                yield name, 0xFA if kind == 1 else 0xFC, 0x0801, data[2:2 + n]
            elif kind == 0x00:
                end = data.find(b'\0')
                yield name, 0x04, 0, data[:end if end >= 0 else len(data)]
            else:
                yield name, {0x08: 0xF1, 0x10: 0xF2, 0x20: 0xF3, 0x40: 0xF4}.get(kind, 0x00), 0, data
        t, s = c[1], c[2]


def prodos_files(img, key=2, prefix=''):
    blk, seen, first = key, set(), True
    while blk and blk not in seen and blk * 512 < len(img.d):
        seen.add(blk)
        b = img.block(blk)
        for e in range(13):
            if first and e == 0:
                continue
            o = 4 + e * 39
            st, nl = b[o] >> 4, b[o] & 0x0F
            if st == 0:
                continue
            name = b[o + 1:o + 1 + nl].decode('latin1')
            typ, ptr = b[o + 0x10], b[o + 0x11] | b[o + 0x12] << 8
            eof = b[o + 0x15] | b[o + 0x16] << 8 | b[o + 0x17] << 16
            aux = b[o + 0x1F] | b[o + 0x20] << 8
            if st == 0xD:
                yield from prodos_files(img, ptr, prefix + name + '/')
                continue
            yield prefix + name, typ, aux, read_fork(img, st, ptr, eof)
        first = False
        blk = b[2] | b[3] << 8


def read_fork(img, st, ptr, eof):
    def index(n):
        b = img.block(n)
        return [b[i] | b[256 + i] << 8 for i in range(256)]
    if st == 1:
        blocks = [ptr]
    elif st == 2:
        blocks = index(ptr)
    elif st == 3:
        blocks = [x for i in index(ptr) if i for x in index(i)]
    else:
        return b''
    data = b''.join(img.block(n) if n else bytes(512) for n in blocks[:(eof + 511) // 512])
    return data[:eof]


def files(path):
    img = Image(path)
    if is_prodos(img):
        return 'prodos', list(prodos_files(img))
    if is_dos33(img):
        return 'dos33', list(dos33_files(img))
    if img.order == 'dos':                         # a ProDOS-order image named .dsk?
        img.order = 'prodos'
        if is_prodos(img):
            return 'prodos', list(prodos_files(img))
    return 'unknown', []


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ('ls', 'x'):
        raise SystemExit(__doc__)
    fs, fl = files(sys.argv[2])
    if sys.argv[1] == 'ls':
        print(f'# {fs}')
        for name, typ, aux, data in fl:
            print(f'{typ:02X} {aux:04X} {len(data):6d} {name}')
        return
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'index.tsv'), 'w') as ix:
        for name, typ, aux, data in fl:
            safe = ''.join(c if c.isprintable() and c not in '/ ' else '_' for c in name)
            fn = f'{safe}#{typ:02X}{aux:04X}'
            open(os.path.join(out, fn), 'wb').write(data)
            ix.write(f'{fn}\t{typ:02X}\t{aux:04X}\t{len(data)}\t{name}\n')
    print(f'{sys.argv[2]}: {fs}, {len(fl)} files')


if __name__ == '__main__':
    main()
