#!/usr/bin/env python3
"""Play a demo sound from this Mac's speaker for Sound Sense (docs/DEMO.md).

The built-ins use the ESC-50 clips the board recognised best in G3 (never trained on). Doorbell and
microwave are generated, for teaching: play one 8 times with --times 8 while the board's teach
screen is recording.

    tools/demo_sound.py alarm
    tools/demo_sound.py doorbell --times 8      # teach it: UP, pick Doorbell, press, then run this
    tools/demo_sound.py list
"""
import argparse
import os
import sys
import time

import numpy as np

T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, T)
from g3_play import ESC, generated, load, loudest, norm, play   # noqa: E402

CLIPS = {   # name: (ESC-50 file, what the board showed in G3, match score)
    "alarm": ("5-219242-A-37.wav", "Alarm", 0.91),
    "glass": ("5-233607-A-39.wav", "Glass breaking", 0.76),
    "baby": ("5-198411-E-20.wav", "Baby crying", 0.76),
    "water": ("5-212734-A-17.wav", "Water running", 0.86),
    "siren": ("5-160551-A-42.wav", "Siren", 0.69),
    "knock": ("5-251426-A-30.wav", "Knock", 0.67),
    "dog": ("5-231762-A-0.wav", "Dog barking", 0.66),
}
GENERATED = ["doorbell", "microwave", "chime3"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sound", choices=list(CLIPS) + GENERATED + ["list"])
    ap.add_argument("--times", type=int, default=1)
    ap.add_argument("--gap", type=float, default=3.0, help="s between repeats")
    ap.add_argument("--gain-db", type=float, default=0.0)
    a = ap.parse_args()
    if a.sound == "list":
        for k, (f, lab, s) in CLIPS.items():
            print(f"{k:10s} -> {lab} (G3 score {s:.2f}, {f})")
        print("doorbell, microwave: generated, for teaching; chime3: an untaught chime")
        return
    rng = np.random.default_rng()
    for k in range(a.times):
        if a.sound in CLIPS:
            y = norm(loudest(load(os.path.join(ESC, "audio", CLIPS[a.sound][0]))), gain_db=a.gain_db)
        else:
            y = norm(generated(a.sound, rng), gain_db=a.gain_db)
        print(f"{a.sound} {k + 1}/{a.times}", flush=True)
        play(y)
        if k + 1 < a.times:
            time.sleep(a.gap)


if __name__ == "__main__":
    main()
