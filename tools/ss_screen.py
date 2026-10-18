#!/usr/bin/env python3
"""Screenshot of the board's display: copies the scanned frame buffer (480x800, B,G,R) of a running
app over J-Link (about 18 s at 64 KB/s) and writes a PNG.

    tools/ss_screen.py out.png [--elf firmware/out/soundsense/E8-HP/debug/soundsense.elf] [--scale 0.5]
"""
import argparse
import os
import sys
import tempfile

import numpy as np
from PIL import Image

T = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(T)
sys.path.insert(0, T)
import jlink   # noqa: E402

W, H = 480, 800


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--elf", default=os.path.join(ROOT, "firmware/out/soundsense/E8-HP/debug/soundsense.elf"))
    ap.add_argument("--symbol", default="fb")
    ap.add_argument("--scale", type=float, default=1.0)
    a = ap.parse_args()
    b = jlink.Board(a.elf)
    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, "fb.bin")
        for attempt in range(3):   # the RTT logger shares the probe
            try:
                b.dump(b.addr(a.symbol), W * H * 3, raw)
                break
            except RuntimeError:
                if attempt == 2:
                    raise
        px = np.fromfile(raw, dtype=np.uint8).reshape(H, W, 3)[:, :, ::-1]
    im = Image.fromarray(px)
    if a.scale != 1.0:
        im = im.resize((int(W * a.scale), int(H * a.scale)), Image.LANCZOS)
    im.save(a.out)
    print(a.out)


if __name__ == "__main__":
    main()
