#!/usr/bin/env python3
"""ampgrep.py FILE... - byte patterns that survive relocation (absolute
addresses of fixed locations), for files the tracer could not follow:

  irq   STA/STX/STY $3FE or $3FF, $FFFE/$FFFF; JSR $BF00 + ALLOC_INTERRUPT
  amp   STA/STX/STY $3F5-$3F7
  ptr   JSR PTRGET ($DFE3/$DFE8/$DFEA/$E007/$E053)
  vp    LDA/LDX/LDY VARPNT ($83/$84) or FORPNT ($85/$86)
  rti   RTI after a PLA (a handler's usual end)
One line per file: the flags, then the path.
"""
import re
import sys

ST = (0x8D, 0x8E, 0x8C)
for p in sys.argv[1:]:
    d = open(p, 'rb').read()
    f = []
    if any(bytes([o, lo, hi]) in d for o in ST for lo, hi in ((0xFE, 3), (0xFF, 3), (0xFE, 0xFF), (0xFF, 0xFF))) \
            or b'\x20\x00\xBF\x40' in d:
        f.append('irq')
    if any(bytes([o, lo, 3]) in d for o in ST for lo in (0xF5, 0xF6, 0xF7)):
        f.append('amp')
    if any(bytes([0x20, a & 0xFF, a >> 8]) in d for a in (0xDFE3, 0xDFE8, 0xDFEA, 0xE007, 0xE053)):
        f.append('ptr')
    if any(bytes([o, z]) in d for o in (0xA5, 0xA6, 0xA4) for z in (0x83, 0x84, 0x85, 0x86)):
        f.append('vp')
    if b'\x68\x40' in d:
        f.append('rti')
    print(' '.join(f) or '-', p, sep='\t')
