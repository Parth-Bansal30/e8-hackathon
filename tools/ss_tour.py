#!/usr/bin/env python3
"""Screen tour on the board, no sound: resets the app, then shows every screen with host commands,
a few seconds each, for watching the display (demo rehearsal, judging, photos).

Nothing is saved: the teach is cancelled, night mode and the microphones are put back, and the
injected sounds only live in RAM until the next reset. Needs the release build flashed.
Don't touch the joystick while it runs: LEFT on an injected alert keeps the sound's own reference as a
"wrong" example (saved in the flash for a taught sound), which then blocks about half of its real matches.
    /opt/anaconda3/bin/python3 tools/ss_tour.py [--pause 6]
"""
import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import jlink  # noqa: E402

ELF = os.path.join(ROOT, "firmware/out/soundsense/E8-HP/release/soundsense.elf")
b = jlink.Board(ELF)
DOWN, LEFT, RIGHT, UP, PRESS = 1, 2, 4, 8, 16
SCR = {0: "Ready", 1: "Home", 2: "Alert", 3: "Timeline", 4: "Stats", 5: "Teach", 6: "Menu"}
MENU = b.addr("_ZL4menu")                   # kind[16] bytes +0, arg[16] +16, sel +84, page +92
SNOOZE = b.addr("_ZL12snooze_until")
MI_BUILTIN, ALARM = 9, 3
ap = argparse.ArgumentParser()
ap.add_argument("--pause", type=float, default=6.0)
P = ap.parse_args().pause
step = 0


def rd(addr, n=1):
    for _ in range(10):
        try:
            return b.read32(addr, n)
        except RuntimeError:
            time.sleep(0.4)
    raise RuntimeError(f"can't read 0x{addr:08X}")


def cmd(c, a=0):
    for _ in range(8):
        try:
            b.command(c, a)
            break
        except RuntimeError:
            time.sleep(0.4)
    for _ in range(20):
        time.sleep(0.25)
        try:
            if b.get("ss_cmd") == 0:
                time.sleep(0.35)
                return
        except RuntimeError:
            pass


def keys(*ks):
    for k in ks:
        cmd(8, k)


def show(what, pause=None):
    global step
    step += 1
    s = rd(b.addr("_ZL3scr"))[0]
    print(f"{step:2d}. [{SCR.get(s, s):8s}] {what}", flush=True)
    time.sleep(P if pause is None else pause)


def goto(row):
    for _ in range(3):
        sel = rd(MENU + 84)[0]
        if sel == row:
            return
        keys(*([DOWN] * (row - sel) if row > sel else [UP] * (sel - row)))


print("Restarting the app...", flush=True)
subprocess.run([os.path.join(ROOT, "tools/jlink.py"), ELF, "reset"], check=True, capture_output=True)
time.sleep(0.3)
show("Ready: the boot self-test (mics, NPU, saved sounds, vibration sensor, lights)", 2.5)
show("Home: live spectrogram, room level against its floor, best 3 matches, recent sounds", P + 2)

cmd(13, 3)
show("Alarm alert (important): full screen, LEDs, '% sure'")
cmd(13, 3)
show("The alarm again: '2 times, still sounding'")
keys(DOWN)
show("DOWN snoozed it: 'Alarm snoozed - no alerts for 10 min; still in the timeline'", 4)
cmd(3)
cmd(13, 4)
show("Glass breaking (urgent): strobes once a second, waits for a press", P + 2)
keys(PRESS)
cmd(13, 0x100 | 2)
show("A weak match: 'Maybe: Baby crying?' (RIGHT it was, LEFT it wasn't)")
keys(PRESS)
cmd(13, 0x200 | 0)
show("Knock felt through the table: 'Felt here'")
keys(PRESS)
cmd(13, 0x400 | 0)
show("Knock heard but not felt: a banner, 'Heard only (TV?)'")
cmd(15)
show("Dog barking (info): a banner on the home screen, no full-screen alert")
cmd(13, 9)
show("Your taught Doorbell: an alert like the built-ins")
keys(PRESS)

cmd(6)
show("Timeline: today's sounds, repeats merged, felt marked")
keys(DOWN, DOWN)
show("Timeline scrolled (the top row is the selected one: LEFT wrong, RIGHT right)", 4)
cmd(5)
show("Stats: NPU time per run, CPU active, NPU busy, model size, saves", P + 2)

cmd(3)
keys(DOWN)
show("Settings menu")
goto(4)
keys(PRESS)
show("Settings > Sounds: built-ins on/off, taught sound's urgency (RIGHT renames)")
kinds = rd(MENU, 4)                          # kind[16] as bytes, then arg[16] (short enums)
args = rd(MENU + 16, 16)
row = next((r for r in range(16) if (kinds[r // 4] >> (8 * (r % 4)) & 0xFF) == MI_BUILTIN and args[r] == ALARM), -1)
if row >= 0:
    goto(row)
    show("Alarm reads 'Snoozed' (from the DOWN on its alert)", 4)
    if rd(SNOOZE + 4 * ALARM)[0] and rd(MENU + 84)[0] == row:
        keys(PRESS)                          # on a snoozed row a press only ends the snooze
        show("Press ended the snooze: Alarm is On again", 4)
keys(LEFT)
goto(5)
keys(PRESS)
show("Calibrate room: measuring 10 s (the row shows progress)", 11)
show("Calibrate room: result, and how loud a sound must be", 4)
goto(1)
keys(PRESS)
show("Microphones off", 3)
keys(LEFT)
show("Home with the mic off ('Mic off' at the top)", 4)
keys(DOWN)
goto(1)
keys(PRESS)
goto(0)
keys(PRESS)
keys(LEFT)
show("Night mode on: home goes dark after 15 s without a press. Owner check: does the backlight go off?", 18)
print("    dark now:", bool(rd(b.addr("_ZL4dark"))[0] & 0xFF), flush=True)
keys(PRESS)                                  # in the dark a press only lights the screen
time.sleep(1)
keys(DOWN)
goto(0)
keys(PRESS)                                  # night mode off
keys(LEFT)
show("Night mode off again, mic on", 3)

keys(UP)
show("Teach: the name screen (UP/DOWN picks the name, press starts)")
cmd(3)
cmd(1, 0)
show("Teach 'Keys': 8 circles, each turns green with its dB over the room", 4)
for k in range(7):
    cmd(17, 0)
    time.sleep(0.3)
show("7 examples in", 3)
cmd(17, 1)
time.sleep(1.5)
show("8th example is the room: flagged orange 'unlike', and 'Sounds like Dog barking' (the test clip is a dog)", P + 3)
keys(UP)
cmd(17, 0)
time.sleep(1.5)
show("UP redid the flagged example", 5)
keys(RIGHT)
show("RIGHT cancelled the teach (nothing saved): home", 4)
print("Tour done.", flush=True)
