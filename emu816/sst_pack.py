#!/usr/bin/env python3
"""Pack SingleStepTests/65816 JSON (one test per line) into sst_run's binary.

  sst_pack.py OUT.bin FILE.json...      (e.g. the firmware's sampled set in
                                         strudelworks-firmware/sim/cpu816_work/samples)

Per test, little-endian:
  regs initial: u16 pc, u16 s, u8 p, u16 a, u16 x, u16 y, u8 dbr, u16 d, u8 pbr, u8 e
  u16 n, n x (u32 addr, u8 value)            initial memory
  regs final, u16 n, n x (u32, u8)           final memory
  u16 cycles
  u8 name length, name                      (the test's own name, for reports)
"""
import json
import struct
import sys

REGS = struct.Struct('<HHBHHHBHBB')


def regs(r):
    return REGS.pack(r['pc'], r['s'], r['p'], r['a'], r['x'], r['y'], r['dbr'], r['d'], r['pbr'], r['e'])


def mem(entries):
    return struct.pack('<H', len(entries)) + b''.join(struct.pack('<IB', a, v) for a, v in entries)


def tests(path):
    with open(path) as f:
        for line in f:
            line = line.strip().lstrip('[').rstrip(',').rstrip(']')
            if not line.endswith('}'):
                continue  # a sampled file's truncated last line
            try:
                yield json.loads(line)
            except json.JSONDecodeError:
                continue


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    n = 0
    with open(argv[1], 'wb') as out:
        for path in argv[2:]:
            for t in tests(path):
                name = t['name'].encode()[:255]
                out.write(regs(t['initial']) + mem(t['initial']['ram']) + regs(t['final']) +
                          mem(t['final']['ram']) + struct.pack('<HB', len(t['cycles']), len(name)) + name)
                n += 1
    print(f'{n} tests -> {argv[1]}')


if __name__ == '__main__':
    main(sys.argv)
