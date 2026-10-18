#!/usr/bin/env python3
"""G1 evaluation of the Sound Sense encoder (docs/HANDOFF-gates.md section 1.3, a-e).

Runs every measurement for the float model and for the int8 PT2E-quantized model on the host
(quantized exactly as export_ss.py does it, with the same calibration clips and the same built-in
profiles that go into ss_builtin_profiles.h). Evaluation windows are each clip's loudest 1 s.

  a  few-shot: 600 episodes 5-way 5-shot (5 support + 15 query per class) on the held-out
     classes, nearest profile, no rejection; and 10-way 5-shot
  b  unknown rejection: per episode 5 taught held-out classes (5 examples, threshold rule) next
     to the 9 built-in profiles; 15 test clips per taught class, 15 from each untaught held-out
     class, 50 background and 50 speech clips
  c  built-ins: fold-5 clips of the built-in classes vs 100 fold-5 clips of other trained classes
  d  int8 vs float
  e  parameters, MACs, .pte size, top-level ops (from out/ss_export_info.json, see export_ss.py)

Usage (inside et-venv): python eval_ss.py [--ckpt ss_encoder.pth] [--out out/g1_metrics.json]
"""
import argparse
import json
import os
import subprocess
import sys
import time

import numpy as np

import ss_common as S
from export_ss import (PROFILE_ALERTING, PROFILE_NAMES, builtin_examples, calib_audio, features,
                       load_model, quantize, run)

HERE = os.path.dirname(os.path.abspath(__file__))
ALERTING = np.array(PROFILE_ALERTING, dtype=bool)


# ---------------------------------------------------------------- helpers

def auroc(pos, neg):
    """P(score of a positive > score of a negative), ties counted half (Mann-Whitney U)."""
    pos, neg = np.asarray(pos), np.asarray(neg)
    s = np.concatenate([pos, neg])
    order = s.argsort(kind="mergesort")
    ranks = np.empty(len(s))
    ranks[order] = np.arange(1, len(s) + 1)
    _, inv, cnt = np.unique(s, return_inverse=True, return_counts=True)
    ranks = (np.bincount(inv, ranks) / cnt)[inv]
    return float((ranks[:len(pos)].sum() - len(pos) * (len(pos) + 1) / 2) / (len(pos) * len(neg)))


def ci95(per_episode):
    a = np.asarray(per_episode, dtype=np.float64)
    return float(1.96 * a.std(ddof=1) / np.sqrt(len(a)))


def make_profile_alt(embs, pct=10, margin=S.THR_MARGIN, lo=0.3, hi=S.THR_HI):
    """ALTERNATIVE threshold (not the product rule), for profiles with many examples: the 10th
    percentile of the leave-one-out similarities minus the margin, clamped to [0.3, 0.85]."""
    z = S.normalize(embs)
    total = z.sum(0)
    loo = [float(S.normalize(total - z[i]) @ z[i]) for i in range(len(z))]
    return S.normalize(total), float(np.clip(np.percentile(loo, pct) - margin, lo, hi))


# ---------------------------------------------------------------- data pools

