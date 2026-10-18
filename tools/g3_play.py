#!/usr/bin/env python3
"""G3 live test with this Mac's speaker as the sound source (docs/HANDOFF-gates.md G3).

Needs the soundsense app on the board with its RTT being appended to docs/gates/ss_rtt.log (one
tools/jlink.py rtt call every 2 s). Each clip is played with afplay; the board logs one decision per
sound ("[SS] t=.. +18.2 dB best 'Dog barking' 0.71 (thr 0.52) ... -> Dog barking"), which is read
back from the log. Results are appended to docs/gates/g3_results.json.

    tools/g3_play.py builtin                 # ESC-50 fold 5 (never trained on): 8 clips per built-in
    tools/g3_play.py teach doorbell          # teach a generated sound over J-Link (8 examples), save it
    tools/g3_play.py trials doorbell --n 10  # play it again, expecting its label
    tools/g3_play.py reject                  # untaught: clapping, coughing (ESC-50 fold 5), another chime
    tools/g3_play.py noise --minutes 3       # spoken words and typing; counts the alerts
    tools/g3_play.py calibrate               # G3 row 7: re-measure the built-ins on the board from 10
                                             # fold 1-4 clips each; writes assets/ss_board_profiles.h
"""
import argparse
import csv
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import wave

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

T = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(T)
sys.path.insert(0, T)
import jlink   # noqa: E402

LOG = os.path.join(ROOT, "docs/gates/ss_rtt.log")
OUT = os.path.join(ROOT, "docs/gates/g3_results.json")
ESC = os.path.join(ROOT, "model/esc50/ESC-50-master")
SC = os.path.join(ROOT, "model/speech_commands")
FS = 48000
DECISION = re.compile(r"\[SS\] t=(\d+) \+?(-?[\d.]+) dB( clipped)? best '([^']+)' ([-\d.]+) \(thr ([\d.]+)\), "
                      r"2nd '([^']*)' ([-\d.]+), felt (\S+) \(([\d.]+) mg\) -> (maybe )?(.*)")
TEACH = re.compile(r"\[TEACH\] (.*)")

BUILTIN = {"knock": ("door_wood_knock", "Knock"), "dog": ("dog", "Dog barking"),
           "baby": ("crying_baby", "Baby crying"), "alarm": ("clock_alarm", "Alarm"),
           "glass": ("glass_breaking", "Glass breaking"), "siren": ("siren", "Siren"),
           "water": ("pouring_water", "Water running")}
PRESETS = ["keys", "doorbell", "clap", "microwave", "kettle", "phone", "washer", "name"]
LABELS = {"keys": "Keys", "doorbell": "Doorbell", "clap": "Clap", "microwave": "Microwave"}
NOT_ALERTS = {"unknown", "quiet"}


# ---------------------------------------------------------------- sounds

def esc_clips(category, fold="5"):
    rows = csv.DictReader(open(os.path.join(ESC, "meta/esc50.csv")))
    return [os.path.join(ESC, "audio", r["filename"]) for r in rows if r["category"] == category and r["fold"] == fold]


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64) / (32768.0 if x.dtype == np.int16 else 1.0)
    if x.ndim > 1:
        x = x.mean(1)
    return resample_poly(x, FS // 100, sr // 100) if sr != FS else x


def loudest(x, lead=0.25, length=1.5):
    """The loudest 1 s of a clip, from `lead` s before it, `length` s long, faded in and out."""
    f = int(0.02 * FS)
    e = np.convolve(x ** 2, np.ones(f), "valid")[::f]
    w = int(1.0 / 0.02)
    k = int(np.argmax(np.convolve(e, np.ones(w), "valid"))) if len(e) > w else 0
    s = max(0, k * f - int(lead * FS))
    y = x[s:s + int(length * FS)].copy()
    r = int(0.01 * FS)
    y[:r] *= np.linspace(0, 1, r)
    y[-r:] *= np.linspace(1, 0, r)
    return y


def tone(f, dur, decay, partials=((1, 1.0), (2.76, 0.35), (5.4, 0.12))):
    t = np.arange(int(dur * FS)) / FS
    y = sum(a * np.sin(2 * np.pi * f * m * t) * np.exp(-t / (decay / m ** 0.5)) for m, a in partials)
    a = int(0.003 * FS)
    y[:a] *= np.linspace(0, 1, a)
    return y


def place(parts, length):
    y = np.zeros(int(length * FS))
    for t0, p in parts:
        s = int(t0 * FS)
        y[s:s + len(p)] += p[:len(y) - s]
    return y


def generated(kind, rng):
    j = lambda: 1 + rng.normal(0, 0.004)          # small pitch jitter per example
    if kind == "doorbell":                         # ding-dong, E5 then C5
        y = place([(0.0, tone(659.3 * j(), 1.2, 0.5)), (0.45, tone(523.3 * j(), 1.4, 0.6))], 1.9)
    elif kind == "microwave":                      # three beeps at about 2 kHz
        t = np.arange(int(0.18 * FS)) / FS
        b = np.sin(2 * np.pi * 1950 * j() * t) + 0.3 * np.sin(2 * np.pi * 5850 * t)
        b[:96] *= np.linspace(0, 1, 96)
        b[-96:] *= np.linspace(1, 0, 96)
        y = place([(0.0, b), (0.3, b), (0.6, b)], 1.0)
    elif kind == "chime3":                         # a different chime: G5, B5, D6 rising
        y = place([(0.0, tone(784 * j(), 0.8, 0.25)), (0.22, tone(988 * j(), 0.8, 0.25)),
                   (0.44, tone(1175 * j(), 0.9, 0.3))], 1.4)
    else:
        raise ValueError(kind)
    return y


def norm(y, peak=0.9, gain_db=0.0):
    return y / (np.abs(y).max() + 1e-9) * peak * 10 ** (gain_db / 20)


def play(y):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "s.wav")
        with wave.open(p, "wb") as w:
            w.setnchannels(2), w.setsampwidth(2), w.setframerate(FS)
            s = (np.clip(y, -1, 1) * 32767).astype("<i2")
            w.writeframes(np.stack([s, s], 1).tobytes())
        subprocess.run(["afplay", p], check=True)


