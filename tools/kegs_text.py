#!/usr/bin/env python3
"""The text screen of a KEGS screenshot, as text: kegs_text.py SHOT.png

Each character cell is matched exactly against KEGS's own font
(src/kegsfont.h): the text area is 560x384 from x = 32, 24 rows of 16
pixels, 14-pixel cells in 40 columns and 7 in 80.  Its top moves between
KEGS runs (y = 32 or 48 seen), so every top lined up with the first lit
pixel row between 24 and 56 is tried (the lower one first: a tie means
the top row was blank), at both widths; the reading with the most
characters (less cells it cannot read) wins.  Inverse and flashing characters come out as
their plain letters; a cell matching nothing is '~'.
"""
import os
import re
import sys

from PIL import Image

FONT = os.path.expanduser("~/repos/kegs.1.38/src/kegsfont.h")
X0 = 32


_TABLE = None


def glyphs():
    global _TABLE
    if _TABLE is not None:
        return _TABLE
    rows = [list(map(lambda v: int(v, 16), re.findall(r"0x([0-9a-f]{2})", l)))
            for l in open(FONT) if l.strip().startswith("{")]
    table = {}
    for code, r in enumerate(rows):
        bits = tuple(tuple((b >> (7 - k)) & 1 for k in range(7)) for b in r)
        if code >= 0x80:                 # normal: screen code -> ASCII
            ch = chr(code & 0x7F)
        else:                            # inverse/flash: shown as the letter
            ch = chr((code & 0x3F) | (0x40 if code & 0x3F < 0x20 else 0))
            bits = tuple(tuple(1 - b for b in row) for row in bits)
        table.setdefault(bits, ch)
    _TABLE = table
    return table


def read(path):
    """the 24 lines of a screenshot (a path, or a PIL image)"""
    im = (path if isinstance(path, Image.Image) else Image.open(path)).convert("RGB")
    w, h = im.size
    data = im.tobytes()
    lit = lambda x, y: 1 if min(data[3 * (y * w + x):3 * (y * w + x) + 3]) > 128 else 0
    table = glyphs()
    first = next((y for y in range(16, h - 30) if any(lit(x, y) for x in range(X0, X0 + 560))), 48)
    best = None
    for y0 in range(56 - (56 - (first - 1) % 16) % 16, 23, -16):   # 24..56, the lower one first
        if y0 + 384 > h:
            continue
        for cols, cw, step in ((40, 14, 2), (80, 7, 1)):
            lines, good = [], 0             # (non-blank cells read, less cells not read)
            for r in range(24):
                s = ""
                for c in range(cols):
                    cell = tuple(tuple(lit(X0 + c * cw + k * step + (step - 1), y0 + r * 16 + 2 * j + 1)
                                       for k in range(7)) for j in range(8))
                    ch = table.get(cell)
                    if ch is None:
                        good -= 1
                    elif ch != " ":
                        good += 1
                    s += ch if ch is not None else "~"
                lines.append(s.rstrip())
            if best is None or good > best[0]:
                best = (good, lines)
    return best[1]


if __name__ == "__main__":
    print("\n".join(read(sys.argv[1])))