class Pools:
    """Every clip the evaluation uses, as float audio, built once with fixed seeds."""

    def __init__(self, d):
        rng = np.random.default_rng(1234)
        # Held-out classes: all 5 folds, 40 clips per class.
        ids = np.concatenate([d.esc_ids(c, [1, 2, 3, 4, 5]) for c in S.HELDOUT])
        self.h_x = np.stack([d.esc_window(i) for i in ids]).astype(np.float32) / 32768.0
        self.h_y = np.array([S.HELDOUT.index(d.esc_cat[i]) for i in ids])
        # Background (noise-file test segments) and speech (Speech Commands test).
        self.bg_x = d.noise_crops(S.NOISE_FILES, "test", 300, rng)
        sc = np.sort(rng.choice(len(d.sc_test_y), 500, replace=False))
        self.sp_x = np.asarray(d.sc_test_x[sc], dtype=np.float32) / 32768.0
        # Built-in positives: fold 5 (water: pouring_water fold 5 + running_tap test windows).
        xs, ys = [], []
        for k, name in enumerate(PROFILE_NAMES[:7]):
            x = [d.esc_window(i) for i in d.esc_ids(S.BUILTINS[name], [5])]
            if name == "water":
                x += list(d.tap_windows("test"))
            xs += x
            ys += [k] * len(x)
        self.bi_x = np.stack(xs).astype(np.float32) / 32768.0
        self.bi_y = np.array(ys)
        # Negatives: 100 fold-5 clips of the other trained (non-held-out, non-built-in) classes.
        others = [c for c in d.esc_train_cats if c not in S.BUILTINS.values()]
        ids = np.concatenate([d.esc_ids(c, [5]) for c in others])
        ids = np.sort(rng.choice(ids, 100, replace=False))
        self.neg_x = np.stack([d.esc_window(i) for i in ids]).astype(np.float32) / 32768.0
        self.neg_cat = [str(d.esc_cat[i]) for i in ids]
        self.n_other_classes = len(others)


# ---------------------------------------------------------------- measurements

def eval_a(h, hy, ways, episodes=600, shots=5, queries=15, seed=100):
    rng = np.random.default_rng(seed)
    z = S.normalize(h)
    by = [np.flatnonzero(hy == c) for c in range(len(S.HELDOUT))]
    accs, per_cls = [], np.zeros((len(S.HELDOUT), 2))
    conf = np.zeros((len(S.HELDOUT), len(S.HELDOUT)), dtype=int)
    for _ in range(episodes):
        cls = rng.choice(len(by), ways, replace=False)
        protos, q, ql = [], [], []
        for j, c in enumerate(cls):
            p = rng.permutation(by[c])
            protos.append(S.make_profile(z[p[:shots]])[0])
            q.append(z[p[shots:shots + queries]])
            ql += [j] * queries
        pred = (np.concatenate(q) @ np.stack(protos).T).argmax(1)
        ql = np.array(ql)
        accs.append(np.mean(pred == ql))
        for j, c in enumerate(cls):
            per_cls[c] += [np.sum(pred[ql == j] == j), queries]
        np.add.at(conf, (cls[ql], cls[pred]), 1)
    return {"acc": float(np.mean(accs)), "ci95": ci95(accs), "episodes": episodes,
            "per_class": {S.HELDOUT[c]: float(per_cls[c, 0] / per_cls[c, 1]) for c in range(len(S.HELDOUT))},
            "confusion_rows_truth": {S.HELDOUT[c]: dict(zip(S.HELDOUT, map(int, conf[c])))
                                     for c in range(len(S.HELDOUT))}}


def rejected(pred):
    """No alert: unknown (-1), or matched a non-alerting built-in (speech / background)."""
    builtin = (pred >= 0) & (pred < len(ALERTING))
    return (pred < 0) | (builtin & ~ALERTING[np.clip(pred, 0, len(ALERTING) - 1)])


SWEEP = np.round(np.arange(0.0, 0.91, 0.05), 2)


