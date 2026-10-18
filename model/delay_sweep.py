#!/usr/bin/env python3
"""Decision delay sweep, offline (docs/STATUS.md "Alert delay", docs/HANDOFF-next.md item 11).

The board classifies each sound once, DECIDE_DELAY after its onset, from the last 1 s of audio
(firmware/soundsense/main.cpp). At 0.8 s the onset sits 0.2 s into the window and the alert shows
about 0.85 s after the sound starts, against a 0.5 s target. This script plays each clip through a
port of the board's onset detector (ss_audio.c kws_block_hook) and main loop (HOLDOFF, the
MIN_SNR_DB gate), takes the 1 s window the board would encode at each delay, and reruns the G1
measurements of eval_ss.py (a few-shot, b taught/untaught, c built-ins) on those windows.

Room: each clip gets 1 s of lead-in and 1 s of tail, with pink noise (the test part of
pink_noise.wav) under all of it, scaled so the clip's loudest 20 ms is 10-25 dB over the noise
(random per clip, fixed seed; G3 on the board saw +8 to +22 dB, docs/gates/g3_builtin_trained.txt).

Per clip, the first decision that clears MIN_SNR_DB stands for the clip. Its SNR is measured over the
encoder's window (the board's snr_of() reads onset - 0.2 s .. onset + 0.8 s, which a shorter delay
doesn't have yet).

Built-in profiles:
  product  ss_builtin_profiles.h's recipe: each fold 1-4 clip's loudest 1 s (export_ss.py)
  onset    the same clips (and 200 Speech Commands train words) framed at the delay under test
Taught sounds: shots and test sounds both at the delay under test (teaching goes through decide()).
"loudest" is the G1 framing (each clip's loudest 1 s, no room noise), for reference.

Float model only (int8 was within 3 points of float in G1).

Two-stage (--early): decide at the early delay only when the best match is an alerting built-in at
least a margin over its threshold; otherwise wait for the 0.8 s decision as today.

Usage (inside et-venv): python delay_sweep.py [--delays 0.3 0.4 0.5 0.6 0.8] [--early 0.45] [--seed 5]
                        [--out out/delay_sweep.json]
"""
import argparse
import json
import math
import os
import sys
import time

import numpy as np

import kws_common as K
import ss_common as S
from eval_ss import eval_a, eval_b, eval_c
from export_ss import PROFILE_NAMES, builtin_examples, features, load_model, run

HERE = os.path.dirname(os.path.abspath(__file__))
SR, CLIP, BLK = K.SR, K.CLIP, K.HOP          # 16 kHz, 1 s window, 20 ms blocks
ONSET_RATIO, REFRACT = 6.0, SR // 10          # ss_audio.h
HOLDOFF = SR * 12 // 10                       # main.cpp
MIN_SNR_DB = 8.0
LEAD = SR                                     # room before the clip: the floor settles in the first 0.5 s
SNR_RANGE = (10.0, 25.0)
LATE = 0.8                                    # DECIDE_DELAY today
OVERHEAD = 0.05                               # onset to alert on the board, beyond the delay (0.85 s at 0.8 s)


# ---------------------------------------------------------------- the board, on the host

def detect(x):
    """kws_block_hook over an int16 stream: the onsets main.cpp acts on (HOLDOFF applied), the
    floor in dB after each block, and each block's mean power."""
    x = np.asarray(x, dtype=np.float32) / 32768.0
    nb = len(x) // BLK
    a = np.abs(x[:nb * BLK]).reshape(nb, BLK)
    peak, pw = a.max(1), (a ** 2).mean(1)
    floor, refract, last = 1e-4, 0, None
    onsets, floor_db = [], np.zeros(nb)
    for b in range(nb):
        thr = ONSET_RATIO * math.sqrt(floor)
        if b + 1 > 25 and peak[b] > thr:
            s = b * BLK + int(np.argmax(a[b] > thr))
            if s >= refract:
                if last is None or s - last >= HOLDOFF:
                    onsets.append(s)
                    last = s
                refract = s + REFRACT
        p = float(pw[b])
        if b + 1 <= 25:
            floor += 0.2 * (p - floor)
        elif p < 10.0 * floor:
            floor += 0.02 * (p - floor)
        else:
            floor += 0.001 * (p - floor)
        floor = max(floor, 1e-10)
        floor_db[b] = 10.0 * math.log10(floor + 1e-12)
    return onsets, floor_db, pw