# ---------------------------------------------------------------- board

def log_size():
    return os.path.getsize(LOG) if os.path.exists(LOG) else 0


def new_lines(since):
    with open(LOG, "rb") as f:
        f.seek(since)
        return f.read().decode(errors="replace").splitlines()


def decisions(lines):
    out = []
    for ln in lines:
        m = DECISION.search(ln)
        if m:
            out.append(dict(t=int(m.group(1)), snr=float(m.group(2)), clipped=bool(m.group(3)), best=m.group(4),
                            score=float(m.group(5)), thr=float(m.group(6)), second=m.group(7),
                            score2=float(m.group(8)), felt=m.group(9), maybe=bool(m.group(11)), result=m.group(12).strip()))
    return out


def play_and_read(y, settle=3.2):
    """Play one sound and return the board's decisions logged for it."""
    n0 = log_size()
    play(y)
    time.sleep(settle)            # decision 0.8 s after the onset, plus the 2 s logger period
    return decisions(new_lines(n0))


def board():
    for build in ("release", "debug"):
        elf = os.path.join(ROOT, f"firmware/out/soundsense/E8-HP/{build}/soundsense.elf")
        if os.path.exists(elf) and os.environ.get("SS_BUILD", "release") == build:
            return jlink.Board(elf)
    return jlink.Board(os.path.join(ROOT, "firmware/out/soundsense/E8-HP/debug/soundsense.elf"))


def command(cmd, arg=0):
    """Write a host command and wait until the board takes it (ss_cmd back to 0). The RTT logger shares the
    probe, so a J-Link call can fail now and then: retry a write that didn't land, never one that did."""
    b = board()
    for _ in range(6):
        try:
            b.command(cmd, arg)
            break
        except RuntimeError:
            time.sleep(0.5)
    else:
        raise RuntimeError(f"couldn't write command {cmd}")
    for _ in range(20):
        time.sleep(0.25)
        try:
            if b.get("ss_cmd") == 0:
                return
        except RuntimeError:
            pass
    raise RuntimeError(f"board did not take command {cmd}")


def record(entry):
    runs = json.load(open(OUT)) if os.path.exists(OUT) else []
    entry["time"] = time.strftime("%H:%M:%S")
    json.dump(runs + [entry], open(OUT, "w"), indent=1)


def first_result(ds):
    """The decision for a sound: the first one that isn't 'quiet' (a quiet one is a stray onset)."""
    for d in ds:
        if d["result"] != "quiet":
            return d
    return ds[0] if ds else None


# ---------------------------------------------------------------- tests