def eval_b(h, hy, bg, sp, b_protos, b_thrs, episodes=600, shots=5, queries=15, n_bg=50, n_sp=50, seed=200):
    rng = np.random.default_rng(seed)
    sweep = np.zeros((len(SWEEP), 2))
    by = [np.flatnonzero(hy == c) for c in range(len(S.HELDOUT))]
    nb = len(b_protos)
    r = {k: [] for k in ("taught_ok", "taught_rej", "taught_wrong", "untaught_rej", "untaught_as_taught",
                         "untaught_as_builtin", "bg_rej", "sp_rej", "auroc", "auroc_margin", "taught_thr")}
    for _ in range(episodes):
        perm = rng.permutation(len(by))
        taught, untaught = perm[:5], perm[5:10]
        protos, thrs, tq, tl = list(b_protos), list(b_thrs), [], []
        for j, c in enumerate(taught):
            p = rng.permutation(by[c])
            pr, t = S.make_profile(h[p[:shots]])
            protos.append(pr)
            thrs.append(t)
            r["taught_thr"].append(t)
            tq.append(h[p[shots:shots + queries]])
            tl += [nb + j] * queries
        tq, tl = np.concatenate(tq), np.array(tl)
        uq = np.concatenate([h[rng.permutation(by[c])[:queries]] for c in untaught])
        bq = bg[rng.choice(len(bg), n_bg, replace=False)]
        sq = sp[rng.choice(len(sp), n_sp, replace=False)]
        protos, thrs = np.stack(protos), np.array(thrs)

        tp, ts, tsim = S.match(tq, protos, thrs)
        up, us, usim = S.match(uq, protos, thrs)
        bp, _, _ = S.match(bq, protos, thrs)
        sp_, _, _ = S.match(sq, protos, thrs)
        r["taught_ok"].append(np.mean(tp == tl))
        r["taught_rej"].append(np.mean(rejected(tp)))
        r["taught_wrong"].append(np.mean(~(tp == tl) & ~rejected(tp)))
        r["untaught_rej"].append(np.mean(rejected(up)))
        r["untaught_as_taught"].append(np.mean(up >= nb))
        r["untaught_as_builtin"].append(np.mean((up >= 0) & (up < nb) & ~rejected(up)))
        r["bg_rej"].append(np.mean(rejected(bp)))
        r["sp_rej"].append(np.mean(rejected(sp_)))
        r["auroc"].append(auroc(ts, us))
        for k, t in enumerate(SWEEP):  # ALTERNATIVE: one fixed threshold for the taught profiles
            th = np.concatenate([thrs[:nb], np.full(len(thrs) - nb, t)])
            sweep[k] += [np.mean(S.match(tq, protos, th)[0] == tl), np.mean(rejected(S.match(uq, protos, th)[0]))]
        tb, ub = tsim.argmax(1), usim.argmax(1)
        r["auroc_margin"].append(auroc(ts - thrs[tb], us - thrs[ub]))
    out = {k: float(np.mean(v)) for k, v in r.items()}
    out["ci95"] = {k: ci95(r[k]) for k in ("taught_ok", "untaught_rej", "auroc")}
    out["episodes"] = episodes
    sweep /= episodes
    out["ALT_fixed_taught_threshold_sweep"] = {f"{t:.2f}": {"taught_ok": float(a), "untaught_rej": float(u)}
                                               for t, (a, u) in zip(SWEEP, sweep)}
    ok90 = [u for a, u in sweep if a >= 0.90]
    rej80 = [a for a, u in sweep if u >= 0.80]
    out["ALT_untaught_rej_at_taught_ok_ge_90"] = float(max(ok90)) if ok90 else None
    out["ALT_taught_ok_at_untaught_rej_ge_80"] = float(max(rej80)) if rej80 else None
    out["notes"] = ("taught_ok = accepted as the right taught class; *_rej = no alert (unknown, or matched speech/"
                    "background); auroc = best-match cosine, taught vs untaught queries, per episode then averaged; "
                    "auroc_margin = same with (best score - that profile's threshold)")
    return out


