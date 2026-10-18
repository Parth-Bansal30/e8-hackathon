#!/usr/bin/env python3
"""Statistical CPU profile of the running board: sample the DWT PC sample register (PCSR,
0xE000101C) over J-Link without halting, and map each PC to its function with nm. This found the
slow joystick reads behind C5 (67% of the time in ARM_LPGPIO_GetValue).

    tools/pc_profile.py [elf] [samples]       defaults: the soundsense release ELF, 400 samples

Time asleep in WFI shows up in the function holding the WFI instruction (main for soundsense), so
compare with the "active" figure in the [SS] stats line. PCSR reads 0xFFFFFFFF when it has no
sample. Stop tools/rtt_log.sh first: both share the probe.
"""
import bisect
import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jlink  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
elf = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else os.path.join(ROOT, "firmware/out/soundsense/E8-HP/release/soundsense.elf")
n = int(sys.argv[2]) if len(sys.argv) > 2 else 400

out = jlink.Board(elf).run(["mem32 0xE000101C 1"] * n)
pcs = [int(m.group(1), 16) for m in re.finditer(r"^E000101C = ([0-9A-Fa-f]{8})", out, re.M)]

syms = []
nm = subprocess.run([os.path.join(jlink._gcc_bin(), "arm-none-eabi-nm"), "-n", "-C", elf],
                    capture_output=True, text=True, check=True).stdout
for line in nm.splitlines():
    p = line.split(None, 2)
    if len(p) == 3 and p[1] in "tTWw":
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]

count = collections.Counter()
for pc in pcs:
    if pc == 0xFFFFFFFF:
        count["<no sample>"] += 1
        continue
    i = bisect.bisect_right(addrs, pc & ~1) - 1
    count[syms[i][1] if i >= 0 else hex(pc)] += 1
print(len(pcs), "samples")
for name, k in count.most_common(14):
    print(f"{100 * k / max(1, len(pcs)):5.1f}% {name}")
