#!/usr/bin/env python3
"""C3 rows 2-4 without any sound: fake recognised sounds over J-Link (host command 13) and read the
board's own timing from the RTT log.

    tools/rtt_log.sh soundsense release docs/gates/c3_rtt.log &     # logger first
    .venv/bin/python tools/c3_ui.py [log]

Row 2: [C3] "on screen N us after the decision" for an important alert, a felt knock and a heard-only
knock (banner). Row 3: an urgent alert (glass: strobe, a full redraw every 500 ms) with host command 14
forcing full redraws 10 times a second on top; "[C3] alert ... ended" gives the longest draw + present
and the audio blocks dropped. Row 4: the "[SS] draw" stats line on the home screen (live view fps).
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g3_play as g  # noqa: E402

g.LOG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(g.ROOT, "docs/gates/c3_rtt.log")
KNOCK, ALARM, GLASS = 0, 3, 4
FELT, HEARD = 0x200, 0x400


def run(label, steps, wait):
    n0 = g.log_size()
    print(label, flush=True)
    for cmd, arg in steps:
        g.command(cmd, arg)
    time.sleep(wait)
    for ln in g.new_lines(n0):
        if "[C3]" in ln or "injected" in ln or "[SS] draw" in ln or "dropped" in ln and "stats" in ln:
            print("   ", ln.strip()[:170], flush=True)


g.command(3)
time.sleep(1)
run("important alert (Alarm)", [(13, ALARM)], 3)
g.command(3)
run("knock felt through the table", [(13, KNOCK | FELT)], 3)
g.command(3)
run("knock heard only (banner)", [(13, KNOCK | HEARD)], 7)
run("urgent alert (Glass) with full redraws 10/s for 12 s", [(13, GLASS), (14, 12)], 17)
run("home screen, live view", [(3, 0)], 21)
