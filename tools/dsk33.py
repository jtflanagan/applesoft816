#!/usr/bin/env python3
"""DOS 3.3 disk images (.dsk, DOS sector order): catalog and extract files.

  dsk33.py cat IMAGE              list the catalog (type, sectors, name)
  dsk33.py get IMAGE NAME OUT     write NAME's contents to OUT

For an AppleSoft (A) or Integer (I) file the 2-byte length header is
stripped, so OUT is the tokenized program exactly as it sits at $801.  For a
binary (B) file the 4-byte address/length header is stripped and the load
address printed.  Text (T) files are written as stored, high bits intact.
"""
import sys

TYPES = {0x00: 'T', 0x01: 'I', 0x02: 'A', 0x04: 'B', 0x08: 'S', 0x10: 'R', 0x20: 'a', 0x40: 'b'}


class Disk:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        if len(self.d) != 35 * 16 * 256:
            raise SystemExit(f'{path}: {len(self.d)} bytes, not a 140K DOS-order image')

    def sector(self, t, s):
        o = (t * 16 + s) * 256
        return self.d[o:o + 256]

    def entries(self):
        vtoc = self.sector(17, 0)
        t, s = vtoc[1], vtoc[2]
        while t:
            c = self.sector(t, s)
            for i in range(7):
                e = c[11 + i * 35:11 + i * 35 + 35]
                if e[0] in (0x00, 0xFF):        # never used / deleted
                    continue
                name = bytes(b & 0x7F for b in e[3:33]).decode('ascii').rstrip()
                yield {'name': name, 'type': TYPES.get(e[2] & 0x7F, '?'),
                       'locked': bool(e[2] & 0x80), 'ts': (e[0], e[1]),
                       'sectors': e[33] | e[34] << 8}
            t, s = c[1], c[2]

    def raw(self, ts):
        out = bytearray()
        t, s = ts
        while t:
            tsl = self.sector(t, s)
            for k in range(122):
                dt, ds = tsl[12 + 2 * k], tsl[13 + 2 * k]
                if dt or ds:
                    out += self.sector(dt, ds)
            t, s = tsl[1], tsl[2]
        return bytes(out)

    def get(self, name):
        for e in self.entries():
            if e['name'] == name:
                r = self.raw(e['ts'])
                if e['type'] in 'AI':
                    n = r[0] | r[1] << 8
                    return e, r[2:2 + n], None
                if e['type'] == 'B':
                    a, n = r[0] | r[1] << 8, r[2] | r[3] << 8
                    return e, r[4:4 + n], a
                return e, r, None
        raise SystemExit(f'{name}: not in the catalog')


def main(argv):
    if len(argv) >= 3 and argv[1] == 'cat':
        for e in Disk(argv[2]).entries():
            print(f"{'*' if e['locked'] else ' '}{e['type']} {e['sectors']:03d} {e['name']}")
    elif len(argv) == 5 and argv[1] == 'get':
        e, data, addr = Disk(argv[2]).get(argv[3])
        open(argv[4], 'wb').write(data)
        print(f"{e['type']} {argv[3]}: {len(data)} bytes" + (f' at ${addr:04X}' if addr is not None else ''))
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main(sys.argv)
