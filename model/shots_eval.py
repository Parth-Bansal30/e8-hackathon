#!/usr/bin/env python3
"""Taught sounds offline: 5 vs 8 examples, and the T2 "unlike the others" check (HANDOFF-next.md items 1-2).

G1 pools and framing (eval_ss.py), float encoder as shipped, 600 episodes each. T2 rule under test:
an example whose leave-one-out similarity is more than UNLIKE below the median of the others is
flagged, and the user redoes it (here: replaced by a fresh example of the same class, as a redo
would be). T6: how often a new profile lands close to a built-in one.

Usage (inside et-venv): python shots_eval.py
"""
import numpy as np

import ss_common as S
from eval_ss import Pools, eval_b
from export_ss import PROFILE_NAMES, builtin_profiles, features, load_model, run

UNLIKE = 0.15


def main():
    d = S.Data()
    pools = Pools(d)
    model = load_model("ss_encoder.pth")
    embed = lambda x: run(model, features(np.asarray(x, dtype=np.float32)), bs=500)
    z = {k: S.normalize(embed(getattr(pools, k))) for k in ("h_x", "bg_x", "sp_x")}
    _, protos, thrs = builtin_profiles(embed, d, np.random.default_rng(11))
    for shots in (5, 8):
        b = eval_b(z["h_x"], pools.h_y, z["bg_x"], z["sp_x"], protos, thrs, shots=shots)
        print(f"{shots} examples: taught accepted {b['taught_ok']:.3f}, untaught rejected {b['untaught_rej']:.3f}, "
              f"AUROC {b['auroc']:.3f}, mean taught thr {b['taught_thr']:.3f}", flush=True)

    # T2: how often a shot gets flagged, and what redoing it does to the threshold
    rng = np.random.default_rng(300)
    h, hy = z["h_x"], pools.h_y
    flagged = 0
    thr_before, thr_after, sim_builtin = [], [], []
    for _ in range(2000):
        c = rng.integers(len(S.HELDOUT))
        idx = rng.permutation(np.flatnonzero(hy == c))
        ex = h[idx[:8]]
        total = ex.sum(0)
        loo = np.array([S.normalize(total - ex[i]) @ ex[i] for i in range(8)])
        p, t = S.make_profile(ex)
        thr_before.append(t)
        sim_builtin.append(float((protos[:7] @ p).max()))
        w = int(loo.argmin())
        if np.median(np.delete(loo, w)) - loo[w] > UNLIKE:
            flagged += 1
            ex2 = ex.copy()
            ex2[w] = h[idx[8]]
            thr_after.append(S.make_profile(ex2)[1])
        else:
            thr_after.append(t)
    print(f"T2 'unlike the others' (> {UNLIKE} under the median): flagged in {flagged / 2000:.1%} of 8-example teaches; "
          f"mean thr {np.mean(thr_before):.3f} -> {np.mean(thr_after):.3f} after one redo")
    sb = np.array(sim_builtin)
    print("T6: new profile's cosine to the closest alerting built-in: median %.2f, p90 %.2f, max %.2f; "
          "share over 0.80: %.1f%%, over 0.70: %.1f%%" % (np.median(sb), np.percentile(sb, 90), sb.max(),
                                                       100 * np.mean(sb > 0.8), 100 * np.mean(sb > 0.7)))
    # T6 between taught classes: profile-to-profile cosine for pairs of different held-out classes
    pp = []
    for _ in range(2000):
        a, b2 = rng.choice(len(S.HELDOUT), 2, replace=False)
        pa = S.make_profile(h[rng.permutation(np.flatnonzero(hy == a))[:8]])[0]
        pb = S.make_profile(h[rng.permutation(np.flatnonzero(hy == b2))[:8]])[0]
        pp.append(float(pa @ pb))
    pp = np.array(pp)
    print("T6: two different taught sounds, profile cosine: median %.2f, p90 %.2f, max %.2f" %
          (np.median(pp), np.percentile(pp, 90), pp.max()))


if __name__ == "__main__":
    main()
