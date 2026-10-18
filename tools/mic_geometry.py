#!/usr/bin/env python3
"""Mic geometry from a recorder take of claps (docs/HANDOFF-gates.md C4).

Finds each clap (a sharp onset well above the noise floor), then for each one:
  - the GCC-PHAT lag between mic 4 and mic 5 (samples, with parabolic sub-sample refinement;
    positive = mic 5 hears it later),
  - the level difference mic 5 minus mic 4 over the clap (dB).
Claps are listed in order; --groups "left:5,front:5,right:5" labels them and summarises per group.

    tools/mic_geometry.py claps.wav --groups left:5,front:5,right:5
"""
import argparse
import json
import wave

import numpy as np

SR = 16000


def read_stereo(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 2 and w.getframerate() == SR
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2)
    return x.astype(np.float64) / 32768.0


def find_claps(x, min_gap_s=0.4, ratio_db=20.0):
    """Onsets where the 5 ms energy jumps ratio_db above the median background."""
    m = x[:, 0] ** 2 + x[:, 1] ** 2
    hop = 80
    e = np.array([m[i:i + hop].mean() for i in range(0, len(m) - hop, hop)])
    floor = np.median(e)
    above = np.flatnonzero(e > floor * 10 ** (ratio_db / 10))
    out, last = [], -1e9
    for i in above:
        if i * hop - last > min_gap_s * SR:
            out.append(i * hop)
            last = i * hop
    return out, 10 * np.log10(floor + 1e-12)


def gcc_phat(a, b, max_lag=12, interp=16):
    n = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    A, B = np.fft.rfft(a, n), np.fft.rfft(b, n)
    R = A * np.conj(B)
    R /= np.abs(R) + 1e-12
    cc = np.fft.irfft(R, n * interp)
    cc = np.concatenate([cc[-max_lag * interp:], cc[:max_lag * interp + 1]])
    k = int(np.argmax(cc))
    if 0 < k < len(cc) - 1:   # parabolic refinement
        y0, y1, y2 = cc[k - 1], cc[k], cc[k + 1]
        k = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    # lag of b relative to a: positive when b (mic 5) is later
    return -(k - max_lag * interp) / interp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--groups", default="")
    ap.add_argument("--win-ms", type=float, default=30.0)
    ap.add_argument("--json")
    a = ap.parse_args()
    x = read_stereo(a.wav)
    claps, floor_db = find_claps(x)
    print(f"{a.wav}: {len(x) / SR:.1f} s, background {floor_db:.1f} dB (both channels), {len(claps)} claps")
    labels = []
    for g in filter(None, a.groups.split(",")):
        name, n = g.split(":")
        labels += [name] * int(n)
    rows = []
    w = int(a.win_ms * SR / 1000)
    for i, s in enumerate(claps):
        seg = x[max(0, s - 80):s - 80 + w]
        lag = gcc_phat(seg[:, 0], seg[:, 1])
        l4 = 10 * np.log10(np.mean(seg[:, 0] ** 2) + 1e-12)
        l5 = 10 * np.log10(np.mean(seg[:, 1] ** 2) + 1e-12)
        pk = 20 * np.log10(np.abs(seg).max() + 1e-12)
        lab = labels[i] if i < len(labels) else "?"
        rows.append(dict(t=s / SR, label=lab, lag=lag, diff_db=l5 - l4, peak_db=pk))
        print(f"  clap {i + 1:2d} t={s / SR:6.2f} s  {lab:6s} lag {lag:+5.2f} samples  mic5-mic4 {l5 - l4:+5.1f} dB  "
              f"peak {pk:5.1f} dBFS")
    summary = {}
    for lab in dict.fromkeys(r["label"] for r in rows):
        g = [r for r in rows if r["label"] == lab]
        lags = np.array([r["lag"] for r in g])
        diffs = np.array([r["diff_db"] for r in g])
        summary[lab] = dict(n=len(g), lag_median=float(np.median(lags)), lag_min=float(lags.min()),
                            lag_max=float(lags.max()), diff_db_median=float(np.median(diffs)))
        print(f"{lab:6s}: n={len(g)} lag median {np.median(lags):+.2f} (range {lags.min():+.2f}..{lags.max():+.2f}) "
              f"samples; mic5-mic4 {np.median(diffs):+.1f} dB")
    if a.json:
        json.dump(dict(claps=rows, summary=summary, background_db=floor_db), open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
