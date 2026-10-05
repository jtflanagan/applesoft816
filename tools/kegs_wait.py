#!/usr/bin/env python3
"""kegs_wait.py MODE CEILING - wait for the KEGS on $DISPLAY, then print its
text screen (trailing blank lines dropped).  Polls (xwd + kegs_text) until
the condition holds on two reads in a row, or CEILING seconds pass:

  home    the screen is only a prompt (HOME has run)
  cmd     more than a bare prompt, and the last line a bare prompt (the
          command typed after HOME has finished)
  prompt  the last line a bare prompt (a boot, a Ctrl-Reset)

A bare prompt is "]" and at most one cell (the cursor, which blinks: it is
left out of the comparison).
"""
import os
import re
import struct
import subprocess
import sys
import time

from PIL import Image

os.environ["DISPLAY"] = os.environ.get("KEGS_DISPLAY", os.environ.get("DISPLAY", ":99"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kegs_text  # noqa: E402

BARE = re.compile(r"^\](.)?$")


def window():
    for _ in range(50):
        out = subprocess.run(["xdotool", "search", "--name", "KEGS"], capture_output=True, text=True).stdout.split()
        if out:
            return out[0]
        time.sleep(0.1)
    raise SystemExit("kegs_wait: no KEGS window")


def grab(w):
    d = subprocess.run(["xwd", "-silent", "-id", w], capture_output=True).stdout
    hs, = struct.unpack(">I", d[0:4])
    width, height = struct.unpack(">II", d[16:24])
    bpl, = struct.unpack(">I", d[48:52])
    nc, = struct.unpack(">I", d[76:80])
    off = hs + nc * 12
    return Image.frombuffer("RGB", (width, height), d[off:off + bpl * height], "raw", "BGRX", bpl, 1)


def screen(w):
    lines = kegs_text.read(grab(w))
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


def key(lines):                       # for comparing reads: the cursor cell out
    return [("]" if BARE.match(l) else l) for l in lines]


def holds(mode, lines):
    nonblank = [l for l in lines if l.strip()]
    if not nonblank or not BARE.match(nonblank[-1]):
        return False
    if mode == "home":
        return len(nonblank) == 1
    if mode == "cmd":
        return len(nonblank) > 1
    return True


def main():
    mode, ceiling = sys.argv[1], float(sys.argv[2])
    w = window()
    end = time.time() + ceiling
    prev = None
    while True:
        lines = screen(w)
        if holds(mode, lines) and prev is not None and key(lines) == key(prev):
            break
        if time.time() >= end:
            break
        prev = lines
        time.sleep(0.05)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