def run_builtin(a):
    res, conf = {}, {}
    for key, (cat, label) in BUILTIN.items():
        if a.only and key not in a.only.split(","):
            continue
        clips = esc_clips(cat)[:a.n]
        hits = maybes = 0
        rows = []
        for c in clips:
            d = first_result(play_and_read(norm(loudest(load(c)), gain_db=a.gain_db)))
            got = d["result"] if d else "none"
            ok = got == label
            hits += ok
            maybes += ok and d["maybe"]
            conf[got] = conf.get(got, 0) + 1
            rows.append(dict(clip=os.path.basename(c), got=got, d=d))
            extra = f"  best {d['best']} {d['score']:.2f} +{d['snr']:.0f} dB" if d else ""
            print(f"  {key:6s} {os.path.basename(c):22s} -> {got}{' (maybe)' if d and d['maybe'] else ''}{extra}",
                  flush=True)
        res[key] = dict(n=len(clips), recognised=hits, maybe=maybes, rows=rows)
        print(f"{key}: {hits}/{len(clips)} recognised ({maybes} as maybe)", flush=True)
    tot = sum(r["recognised"] for r in res.values())
    n = sum(r["n"] for r in res.values())
    print(f"built-ins: {tot}/{n} = {100.0 * tot / max(n, 1):.0f}%")
    record(dict(test="builtin", gain_db=a.gain_db, results=res, total=tot, n=n))


def run_teach(a):
    kind = a.sound
    preset = PRESETS.index(kind)
    rng = np.random.default_rng(1)
    command(1, preset)
    time.sleep(0.5)
    shots = []
    for k in range(a.max_plays):
        n0 = log_size()
        play(norm(generated(kind, rng), gain_db=a.gain_db))
        time.sleep(3.2)
        lines = [ln for ln in new_lines(n0) if "[TEACH]" in ln]
        for ln in lines:
            print("  " + ln.strip(), flush=True)
        shots += lines
        if any("profile:" in ln for ln in lines):
            break
    command(2)
    time.sleep(2.5)
    saved = [ln for ln in new_lines(0)[-20:] if "[TEACH] saved" in ln]
    print(saved[-1].strip() if saved else "not saved?")
    record(dict(test="teach", sound=kind, lines=shots, saved=saved[-1] if saved else None))


def run_trials(a):
    kind = a.sound
    label = LABELS.get(kind, kind)
    rng = np.random.default_rng(7)
    hits, rows = 0, []
    for k in range(a.n):
        d = first_result(play_and_read(norm(generated(kind, rng), gain_db=a.gain_db)))
        got = d["result"] if d else "none"
        hits += got == label
        rows.append(dict(got=got, d=d))
        print(f"  {kind} trial {k + 1}: {got}" + (f"  best {d['best']} {d['score']:.2f} (thr {d['thr']:.2f}), "
                                                   f"2nd {d['second']} {d['score2']:.2f}" if d else ""), flush=True)
    print(f"{kind}: {hits}/{a.n} accepted as {label}")
    record(dict(test="trials", sound=kind, n=a.n, accepted=hits, rows=rows))


def run_reject(a):
    rng = np.random.default_rng(3)
    sets = {"clapping": [norm(loudest(load(c))) for c in esc_clips("clapping")[:a.n]],
            "coughing": [norm(loudest(load(c))) for c in esc_clips("coughing")[:a.n]],
            "chime3": [norm(generated("chime3", rng)) for _ in range(a.n)]}
    res = {}
    for name, clips in sets.items():
        rejected, rows = 0, []
        for y in clips:
            d = first_result(play_and_read(y))
            got = d["result"] if d else "none"
            ok = got in NOT_ALERTS or got == "none"
            rejected += ok
            rows.append(dict(got=got, d=d))
            print(f"  {name}: {got}" + (f"  best {d['best']} {d['score']:.2f} (thr {d['thr']:.2f})" if d else ""), flush=True)
        res[name] = dict(n=len(clips), rejected=rejected, rows=rows)
        print(f"{name}: {rejected}/{len(clips)} rejected", flush=True)
    tot = sum(r["rejected"] for r in res.values())
    n = sum(r["n"] for r in res.values())
    print(f"untaught: {tot}/{n} = {100.0 * tot / max(n, 1):.0f}% rejected")
    record(dict(test="reject", results=res, total=tot, n=n))


