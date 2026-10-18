#!/usr/bin/env python3
"""C2 rows 2 and 3 (docs/HANDOFF-gates.md): taught sounds survive power loss.

Needs the soundsense app on the board and tools/rtt_log.sh appending its RTT to the log file. After
every boot it reports what the board loaded from flash and, for row 2, starts a long save loop again
so the next power pull lands during a save. Pull the USB power at any moment, plug it back, wait for
the next "boot" line here, and repeat.

    tools/c2_power.py pulls --n 5        # row 2: power pulled during back-to-back saves
    tools/c2_power.py cycles --n 5       # row 3: normal power cycles, no saving
"""
import argparse
import os
import re
import sys
import time

T = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(T)
sys.path.insert(0, T)
import jlink   # noqa: E402

ELF = os.path.join(ROOT, "firmware/out/soundsense/E8-HP/release/soundsense.elf")


def command(b, cmd, arg=0):
    """Write a host command; retry, since the RTT logger shares the probe."""
    for _ in range(10):
        try:
            b.set("ss_arg", arg)
            b.set("ss_cmd", cmd)
            time.sleep(0.3)
            if b.get("ss_cmd") == 0:
                return True
        except (RuntimeError, IndexError):
            pass
        time.sleep(0.5)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["pulls", "cycles"])
    ap.add_argument("--n", type=int, default=5)
    ap.add_argument("--log", default=os.path.join(ROOT, "docs/gates/c2_rtt.log"))
    a = ap.parse_args()
    b = jlink.Board(ELF)

    pos = os.path.getsize(a.log)
    boots, ok, buf = 0, 0, ""
    if a.mode == "pulls":
        print("starting the save loop; pull the USB power whenever you like", flush=True)
        command(b, 11, 1000)   # about 4 min of saves; caps flash wear if left running
    while boots < a.n:
        time.sleep(1)
        with open(a.log, "rb") as f:
            f.seek(pos)
            raw = f.read()
        pos += len(raw)
        buf += raw.decode(errors="replace")
        if "[SS] listening" not in buf:
            continue
        boot, buf = buf.split("[SS] listening", 1)
        copies = re.findall(r"\[STORE\] copy A (\w+) \(seq (\d+)\), copy B (\w+) \(seq (\d+)\)", boot)
        loaded = re.findall(r"\[STORE\] loaded (\d+) taught sounds from copy (.), seq (\d+), in (\d+) us", boot)
        saves = re.findall(r"\[STORE\] save (\d+) ok: copy (.) seq (\d+)", boot)
        boots += 1
        good = bool(loaded)
        ok += good
        last = f"last save before the pull: copy {saves[-1][1]} seq {saves[-1][2]}" if saves else "no saves logged"
        print(f"boot {boots}: {copies[-1] if copies else '?'} -> "
              f"{'loaded %s sounds from copy %s seq %s in %s us' % loaded[-1] if good else 'NOTHING LOADED'}; {last}",
              flush=True)
        if a.mode == "pulls" and boots < a.n:
            time.sleep(1)
            command(b, 11, 1000)   # about 4 min of saves; caps flash wear if left running
            print("  save loop running again; pull the power again", flush=True)
    print(f"RESULT {a.mode}: {ok}/{boots} boots loaded a valid copy")


if __name__ == "__main__":
    main()