def window_at(x, s, delay, floor_db, pw):
    """(window float [16000], snr dB) the board decides on for onset s at this delay: the 1 s that
    ends at the first block boundary >= s + delay; None if the stream ends first."""
    end = -(-(s + int(round(delay * SR))) // BLK) * BLK
    if end > len(floor_db) * BLK or end < CLIP:
        return None
    b1 = end // BLK
    snr = 10.0 * math.log10(float(pw[b1 - CLIP // BLK:b1].max()) + 1e-12) - floor_db[b1 - 1]
    return np.asarray(x[end - CLIP:end], dtype=np.float32) / 32768.0, snr


def decision_windows(x, delay):
    """[(window, snr dB, onset sample)] for each decision the board makes in x at this delay."""
    onsets, floor_db, pw = detect(x)
    return [(*w, s) for s in onsets if (w := window_at(x, s, delay, floor_db, pw)) is not None]


def two_stage_windows(streams, early, late):
    """Per stream, the first onset whose late window clears MIN_SNR_DB: (early window or None when
    that one is under MIN_SNR_DB, late window); None for streams with no such onset."""
    out = []
    for x in streams:
        onsets, floor_db, pw = detect(x)
        got = None
        for s in onsets:
            wl = window_at(x, s, late, floor_db, pw)
            if wl is not None and wl[1] >= MIN_SNR_DB:
                we = window_at(x, s, early, floor_db, pw)
                got = (we[0] if we is not None and we[1] >= MIN_SNR_DB else None, wl[0])
                break
        out.append(got)
    return out


def in_room(clip, room, snr_db):
    """The clip after LEAD samples of room noise, with room noise under it and 1 s after, the noise
    scaled so the clip's loudest 20 ms block is snr_db over it."""
    clip = np.asarray(clip, dtype=np.float32)
    n = LEAD + len(clip) + SR
    nb = len(clip) // BLK
    p_clip = float((clip[:nb * BLK].reshape(nb, BLK) ** 2).mean(1).max())
    noise = room[:n] * math.sqrt(p_clip / 10 ** (snr_db / 10) / max(float((room[:n] ** 2).mean()), 1e-12))
    y = noise.copy()
    y[LEAD:LEAD + len(clip)] += clip
    return np.clip(np.round(y), -32768, 32767).astype(np.int16)


class Rooms:
    """Each clip's room: a crop of the pink noise test segment and an SNR, fixed per clip key."""

    def __init__(self, d, seed=5):
        self.noise = np.asarray(d.noise["pink_noise_test"], dtype=np.float32)
        self.seed = seed

    def stream(self, key, clip):
        rng = np.random.default_rng([self.seed, *key])
        n = LEAD + len(clip) + SR
        s = int(rng.integers(0, len(self.noise) - n))
        return in_room(clip, self.noise[s:s + n], float(rng.uniform(*SNR_RANGE)))


# ---------------------------------------------------------------- pools at one delay

def first_windows(streams, delay):
    """Per stream: the first decision's window that clears MIN_SNR_DB (or None), and every
    such decision's window."""
    first, every = [], []
    for x in streams:
        w = [win for win, snr, _ in decision_windows(x, delay) if snr >= MIN_SNR_DB]
        first.append(w[0] if w else None)
        every.append(w)
    return first, every


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", default=os.path.join(HERE, "ss_encoder.pth"))
    ap.add_argument("--delays", type=float, nargs="+", default=[0.3, 0.4, 0.45, 0.5, 0.6, 0.8])
    ap.add_argument("--early", type=float, nargs="*", default=[0.4, 0.45, 0.5],
                    help="two-stage: early delays to try before the 0.8 s decision")
    ap.add_argument("--seed", type=int, default=5, help="rooms (noise crop and level per clip)")
    ap.add_argument("--out", default=os.path.join(HERE, "out", "delay_sweep.json"))
    args = ap.parse_args()

    t0 = time.time()
    d = S.Data()
    model = load_model(args.ckpt)
    embed = lambda x: run(model, features(np.asarray(x, dtype=np.float32)), bs=500)
    rooms = Rooms(d, args.seed)
    rng = np.random.default_rng(1234)

    # Clips, as in eval_ss.Pools, but whole (5 s ESC-50, 1 s Speech Commands) so the board finds the onset.
    h_ids = np.concatenate([d.esc_ids(c, [1, 2, 3, 4, 5]) for c in S.HELDOUT])
    h_y = np.array([S.HELDOUT.index(d.esc_cat[i]) for i in h_ids])
    bg = d.noise_crops(S.NOISE_FILES, "test", 300, rng)
    sp_ids = np.sort(rng.choice(len(d.sc_test_y), 500, replace=False))
    bi_ids, bi_y = [], []
    for k, name in enumerate(PROFILE_NAMES[:7]):
        ids = d.esc_ids(S.BUILTINS[name], [5])
        bi_ids += list(ids)
        bi_y += [k] * len(ids)
    bi_ids, bi_y = np.array(bi_ids), np.array(bi_y)
    tap_test = d.tap_windows("test").astype(np.float32) / 32768.0     # water, continuous: as in G1
    others = [c for c in d.esc_train_cats if c not in S.BUILTINS.values()]
    neg_ids = np.concatenate([d.esc_ids(c, [5]) for c in others])
    tr_ids = {name: d.esc_ids(S.BUILTINS[name], [1, 2, 3, 4]) for name in PROFILE_NAMES[:7]}
    ex_rng = np.random.default_rng(11)                                   # as builtin_examples()
    product_ex = builtin_examples(d, ex_rng)
    sc_tr_ids = np.sort(np.random.default_rng(11).choice(len(d.sc_train_y), 200, replace=False))

    esc_stream = lambda i: rooms.stream((0, int(i)), d.esc_x[i])
    streams = {
        "h": [esc_stream(i) for i in h_ids],
        "bi": [esc_stream(i) for i in bi_ids],
        "neg": [esc_stream(i) for i in neg_ids],
        "sp": [rooms.stream((1, int(i)), d.sc_test_x[i]) for i in sp_ids],
        "sc_tr": [rooms.stream((2, int(i)), d.sc_train_x[i]) for i in sc_tr_ids],
        **{f"tr_{n}": [esc_stream(i) for i in ids] for n, ids in tr_ids.items()},
    }
    print(f"streams built in {time.time() - t0:.0f} s: heldout {len(h_ids)}, built-in {len(bi_ids)}, "
          f"neg {len(neg_ids)}, speech {len(sp_ids)}", flush=True)

    z_bg = S.normalize(embed(bg))
    product = [S.make_profile(embed(product_ex[n])) for n in PROFILE_NAMES]
    p_protos, p_thrs = np.stack([p for p, _ in product]), np.array([t for _, t in product])

    def measure(z_h, hy, z_bi, by, z_neg, z_sp, protos, thrs):
        q = int(min(15, np.bincount(hy, minlength=len(S.HELDOUT)).min() - 5))   # a class can keep < 20 clips
        a = eval_a(z_h, hy, 5, queries=q)
        b = eval_b(z_h, hy, z_bg, z_sp, protos, thrs, queries=q)
        c = eval_c(z_bi, by, z_neg, z_bg, z_sp, protos, thrs)
        return {"queries": q, "a_5way5shot": a["acc"], "b_taught_ok": b["taught_ok"], "b_untaught_rej": b["untaught_rej"],
                "b_auroc": b["auroc"], "b_speech_rej": b["sp_rej"], "c_recognised": c["recognised"],
                "c_per_class": c["per_class"], "c_neg_false_accept": c["neg_false_accept"],
                "c_speech_false_accept": c["speech_false_accept"], "c_thresholds": c["thresholds"]}

    res = {"model": os.path.relpath(args.ckpt, HERE), "snr_range_db": SNR_RANGE, "seed": args.seed, "rows": {}}

    # Reference: G1 framing (loudest 1 s, no room).
    z = {"h": [d.esc_window(i) for i in h_ids], "bi": [d.esc_window(i) for i in bi_ids],
         "neg": [d.esc_window(i) for i in neg_ids], "sp": list(np.asarray(d.sc_test_x[sp_ids]))}
    z = {k: S.normalize(embed(np.stack(v).astype(np.float32) / 32768.0)) for k, v in z.items()}
    z_bi = np.concatenate([z["bi"], S.normalize(embed(tap_test))])
    by = np.concatenate([bi_y, np.full(len(tap_test), PROFILE_NAMES.index("water"))])
    r = measure(z["h"], h_y, z_bi, by, z["neg"], z["sp"], p_protos, p_thrs)
    res["rows"]["loudest"] = {"product_profiles": r}
    print(f"loudest: a {r['a_5way5shot']:.3f}  b ok {r['b_taught_ok']:.3f} rej {r['b_untaught_rej']:.3f}  "
          f"c rec {r['c_recognised']:.3f} negFA {r['c_neg_false_accept']:.3f}", flush=True)

    for delay in args.delays:
        t1 = time.time()
        w = {k: first_windows(v, delay) for k, v in streams.items()}
        firsts = {k: f for k, (f, _) in w.items()}
        kept = {k: np.array([x is not None for x in f]) for k, f in firsts.items()}
        emb = lambda k: S.normalize(embed(np.stack([x for x in firsts[k] if x is not None])))
        z = {k: emb(k) for k in ("h", "bi", "neg", "sp")}
        hy = h_y[kept["h"]]
        z_bi = np.concatenate([z["bi"], S.normalize(embed(tap_test))])
        by = np.concatenate([bi_y[kept["bi"]], np.full(len(tap_test), PROFILE_NAMES.index("water"))])

        # Every decision on the built-in clips, not just the first.
        every = [(x, bi_y[j]) for j, ws in enumerate(w["bi"][1]) for x in ws]
        z_every = S.normalize(embed(np.stack([x for x, _ in every])))
        y_every = np.array([y for _, y in every])

        # Onset profiles: the training clips framed at this delay.
        on_ex = {}
        for name in PROFILE_NAMES[:7]:
            x = [f for f in firsts[f"tr_{name}"] if f is not None]
            if name == "water":
                x += list(product_ex["water"][len(tr_ids["water"]):])   # running_tap crops, as product
            on_ex[name] = np.stack(x)
        on_ex["speech"] = np.stack([f for f in firsts["sc_tr"] if f is not None])
        on_ex["background"] = product_ex["background"]
        onset = [S.make_profile(embed(on_ex[n])) for n in PROFILE_NAMES]
        o_protos, o_thrs = np.stack([p for p, _ in onset]), np.array([t for _, t in onset])

        row = {"decided": {k: float(v.mean()) for k, v in kept.items() if k in ("h", "bi", "neg", "sp")}}
        for tag, protos, thrs in (("product_profiles", p_protos, p_thrs), ("onset_profiles", o_protos, o_thrs)):
            r = measure(z["h"], hy, z_bi, by, z["neg"], z["sp"], protos, thrs)
            pe, _, _ = S.match(z_every, protos, thrs)
            r["c_recognised_every_decision"] = float(np.mean(pe == y_every))
            r["c_recognised_incl_undecided"] = float(
                (np.sum(S.match(z["bi"], protos, thrs)[0] == bi_y[kept["bi"]])
                 + np.sum(S.match(S.normalize(embed(tap_test)), protos, thrs)[0] == PROFILE_NAMES.index("water")))
                / (len(bi_y) + len(tap_test)))
            row[tag] = r
            print(f"delay {delay:.2f} {tag:16s}: a {r['a_5way5shot']:.3f}  b ok {r['b_taught_ok']:.3f} "
                  f"rej {r['b_untaught_rej']:.3f}  c rec {r['c_recognised']:.3f} (every {r['c_recognised_every_decision']:.3f}, "
                  f"w/ undecided {r['c_recognised_incl_undecided']:.3f}) negFA {r['c_neg_false_accept']:.3f}  "
                  f"speechFA {r['c_speech_false_accept']:.3f}", flush=True)
        print(f"delay {delay:.2f}: decided " + ", ".join(f"{k} {v:.2f}" for k, v in row["decided"].items())
              + f"; {len(every)} built-in decisions; {time.time() - t1:.0f} s", flush=True)
        res["rows"][f"{delay:.2f}"] = row

    # Two-stage: decide at `early` when the best match is an alerting built-in at least `margin` over
    # its threshold, otherwise at LATE as today. Built-in profiles as shipped.
    alerting = np.array([1, 1, 1, 1, 1, 1, 1, 0, 0], dtype=bool)
    res["two_stage"] = {}
    for early in args.early:
        tw = {k: two_stage_windows(streams[k], early, LATE) for k in ("bi", "neg", "h", "sp")}
        st = {}
        for k, v in tw.items():
            keep = [w for w in v if w is not None]
            ye = [j for j, w in enumerate(keep) if w[0] is not None]
            se = np.full(len(keep), -1.0)
            pe = np.full(len(keep), -1)
            if ye:
                ze = S.normalize(embed(np.stack([keep[j][0] for j in ye])))
                sim = ze @ p_protos.T
                pe[ye], se[ye] = sim.argmax(1), sim.max(1)
            pl = S.match(S.normalize(embed(np.stack([w[1] for w in keep]))), p_protos, p_thrs)[0]
            truth = bi_y[[w is not None for w in tw["bi"]]] if k == "bi" else None
            st[k] = (pe, se, pl, truth)
        rows = {}
        for margin in (0.0, 0.05, 0.10, 0.15, math.inf):   # inf: never early, today's board
            r = {}
            for k, (pe, se, pl, truth) in st.items():
                fire = (pe >= 0) & alerting[np.clip(pe, 0, 8)] & (se >= p_thrs[np.clip(pe, 0, 8)] + margin)
                final = np.where(fire, pe, pl)
                alert = (final >= 0) & alerting[np.clip(final, 0, 8)]
                if k == "bi":
                    ok = final == truth
                    r["bi_recognised"] = float(ok.mean())
                    r["bi_recognised_early"] = float((ok & fire).mean())
                    r["bi_wrong_early"] = float((fire & (pe != truth)).mean())
                    r["bi_mean_latency_s"] = float(np.where(fire, early, LATE)[ok].mean() + OVERHEAD) if ok.any() else None
                else:
                    r[f"{k}_false_alert"] = float(alert.mean())
            rows[f"{margin:.2f}"] = r
            print(f"two-stage {early:.2f}/{LATE:.2f} margin {margin:.2f}: built-ins {r['bi_recognised']:.3f} "
                  f"(early {r['bi_recognised_early']:.3f}, wrong early {r['bi_wrong_early']:.3f}, "
                  f"mean latency {r['bi_mean_latency_s']:.2f} s)  false alerts: other sounds {r['neg_false_alert']:.3f} "
                  f"untrained {r['h_false_alert']:.3f} speech {r['sp_false_alert']:.3f}", flush=True)
        res["two_stage"][f"{early:.2f}"] = rows

    res["date"] = time.strftime("%Y-%m-%d %H:%M %Z")
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(res, f, indent=1)
    print(f"wrote {args.out} in {time.time() - t0:.0f} s")


if __name__ == "__main__":
    sys.exit(main())