def eval_c(bi, bi_y, neg, bg, sp, protos, thrs):
    p, s, _ = S.match(bi, protos, thrs)
    per = {PROFILE_NAMES[k]: float(np.mean(p[bi_y == k] == k)) for k in range(7)}
    conf = np.zeros((7, len(PROFILE_NAMES) + 1), dtype=int)  # last column: unknown
    for t, q in zip(bi_y, p):
        conf[t, q if q >= 0 else -1] += 1
    np_, _, _ = S.match(neg, protos, thrs)
    bp, _, _ = S.match(bg, protos, thrs)
    spp, _, _ = S.match(sp, protos, thrs)
    fa = lambda q: float(np.mean((q >= 0) & ALERTING[np.clip(q, 0, len(ALERTING) - 1)]))
    return {"recognised": float(np.mean(p == bi_y)), "per_class": per, "n_pos": int(len(bi_y)),
            "neg_false_accept": fa(np_), "n_neg": int(len(neg)),
            "bg_false_accept": fa(bp), "speech_false_accept": fa(spp),
            "thresholds": {n: float(t) for n, t in zip(PROFILE_NAMES, thrs)},
            "confusion_rows_truth": {PROFILE_NAMES[k]: dict(zip(PROFILE_NAMES + ["unknown"], map(int, conf[k])))
                                     for k in range(7)}}


def evaluate(name, embed_fn, d, pools):
    t0 = time.time()
    emb = {k: embed_fn(getattr(pools, k)) for k in ("h_x", "bg_x", "sp_x", "bi_x", "neg_x")}
    ex = builtin_examples(d, np.random.default_rng(11))  # same examples as export_ss.py
    ex_emb = {k: embed_fn(v) for k, v in ex.items()}
    print(f"[{name}] embedded in {time.time() - t0:.0f} s", flush=True)
    z = {k: S.normalize(v) for k, v in emb.items()}

    profs = [S.make_profile(ex_emb[n]) for n in PROFILE_NAMES]
    protos, thrs = np.stack([p for p, _ in profs]), np.array([t for _, t in profs])
    alt = [make_profile_alt(ex_emb[n]) for n in PROFILE_NAMES]
    alt_thrs = np.array([t for _, t in alt])

    res = {
        "a_5way5shot": eval_a(z["h_x"], pools.h_y, 5),
        "a_10way5shot": eval_a(z["h_x"], pools.h_y, 10),
        "b_rejection": eval_b(z["h_x"], pools.h_y, z["bg_x"], z["sp_x"], protos, thrs),
        "c_builtins": eval_c(z["bi_x"], pools.bi_y, z["neg_x"], z["bg_x"], z["sp_x"], protos, thrs),
        "c_builtins_ALT_threshold": eval_c(z["bi_x"], pools.bi_y, z["neg_x"], z["bg_x"], z["sp_x"], protos, alt_thrs),
        "b_rejection_ALT_builtin_threshold": eval_b(z["h_x"], pools.h_y, z["bg_x"], z["sp_x"], protos, alt_thrs),
    }
    res["c_builtins_ALT_threshold"]["rule"] = ("ALTERNATIVE, not the product rule: 10th percentile of leave-one-out "
                                               "similarities - 0.05, clamped to [0.3, 0.85]")
    a, b, c = res["a_5way5shot"], res["b_rejection"], res["c_builtins"]
    print(f"[{name}] a: 5w5s {a['acc']:.4f} +-{a['ci95']:.4f}  10w5s {res['a_10way5shot']['acc']:.4f}", flush=True)
    print(f"[{name}] b: taught ok {b['taught_ok']:.4f}  untaught rej {b['untaught_rej']:.4f}  bg rej {b['bg_rej']:.4f}  "
          f"speech rej {b['sp_rej']:.4f}  AUROC {b['auroc']:.4f} (margin {b['auroc_margin']:.4f})  "
          f"mean taught thr {b['taught_thr']:.3f}", flush=True)
    print(f"[{name}] b ALT fixed taught threshold: best untaught rej with taught ok >= 90%: "
          f"{b['ALT_untaught_rej_at_taught_ok_ge_90']}; best taught ok with untaught rej >= 80%: "
          f"{b['ALT_taught_ok_at_untaught_rej_ge_80']}; sweep "
          + " ".join(f"{t}:{v['taught_ok']:.2f}/{v['untaught_rej']:.2f}" for t, v in b["ALT_fixed_taught_threshold_sweep"].items()),
          flush=True)
    print(f"[{name}] c: built-ins recognised {c['recognised']:.4f}  neg false accept {c['neg_false_accept']:.4f}  "
          f"bg FA {c['bg_false_accept']:.4f}  speech FA {c['speech_false_accept']:.4f}", flush=True)
    print(f"[{name}]    per class " + ", ".join(f"{k} {v:.2f}" for k, v in c["per_class"].items())
          + "; thr " + ", ".join(f"{k} {v:.2f}" for k, v in c["thresholds"].items()), flush=True)
    ca, ba = res["c_builtins_ALT_threshold"], res["b_rejection_ALT_builtin_threshold"]
    print(f"[{name}] ALT built-in thresholds: c recognised {ca['recognised']:.4f}  neg FA {ca['neg_false_accept']:.4f}; "
          f"b taught ok {ba['taught_ok']:.4f} untaught rej {ba['untaught_rej']:.4f}; thr "
          + ", ".join(f"{k} {v:.2f}" for k, v in ca["thresholds"].items()), flush=True)
    return res, emb


