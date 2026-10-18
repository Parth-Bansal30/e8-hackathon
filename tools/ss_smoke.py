#!/usr/bin/env python3
"""Full-app smoke test over J-Link, no sound: reset, then every screen and path via host commands.

Needs the release build flashed and the RTT logger running (it reads the stats lines from
docs/gates/ss_rtt.log):

    tools/rtt_log.sh soundsense release &
    /opt/anaconda3/bin/python3 tools/ss_smoke.py

Prints PASS/FAIL per check and "SMOKE PASS" when all 16 pass with 0 audio blocks dropped and 0 IMU errors.
Taught sounds in the flash are kept (the teach is cancelled).
"""
import os
import re
import subprocess
import sys
import time

ROOT = "/Users/parthbansal/alif/e8-hackathon"
sys.path.insert(0, os.path.join(ROOT, "tools"))
import jlink  # noqa: E402

ELF = os.path.join(ROOT, "firmware/out/soundsense/E8-HP/release/soundsense.elf")
LOG = os.path.join(ROOT, "docs/gates/ss_rtt.log")
b = jlink.Board(ELF)
SCR = {0: "READY", 1: "HOME", 2: "ALERT", 3: "TIMELINE", 4: "STATS", 5: "TEACH", 6: "MENU"}
results = []


def get(name):
    for _ in range(8):
        try:
            return b.get(name)
        except RuntimeError:
            time.sleep(0.4)
    return None


def cmd(c, a=0):
    for _ in range(6):
        try:
            b.command(c, a)
            break
        except RuntimeError:
            time.sleep(0.4)
    for _ in range(20):
        time.sleep(0.25)
        try:
            if b.get("ss_cmd") == 0:
                return True
        except RuntimeError:
            pass
    return False


def check(what, c, a, want, settle=0.6):
    took = cmd(c, a)
    time.sleep(settle)
    s = get("_ZL3scr")
    ok = took and s == want
    results.append((what, ok))
    print(f"{'PASS' if ok else 'FAIL'}  {what}: screen {SCR.get(s, s)} (want {SCR[want]}){'' if took else ', command not taken'}",
          flush=True)


n0 = os.path.getsize(LOG)
subprocess.run([os.path.join(ROOT, "tools/jlink.py"), ELF, "reset"], check=True, capture_output=True)
time.sleep(6)
s = get("_ZL3scr")
results.append(("boot to home", s == 1))
print(f"{'PASS' if s == 1 else 'FAIL'}  boot: screen {SCR.get(s, s)} 6 s after reset", flush=True)
check("alarm detection -> alert", 13, 3, 2)
check("press dismisses", 8, 16, 1)
check("felt knock -> alert", 13, 0x200 | 0, 2)
took = cmd(8, 2)
time.sleep(0.6)
s = get("_ZL3scr")
results.append(("LEFT marks the alert wrong", took and s in (1, 2)))
print(f"{'PASS' if took and s in (1, 2) else 'FAIL'}  LEFT marks the alert wrong: screen {SCR.get(s, s)}", flush=True)
check("home", 3, 0, 1)
check("test clip (dog, info) -> banner on home", 15, 0, 1)
check("timeline", 6, 0, 3)
check("mark row 0 right", 16, 0x100, 3)
check("stats", 5, 0, 4)
check("home", 3, 0, 1)
check("DOWN opens the menu", 8, 1, 6)
check("LEFT leaves the menu", 8, 2, 1)
check("teach Keys", 1, 0, 5)
for _ in range(8):
    cmd(17, 0)
time.sleep(0.5)
check("RIGHT cancels teaching", 8, 4, 1)
check("10 s of forced full redraws", 14, 3, 1, settle=3.5)
time.sleep(7)                         # one stats line
with open(LOG, "rb") as f:
    f.seek(n0)
    log = f.read().decode(errors="replace")
st = re.findall(r"dropped (\d+), imu \d+ samples (\d+) errors", log)
drop = max((int(d) for d, _ in st), default=-1)
errs = max((int(e) for _, e in st), default=-1)
for pat in (r"\[S1\] ready[^\r\n]*", r"\[STORE\] loaded[^\r\n]*", r"\[TEACH\] T6[^\r\n]*", r"\[C3\] [^\r\n]*on screen[^\r\n]*"):
    m = re.findall(pat, log)
    print("   ", m[-1][:150] if m else f"(no {pat})")
ok_all = all(ok for _, ok in results) and drop == 0 and errs == 0
print(f"stats lines {len(st)}, dropped {drop}, imu errors {errs}; {sum(ok for _, ok in results)}/{len(results)} checks pass")
print("SMOKE", "PASS" if ok_all else "FAIL")
