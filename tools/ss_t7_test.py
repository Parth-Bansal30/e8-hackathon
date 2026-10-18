#!/usr/bin/env python3
"""T7 and H5 on the board, no sound: the Sounds page (built-ins on/off, taught urgency) and "still sounding".

Needs the release build flashed, one taught sound in slot 0 (profile 9), and the RTT logger running.
Leaves the settings as it found them (all built-ins on, the taught sound back to its urgency).
    /opt/anaconda3/bin/python3 tools/ss_t7_test.py
    /opt/anaconda3/bin/python3 tools/ss_t7_test.py --set 0 1   # all built-ins on, taught sound Important
"""
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import jlink  # noqa: E402

ELF = os.path.join(ROOT, "firmware/out/soundsense/E8-HP/release/soundsense.elf")
LOG = os.path.join(ROOT, "docs/gates/ss_rtt.log")
b = jlink.Board(ELF)
DOWN, LEFT, RIGHT, UP, PRESS = 1, 2, 4, 8, 16
results = []


def get(name, off=0):
    for _ in range(8):
        try:
            return b.read32(b.addr(name) + off)[0]
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
                time.sleep(0.35)   # past the 250 ms key lock-out
                return True
        except RuntimeError:
            pass
    return False


def keys(*ks):
    for k in ks:
        cmd(8, k)


def check(what, ok, detail=""):
    results.append(ok)
    print(f"{'PASS' if ok else 'FAIL'}  {what}{': ' + detail if detail else ''}", flush=True)


def log_since(n0):
    time.sleep(2.5)                          # the logger's period
    with open(LOG, "rb") as f:
        f.seek(n0)
        return f.read().decode(errors="replace")


MENU = b.addr("_ZL4menu")                    # kind[16] (1-byte enums), arg[16], then n, sel, confirm, page
PROF = b.addr("_ZL4prof")                    # 24 bytes each; urg (1 byte) at 20


def menu_sel_page():
    n, sel, _, page = b.read32(MENU + 80, 4)
    return sel, page


def urg_of(p):
    return b.read32(PROF + 24 * p + 20)[0] & 0xFF


def goto(row):
    """Select a menu row by reading where the selection is (a sound in the room can eat a press)."""
    for _ in range(3):
        sel, _ = menu_sel_page()
        if sel == row:
            return True
        keys(*([DOWN] * (row - sel) if row > sel else [UP] * (sel - row)))
    return menu_sel_page()[0] == row


def open_sounds():
    """Home, menu, Sounds row (4), press: on the Sounds page with row 0 selected."""
    for _ in range(3):
        cmd(3)
        keys(DOWN)
        if get("_ZL3scr") != 6:
            continue
        if goto(4):
            keys(PRESS)
            if menu_sel_page() == (0, 1):
                return True
    return False


def sounds_press(row):
    return open_sounds() and goto(row) and (keys(PRESS) or True)


def mute():
    return get("_ZL12builtin_mute")


def set_state(m, u):
    for i in range(7):                       # Sounds rows 0-6 are the built-ins that alert, in order
        if (mute() ^ m) >> i & 1:
            sounds_press(i)
    for _ in range(3):
        if urg_of(9) == u:
            break
        sounds_press(7)
    cmd(3)
    return mute() == m and urg_of(9) == u


if len(sys.argv) == 4 and sys.argv[1] == "--set":   # put the settings to builtin_mute MUTE, taught urgency URG
    ok = set_state(int(sys.argv[2], 0), int(sys.argv[3]))
    print(f"builtin_mute 0x{mute():x}, taught urgency {urg_of(9)}:", "set" if ok else "FAILED")
    sys.exit(0 if ok else 1)

cmd(3)
mute0, urg0 = mute(), urg_of(9)
print(f"start: builtin_mute 0x{mute0:x}, taught urgency {urg0}")

# 1. Dog barking off (Sounds row 1): the boot test clip (a dog) no longer alerts
want = mute0 | 2
if mute0 & 2:
    want = mute0
else:
    sounds_press(1)
check("Dog barking turned off", mute() == want, f"builtin_mute 0x{mute():x}")
keys(LEFT)
check("LEFT on the Sounds page goes back to Settings", get("_ZL3scr") == 6 and menu_sel_page() == (4, 0))
keys(LEFT)
check("LEFT again goes home", get("_ZL3scr") == 1)
n1 = os.path.getsize(LOG)
cmd(15)
ln = re.findall(r"\[SS\] t=\d+ \+20\.0 dB best 'Dog barking'[^\r\n]*", log_since(n1))
check("the dog clip doesn't alert", bool(ln) and ln[-1].endswith("-> unknown") and get("_ZL9banner_on") & 0xFF == 0,
      ln[-1][-60:] if ln else "no decision line")

# 2. Kept in the flash across a reset
n2 = os.path.getsize(LOG)
subprocess.run([os.path.join(ROOT, "tools/jlink.py"), ELF, "reset"], check=True, capture_output=True)
time.sleep(6)
st = re.findall(r"\[STORE\] loaded[^\r\n]*", log_since(n2))
check("still off after a reset", mute() == want, st[-1] if st else "")

# 3. Taught sound (Sounds row 7) one step up the urgency: an injected detection gets it
u1 = (urg0 + 1) % 3
n3 = os.path.getsize(LOG)
sounds_press(7)
u = re.findall(r"\[SS\] '[^']+' urgency: \w+", log_since(n3))
check("taught sound's urgency changed", urg_of(9) == u1, u[-1] if u else "no log line")
cmd(3)
cmd(13, 9)
time.sleep(0.6)
urg = get("_ZL8alert_ev", 4) >> 16 & 0xFF   # ui_event: 1-byte enums, urgency at byte 6
check("its alert has that urgency", urg == u1, f"screen {get('_ZL3scr')}, urgency {urg}")

# 4. Put everything back as it was
check("settings restored", set_state(mute0, urg0), f"builtin_mute 0x{mute():x}, urgency {urg_of(9)}")

# 5. H5: an alarm heard twice reads "still sounding" until 15 s after the second
cmd(13, 3)
time.sleep(2)
cmd(13, 3)
time.sleep(1)
s1 = get("_ZL11still_shown") & 0xFF
cnt = get("_ZL8alert_ev", 8) >> 16   # count at byte 10
check("repeated alarm: still sounding", s1 == 1, f"still_shown {s1}, count {cnt}")
time.sleep(16)
s2 = get("_ZL11still_shown") & 0xFF
check("15 s after the last repeat: not any more", s2 == 0, f"still_shown {s2}, screen {get('_ZL3scr')}")
cmd(3)
print(f"{sum(results)}/{len(results)} checks pass")
print("T7 TEST", "PASS" if all(results) else "FAIL")