def verdict(r8, rf):
    a8, b8, c8 = r8["a_5way5shot"]["acc"], r8["b_rejection"], r8["c_builtins"]
    gap = max(abs(r8["a_5way5shot"]["acc"] - rf["a_5way5shot"]["acc"]),
              abs(r8["b_rejection"]["taught_ok"] - rf["b_rejection"]["taught_ok"]),
              abs(r8["b_rejection"]["untaught_rej"] - rf["b_rejection"]["untaught_rej"]),
              abs(r8["c_builtins"]["recognised"] - rf["c_builtins"]["recognised"]),
              abs(r8["c_builtins"]["neg_false_accept"] - rf["c_builtins"]["neg_false_accept"]))
    crit = {"fewshot_int8_ge_85": a8 >= 0.85,
            "taught_accept_ge_90": b8["taught_ok"] >= 0.90,
            "untaught_reject_ge_80": b8["untaught_rej"] >= 0.80,
            "builtins_recognised_ge_85": c8["recognised"] >= 0.85,
            "builtins_false_accept_le_10": c8["neg_false_accept"] <= 0.10,
            "int8_within_2pt_of_float": gap <= 0.02}
    if all(crit.values()):
        v = "GO"
    elif a8 < 0.75:
        v = "NO-GO"
    else:
        v = "SOFT"
    return v, crit, float(gap)


