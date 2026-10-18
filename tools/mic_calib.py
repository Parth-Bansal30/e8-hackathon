#!/usr/bin/env python3
"""Mic-pair calibration with this Mac's speaker as the sound source (C4, H12).

Needs the recorder firmware on the board (tools/build_flash.sh recorder). For one speaker position:
starts a take over J-Link, plays --n synthetic claps (1 ms noise bursts with a 15 ms decay) with
afplay, stops the take, pulls it, and reports the GCC-PHAT lag of each clap (samples, + = mic 5
later) and the level difference mic 5 - mic 4. Results are appended to --log, one entry per run.

    tools/mic_calib.py right            # speaker in line with the pair, past the right mic
    tools/mic_calib.py --summary        # offset and maximum lag from the 'left' and 'right' runs

The pair's channel offset is (left + right) / 2 and its maximum lag (left - right) / 2, using the
median lag of each end.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
import wave

import numpy as np

T = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(T)
sys.path.insert(0, T)
import jlink                                         # noqa: E402
from mic_geometry import find_claps, gcc_phat        # noqa: E402

SR = 16000
ELF = os.path.join(ROOT, "firmware/out/recorder/E8-HP/debug/recorder.elf")
RAM_BASE = 0xA0000000


def clap_wav(path, n, gap_s, peak, speaker, fs=48000, seed=0):
    rng = np.random.default_rng(seed)
    t = np.arange(int(0.06 * fs)) / fs
    env = np.minimum(t / 0.001, 1.0) * np.exp(-np.maximum(t - 0.001, 0) / 0.015)
    lead = int(0.3 * fs)
    x = np.zeros(lead + int(n * gap_s * fs))
    for k in range(n):
        b = rng.standard_normal(len(t)) * env
        s = lead + int(k * gap_s * fs)
        x[s:s + len(b)] += b / np.abs(b).max() * peak
    ch = {"left": (1, 0), "right": (0, 1), "both": (1, 1)}[speaker]
    st = np.stack([x * ch[0], x * ch[1]], 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(2), w.setsampwidth(2), w.setframerate(fs)
        w.writeframes((st * 32767).astype("<i2").tobytes())


def record(b, wav, settle_s=0.4):
    if b.get("rec_state"):
        b.set("rec_cmd", 2)
        time.sleep(0.3)
    b.set("rec_cmd", 1)
    time.sleep(settle_s)
    subprocess.run(["afplay", wav], check=True)
    time.sleep(settle_s)
    b.set("rec_cmd", 2)
    time.sleep(0.3)
    k = b.get("rec_takes") - 1
    off = b.read32(b.addr("rec_take_off") + 4 * k)[0]
    n = b.read32(b.addr("rec_take_len") + 4 * k)[0]
    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, "take.raw")
        b.dump(RAM_BASE + off, n, raw)
        x = np.fromfile(raw, dtype="<i2").reshape(-1, 2)
    return x


def analyse(x, win_ms=30.0):
    xf = x.astype(np.float64) / 32768.0
    claps, floor_db = find_claps(xf)
    w = int(win_ms * SR / 1000)
    rows = []
    for s in claps:
        seg = xf[max(0, s - 80):s - 80 + w]
        l4, l5 = (10 * np.log10(np.mean(seg[:, c] ** 2) + 1e-12) for c in (0, 1))
        rows.append(dict(t=s / SR, lag=float(gcc_phat(seg[:, 0], seg[:, 1])), diff_db=float(l5 - l4),
                         peak_db=float(20 * np.log10(np.abs(seg).max() + 1e-12)),
                         clipped=int((np.abs(seg) > 0.99).sum())))
    return rows, float(floor_db)


def summary(log):
    runs = json.load(open(log)) if os.path.exists(log) else []
    last = {r["label"]: r for r in runs}
    for lab, r in last.items():
        print(f"{lab:10s} n={len(r['claps'])} lag median {r['lag_median']:+.2f} samples "
              f"(spread {r['lag_spread']:.2f}), mic5-mic4 {r['diff_db_median']:+.1f} dB, speaker {r['speaker']}")
    if "left" in last and "right" in last:
        L, R = last["left"]["lag_median"], last["right"]["lag_median"]
        print(f"channel offset {(L + R) / 2:+.2f} samples, maximum lag {(L - R) / 2:.2f} samples "
              f"(= {(L - R) / 2 * 343000 / SR:.0f} mm of path difference)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("label", nargs="?", help="speaker position, e.g. left, right, front, back")
    ap.add_argument("--n", type=int, default=5)
    ap.add_argument("--gap", type=float, default=1.0, help="s between claps")
    ap.add_argument("--peak", type=float, default=0.9, help="clap peak in the played file, full scale = 1")
    ap.add_argument("--speaker", default="both", choices=["left", "right", "both"],
                    help="which of the Mac's speakers plays; one speaker alone is about 6 dB quieter")
    ap.add_argument("--wav-out", help="also save the take as a stereo wav")
    ap.add_argument("--log", default=os.path.join(ROOT, "docs/gates/c4_mic_calib.json"))
    ap.add_argument("--summary", action="store_true")
    a = ap.parse_args()
    if a.summary or not a.label:
        return summary(a.log)

    b = jlink.Board(ELF)
    with tempfile.TemporaryDirectory() as d:
        wav = os.path.join(d, "claps.wav")
        clap_wav(wav, a.n, a.gap, a.peak, a.speaker)
        x = record(b, wav)
    if a.wav_out:
        with wave.open(a.wav_out, "wb") as w:
            w.setnchannels(2), w.setsampwidth(2), w.setframerate(SR)
            w.writeframes(x.astype("<i2").tobytes())
    rows, floor_db = analyse(x)
    for i, r in enumerate(rows):
        print(f"  clap {i + 1} t={r['t']:5.2f} s  lag {r['lag']:+5.2f}  mic5-mic4 {r['diff_db']:+5.1f} dB  "
              f"peak {r['peak_db']:5.1f} dBFS{'  CLIPPED' if r['clipped'] else ''}")
    if not rows:
        print(f"no claps found ({len(x) / SR:.1f} s recorded, background {floor_db:.1f} dB): speaker too quiet?")
        return 1
    lags = np.array([r["lag"] for r in rows])
    med = float(np.median(lags))
    entry = dict(label=a.label, time=time.strftime("%H:%M:%S"), speaker=a.speaker, peak=a.peak, claps=rows,
                 lag_median=med, lag_spread=float(np.median(np.abs(lags - med))),
                 diff_db_median=float(np.median([r["diff_db"] for r in rows])), background_db=floor_db)
    print(f"{a.label}: {len(rows)} of {a.n} claps found, lag median {med:+.2f} samples, "
          f"mic5-mic4 {entry['diff_db_median']:+.1f} dB, background {floor_db:.1f} dB")
    runs = json.load(open(a.log)) if os.path.exists(a.log) else []
    json.dump(runs + [entry], open(a.log, "w"), indent=1)
    summary(a.log)


if __name__ == "__main__":
    sys.exit(main())
