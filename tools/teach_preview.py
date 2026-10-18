#!/usr/bin/env python3
"""Preview teaching a sound from a board recording, before doing it live (G3).

Finds the sound's onsets in a recorder take (tools/pull_audio.sh), cuts 1 s windows of mic 4 with
the onset 0.2 s in (as the firmware's teach does), embeds them with the float encoder, builds a
profile from the first --n examples (ss_common.make_profile, the same rule as ss_match.c) and
scores:
  - every window of the taught sound against the profile,
  - onsets in --negatives takes (other sounds) and the quietest seconds of them (background),
  - all of these against the built-in profiles in firmware/soundsense/assets, to show which
    built-in a sound would trigger if it were not taught.

Run with the et-venv python:
    tools/teach_preview.py docs/gates/audio/c4_take2.wav --negatives docs/gates/audio/c4_take1.wav
"""
import argparse
import json
import os
import re
import sys

import numpy as np
import torch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "model"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from kws_common import Features                     # noqa: E402
from mic_geometry import find_claps, read_stereo    # noqa: E402
from ss_common import load_encoder, make_profile, normalize   # noqa: E402

SR = 16000
PRE = 0.2   # onset position inside the 1 s window, s


def onsets(x, min_gap_s):
    o, _ = find_claps(x, min_gap_s=min_gap_s)
    return [s for s in o if s >= int(PRE * SR) + 200 and s - int(PRE * SR) + SR <= len(x)]


def windows(x, ons):
    return [x[s - int(PRE * SR):s - int(PRE * SR) + SR, 0] for s in ons]   # mic 4, as the firmware


def quiet_windows(x, n=8):
    e = np.array([np.mean(x[i:i + SR, 0] ** 2) for i in range(0, len(x) - SR, SR // 4)])
    return [x[i * SR // 4:i * SR // 4 + SR, 0] for i in np.argsort(e)[:n]]


def builtins(path):
    h = open(path).read()
    names = re.search(r"ss_builtin_names\[[^]]*\] = \{([^}]*)\}", h).group(1).replace('"', "").replace(" ", "").split(",")
    thrs = [float(v.rstrip("f")) for v in re.search(r"ss_builtin_thr\[[^]]*\] = \{([^}]*)\}", h).group(1).split(",")]
    body = h[h.index("ss_builtin_proto"):]
    vals = [float(v) for v in re.findall(r"-?\d+\.\d+(?:e-?\d+)?", body[body.index("{"):])]
    return names, np.array(thrs), np.array(vals[:len(names) * 64]).reshape(len(names), 64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", help="recorder take of the sound to teach")
    ap.add_argument("--n", type=int, default=5, help="examples to teach from (the first n onsets)")
    ap.add_argument("--gap", type=float, default=1.0, help="minimum s between onsets of the taught sound")
    ap.add_argument("--negatives", nargs="*", default=[], help="takes of other sounds")
    ap.add_argument("--model", default=os.path.join(ROOT, "model", "ss_encoder.pth"))
    ap.add_argument("--profiles", default=os.path.join(ROOT, "firmware", "soundsense", "assets", "ss_builtin_profiles.h"))
    ap.add_argument("--json")
    a = ap.parse_args()

    enc, _ = load_encoder(a.model)
    feat = Features()

    def embed(w):
        with torch.no_grad():
            return normalize(enc(feat(torch.from_numpy(np.stack(w).astype(np.float32)))).numpy())

    x = read_stereo(a.wav)
    ons = onsets(x, a.gap)
    print(f"{a.wav}: {len(ons)} onsets at {[round(s / SR, 2) for s in ons]} s")
    for s in ons:
        seg = x[s - 80:s - 80 + SR]
        pk = 20 * np.log10(np.abs(seg).max(axis=0) + 1e-9)
        print(f"  t={s / SR:5.2f} s  peak mic4 {pk[0]:5.1f} / mic5 {pk[1]:5.1f} dBFS  clipped samples {(np.abs(seg) > 0.99).sum(axis=0).tolist()}")
    T = embed(windows(x, ons))
    proto, thr = make_profile(T[:a.n])
    groups = {"taught sound": T}
    for p in a.negatives:
        y = read_stereo(p)
        groups[f"onsets in {os.path.basename(p)}"] = embed(windows(y, onsets(y, 0.4)))
        groups[f"background of {os.path.basename(p)}"] = embed(quiet_windows(y))
    names, bthr, P = builtins(a.profiles)

    print(f"\nprofile from the first {a.n}: threshold {thr:.3f}")
    out = dict(wav=a.wav, n_onsets=len(ons), n_taught=a.n, threshold=thr, groups={})
    for g, E in groups.items():
        s = E @ proto
        acc = int((s >= thr).sum())
        S = E @ P.T
        best = S.argmax(1)
        over = [names[b] for i, b in enumerate(best) if S[i, b] >= bthr[b]]
        print(f"  {g:28s} n={len(E):2d}  vs profile {s.min():.2f}..{s.max():.2f}  accepted {acc}/{len(E)}  "
              f"built-ins over threshold: {', '.join(f'{k} {over.count(k)}' for k in dict.fromkeys(over)) or 'none'}")
        out["groups"][g] = dict(n=len(E), score_min=float(s.min()), score_max=float(s.max()), accepted=acc,
                                builtin_over_thr={k: over.count(k) for k in dict.fromkeys(over)})
    if a.json:
        json.dump(out, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