def summary(r):
    """Key numbers of one metrics file, for side-by-side variants."""
    out = {"checkpoint": r.get("checkpoint")}
    for k in ("float", "int8"):
        if k in r:
            a, b, c = r[k]["a_5way5shot"], r[k]["b_rejection"], r[k]["c_builtins"]
            out[k] = {"a_5way5shot": a["acc"], "a_10way5shot": r[k]["a_10way5shot"]["acc"],
                      "b_taught_ok": b["taught_ok"], "b_untaught_rej": b["untaught_rej"], "b_auroc": b["auroc"],
                      "b_bg_rej": b["bg_rej"], "b_speech_rej": b["sp_rej"],
                      "c_recognised": c["recognised"], "c_neg_false_accept": c["neg_false_accept"]}
    for k in ("float", "int8"):
        if k in r:
            out[k]["b_ALT_untaught_rej_at_taught_ok_ge_90"] = r[k]["b_rejection"].get("ALT_untaught_rej_at_taught_ok_ge_90")
            out[k]["b_ALT_taught_ok_at_untaught_rej_ge_80"] = r[k]["b_rejection"].get("ALT_taught_ok_at_untaught_rej_ge_80")
    out["verdict"] = r.get("verdict", {}).get("result")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", default=os.path.join(HERE, "ss_encoder.pth"))
    ap.add_argument("--calib", type=int, default=600)
    ap.add_argument("--no_int8", action="store_true")
    ap.add_argument("--out", default=os.path.join(HERE, "out", "g1_metrics.json"))
    ap.add_argument("--label", default="", help="what this checkpoint is (stored in the JSON)")
    ap.add_argument("--compare", nargs="*", default=[], metavar="NAME=METRICS.json",
                    help="other variants' metrics files to summarise next to this one")
    args = ap.parse_args()

    d = S.Data()
    pools = Pools(d)
    model = load_model(args.ckpt)
    _, ck = S.load_encoder(args.ckpt)
    params, macs = S.count_params_macs(S.load_encoder(args.ckpt)[0])
    print(f"checkpoint {args.ckpt}: epoch {ck['epoch']}, cfg {ck['cfg']}, {params} params, {macs / 1e6:.2f} M MACs; "
          f"pools: heldout {len(pools.h_y)}, built-in pos {len(pools.bi_y)}, neg {len(pools.neg_x)} "
          f"({pools.n_other_classes} classes), bg {len(pools.bg_x)}, speech {len(pools.sp_x)}", flush=True)

    out = {"checkpoint": {"path": os.path.relpath(args.ckpt, HERE), "epoch": ck["epoch"], "cfg": ck["cfg"],
                          "train_args": ck.get("args"), "label": args.label}}
    rf, ef = evaluate("float", lambda x: run(model, features(x), bs=500), d, pools)
    out["float"] = rf
    if not args.no_int8:
        calib = features(calib_audio(d, args.calib, np.random.default_rng(7)))  # as export_ss.py
        q, _ = quantize(model, calib)
        r8, e8 = evaluate("int8", lambda x: run(q, features(x)), d, pools)
        out["int8"] = r8
        cos = np.sum(S.normalize(np.concatenate(list(e8.values()))) * S.normalize(np.concatenate(list(ef.values()))), 1)
        v, crit, gap = verdict(r8, rf)
        out["d_int8_vs_float"] = {
            "a_5way5shot_delta": r8["a_5way5shot"]["acc"] - rf["a_5way5shot"]["acc"],
            "a_10way5shot_delta": r8["a_10way5shot"]["acc"] - rf["a_10way5shot"]["acc"],
            "b_taught_ok_delta": r8["b_rejection"]["taught_ok"] - rf["b_rejection"]["taught_ok"],
            "b_untaught_rej_delta": r8["b_rejection"]["untaught_rej"] - rf["b_rejection"]["untaught_rej"],
            "c_recognised_delta": r8["c_builtins"]["recognised"] - rf["c_builtins"]["recognised"],
            "c_false_accept_delta": r8["c_builtins"]["neg_false_accept"] - rf["c_builtins"]["neg_false_accept"],
            "max_abs_gap": gap,
            "embedding_cosine_int8_vs_float": {"mean": float(cos.mean()), "min": float(cos.min()),
                                               "n": int(len(cos))}}
        out["verdict"] = {"result": v, "criteria": crit}
        print(f"int8 vs float: max gap {gap * 100:.2f} points; embedding cosine mean {cos.mean():.4f} min {cos.min():.4f}")
        print(f"VERDICT (computed): {v}  {crit}")

    info_path = os.path.join(HERE, "out", "ss_export_info.json")
    e = {"params": params, "params_bn_folded": S.count_params_macs(model)[0], "macs": macs}
    if os.path.exists(info_path):
        info = json.load(open(info_path))
        if info.get("epoch") != ck["epoch"] or info.get("untrained"):
            info["WARNING"] = "export info is from a different checkpoint than this evaluation"
        e.update(info)
    out["e_model"] = e
    try:
        out["git_head"] = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=HERE, capture_output=True,
                                         text=True).stdout.strip()
    except OSError:
        pass
    if args.compare:
        out["variants"] = {}
        for item in args.compare:
            name, path = item.split("=", 1)
            out["variants"][name] = summary(json.load(open(path)))
    out["date"] = time.strftime("%Y-%m-%d %H:%M %Z")
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(out, f, indent=1)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    sys.exit(main())