def run_noise(a):
    rng = np.random.default_rng(5)
    words = [d for d in os.listdir(SC) if os.path.isdir(os.path.join(SC, d)) and not d.startswith("_")]
    typing = [loudest(load(c), lead=0.0, length=4.0) for c in esc_clips("keyboard_typing", fold="1")]
    total = a.minutes * 60.0
    y = np.zeros(int(total * FS))
    t = 0.5
    while t < total - 5:
        if rng.random() < 0.2:       # a burst of typing
            p = typing[rng.integers(len(typing))] * 0.5
        else:                        # a short phrase: 2-5 spoken words
            p = np.concatenate([np.concatenate([load(os.path.join(SC, w, rng.choice(os.listdir(os.path.join(SC, w))))),
                                                np.zeros(int(rng.uniform(0.05, 0.3) * FS))])
                                for w in rng.choice(words, rng.integers(2, 6))])
            p = norm(p, 0.7)
        s = int(t * FS)
        y[s:s + len(p)] += p[:len(y) - s]
        t += len(p) / FS + rng.uniform(0.5, 2.5)
    n0 = log_size()
    print(f"playing {a.minutes} min of speech and typing", flush=True)
    play(y)
    time.sleep(3.2)
    ds = decisions(new_lines(n0))
    alerts = [d for d in ds if d["result"] not in NOT_ALERTS]
    for d in alerts:
        print(f"  alert at t={d['t']}: {'maybe ' if d['maybe'] else ''}{d['result']} ({d['best']} {d['score']:.2f})")
    print(f"{len(ds)} sounds classified, {len(alerts)} alerts in {a.minutes} min "
          f"({len(alerts) * 10.0 / a.minutes:.1f} per 10 min)")
    record(dict(test="noise", minutes=a.minutes, sounds=len(ds), alerts=len(alerts),
                alert_rows=alerts, counts={r: sum(d["result"] == r for d in ds) for r in {d["result"] for d in ds}}))


CAL = re.compile(r"\[CAL\] proto (\S+) ([\d.]+) (.*)")
BOARD_H = os.path.join(ROOT, "firmware/soundsense/assets/ss_board_profiles.h")


def run_calibrate(a):
    names = list(BUILTIN)          # same order as ss_builtin_names
    rng = np.random.default_rng(11)
    got = {}
    for idx, key in enumerate(names):
        if a.only and key not in a.only.split(","):
            continue
        cat = BUILTIN[key][0]
        clips = [c for f in "1234" for c in esc_clips(cat, fold=f)]
        clips = [clips[i] for i in rng.permutation(len(clips))[:a.n]]
        n0 = log_size()
        command(9, idx)
        for c in clips:
            play(norm(loudest(load(c), length=1.1), gain_db=a.gain_db))   # short: one onset per clip
            time.sleep(1.8)
        time.sleep(1.0)
        command(10)
        time.sleep(3.0)
        lines = new_lines(n0)
        for ln in lines:
            if "[CAL]" in ln and "proto" not in ln:
                print("  " + ln.strip(), flush=True)
        for ln in lines:
            m = CAL.search(ln)
            if m and m.group(1) == key:
                got[key] = (float(m.group(2)), [float(v) for v in m.group(3).split()])
    old = {}
    if os.path.exists(BOARD_H):            # keep earlier measurements of the others
        s = open(BOARD_H).read()
        for m in re.finditer(r'/\* (\S+) thr ([\d.]+) \*/ \{([^}]*)\}', s):
            old[m.group(1)] = (float(m.group(2)), [float(v.rstrip("f")) for v in m.group(3).split(",") if v.strip()])
    old.update(got)
    keys = [k for k in names if k in old]
    with open(BOARD_H, "w") as f:
        f.write("/* Generated by tools/g3_play.py calibrate: built-in profiles re-measured on the board (G3 row 7).\n"
                " * Each is the unit-norm mean of the board's embeddings of ESC-50 fold 1-4 clips played from the\n"
                " * Mac speaker; the threshold uses the same leave-one-out rule as taught sounds. */\n#pragma once\n")
        f.write(f"#define SS_NUM_BOARD {len(keys)}\n")
        f.write("static const char *const ss_board_names[SS_NUM_BOARD] = {" + ", ".join(f'"{k}"' for k in keys) + "};\n")
        f.write("static const float ss_board_thr[SS_NUM_BOARD] = {" + ", ".join(f"{old[k][0]:.4f}f" for k in keys) + "};\n")
        f.write("static const float ss_board_proto[SS_NUM_BOARD][SS_EMB] = {\n")
        for k in keys:
            f.write(f"    /* {k} thr {old[k][0]:.4f} */ {{" + ", ".join(f"{v:.5f}f" for v in old[k][1]) + "},\n")
        f.write("};\n")
    print(f"{BOARD_H}: {', '.join(keys)}")
    record(dict(test="calibrate", measured={k: dict(thr=v[0]) for k, v in got.items()}))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("test", choices=["builtin", "teach", "trials", "reject", "noise", "calibrate"])
    ap.add_argument("sound", nargs="?")
    ap.add_argument("--n", type=int, default=8)
    ap.add_argument("--only", help="built-ins to run, comma separated")
    ap.add_argument("--gain-db", type=float, default=0.0)
    ap.add_argument("--max-plays", type=int, default=12)
    ap.add_argument("--minutes", type=float, default=3)
    a = ap.parse_args()
    {"builtin": run_builtin, "teach": run_teach, "trials": run_trials, "reject": run_reject, "noise": run_noise,
     "calibrate": run_calibrate}[a.test](a)


if __name__ == "__main__":
    main()
