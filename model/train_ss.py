#!/usr/bin/env python3
"""Train the Sound Sense encoder (docs/HANDOFF-gates.md G1).

A cosine classifier over 76 classes (40 ESC-50 categories, 35 Speech Commands words, background)
teaches the encoder an embedding space; only the encoder is exported. The 10 held-out ESC-50
classes are never trained on: eval_ss.py uses them to test few-shot teaching.

The defaults are the shipped encoder (G1 fallback 1: 192 channels, stride at block 1, 60 epochs;
out/train_ss_ch192.log). The first spec run was --ch 128 --stride_block 2 --epochs 30
(out/train_ss.log); --specaug/--sc_per_word were tried and were worse on the held-out classes
(out/train_ss_ch192_sa.log, out/g1_metrics.json "variants").

Usage (inside et-venv):
    python train_ss.py --epochs 1 --max_batches 40    # measure time per batch
    nohup python -u train_ss.py > out/train_ss.log 2>&1 &
Writes ss_encoder.pth every epoch; the final model is the last epoch (no selection on held-out
classes; the held-out few-shot number in the log is for monitoring only).
"""
import argparse
import math
import os
import sys
import time

import numpy as np
import torch
import torch.nn.functional as F

import kws_common as K
import ss_common as S

HERE = os.path.dirname(os.path.abspath(__file__))


class Sampler:
    """One epoch of training rows: (kind, index, start) where kind is esc / tap / sc / bg."""

    def __init__(self, d, esc_rep=8, tap_n=128, sc_per_word=400, bg_n=300):
        self.d = d
        self.esc_rep, self.tap_n, self.sc_per_word, self.bg_n = esc_rep, tap_n, sc_per_word, bg_n
        self.esc_ids = np.concatenate([d.esc_ids(c, [1, 2, 3, 4]) for c in d.esc_train_cats])
        self.esc_y = np.array([d.cls_index[("esc", d.esc_cat[i])] for i in self.esc_ids])
        self.tap_y = d.cls_index[("esc", S.BUILTINS["water"])]
        self.sc_by_word = [np.flatnonzero(d.sc_train_y == w) for w in range(len(d.words))]
        self.sc_y0 = d.cls_index[("sc", d.words[0])]
        self.bg_y = d.cls_index[("bg", "background")]

    def epoch(self, rng):
        rows = []
        for _ in range(self.esc_rep):
            rows += [("esc", i, y) for i, y in zip(self.esc_ids, self.esc_y)]
        rows += [("tap", 0, self.tap_y)] * self.tap_n
        for w, ids in enumerate(self.sc_by_word):
            rows += [("sc", i, self.sc_y0 + w) for i in rng.choice(ids, self.sc_per_word, replace=False)]
        rows += [("bg", 0, self.bg_y)] * self.bg_n
        return [rows[k] for k in rng.permutation(len(rows))]

    def batch(self, rows, rng):
        """int16 audio [B, 16000], labels, and which rows are background (not augmented)."""
        d = self.d
        x = np.zeros((len(rows), K.CLIP), dtype=np.float32)
        for k, (kind, i, _) in enumerate(rows):
            if kind == "esc":
                starts = np.flatnonzero(d.esc_valid[i]) * S.ESC_HOP
                s = int(np.clip(rng.choice(starts) + rng.integers(-1600, 1601), 0, S.ESC_LEN - K.CLIP))
                x[k] = d.esc_x[i, s:s + K.CLIP]
            elif kind == "tap":
                x[k] = d.tap_windows("train", 1, rng)[0]
            elif kind == "sc":
                a = np.asarray(d.sc_train_x[i], dtype=np.float32)
                sh = int(rng.integers(-1600, 1601))  # +-100 ms, zero fill
                x[k] = np.roll(a, sh)
                if sh > 0:
                    x[k, :sh] = 0
                elif sh < 0:
                    x[k, sh:] = 0
            else:
                x[k] = d.noise_crops(S.NOISE_FILES, "train", 1, rng)[0] * 32768.0
        y = np.array([r[2] for r in rows])
        bg = np.array([r[0] == "bg" for r in rows])
        return torch.from_numpy(x / 32768.0), torch.from_numpy(y), torch.from_numpy(bg)


def noise_batch(d, n, rng):
    return torch.from_numpy(d.noise_crops(S.NOISE_FILES, "train", n, rng, level=(1.0, 1.0)))


def augment_rows(x, noise, skip, gen):
    """Augment on the device; rows in `skip` (background crops) are left as they are."""
    return torch.where(skip[:, None], x, augment_core(x, noise, gen))


def augment_core(x, noise, gen):
    """Reverb (p=0.5), noise at 0-20 dB SNR (p=0.8), gain 0.3-2, clip to [-1, 1)."""
    B, n, dev = x.shape[0], x.shape[1], x.device
    u = lambda *s: torch.rand(*s, generator=gen).to(dev)
    rms = lambda a: a.pow(2).mean(1, keepdim=True).add(1e-12).sqrt()

    # Synthetic reverb: direct path plus exponentially decaying noise, RT60 0.1-0.6 s (p=0.5).
    L = int(0.6 * K.SR)
    t = torch.arange(L, device=dev, dtype=torch.float32) / K.SR
    rt60 = 0.1 + 0.5 * u(B, 1)
    h = torch.randn(B, L, generator=gen).to(dev) * torch.exp(-6.9 * t / rt60) * 0.3
    h[:, 0] = 1.0
    nfft = 1 << math.ceil(math.log2(n + L))
    wet = torch.fft.irfft(torch.fft.rfft(x, nfft) * torch.fft.rfft(h, nfft), nfft)[:, :n]
    wet = wet * rms(x) / rms(wet)
    x = torch.where(u(B, 1) < 0.5, wet, x)

    # Background noise from the NOISE_FILES train segments at 0-20 dB SNR (p=0.8).
    k = rms(x) / rms(noise) * torch.pow(10.0, -(20.0 * u(B, 1)) / 20.0)
    x = x + torch.where(u(B, 1) < 0.8, k, torch.zeros_like(k)) * noise

    # Gain 0.3-2, then the int16 range.
    x = x * (0.3 + 1.7 * u(B, 1))
    return torch.clamp(x, -1.0, 32767 / 32768.0)


def spec_augment(f, n_t, max_t, n_f, max_f, gen):
    """SpecAugment on [B, 1, 49, 40] features (training only): n_t time masks of up to max_t
    frames and n_f frequency masks of up to max_f bands, set to 0 (the patch mean)."""
    B, _, T, M = f.shape
    keep = torch.ones(B, T, M, dtype=torch.bool)
    for n, size, ax in ((n_t, max_t, 1), (n_f, max_f, 2)):
        L = T if ax == 1 else M
        for _ in range(n):
            w = torch.randint(0, size + 1, (B, 1), generator=gen)
            s = (torch.rand(B, 1, generator=gen) * (L - w + 1)).long()
            pos = torch.arange(L)[None]
            m = (pos >= s) & (pos < s + w)
            keep &= ~(m[:, :, None] if ax == 1 else m[:, None, :])
    return f * keep[:, None].to(f.device, f.dtype)


def val_set(d, rng):
    """Cheap validation: ESC-50 fold 5 loudest windows of the trained classes, Speech Commands
    test, 100 background test crops."""
    ids = np.concatenate([d.esc_ids(c, [5]) for c in d.esc_train_cats])
    xe = np.stack([d.esc_window(i) for i in ids]).astype(np.float32) / 32768.0
    ye = np.array([d.cls_index[("esc", d.esc_cat[i])] for i in ids])
    xs = np.asarray(d.sc_test_x, dtype=np.float32) / 32768.0
    ys = d.cls_index[("sc", d.words[0])] + d.sc_test_y
    xb = d.noise_crops(S.NOISE_FILES, "test", 100, rng)
    yb = np.full(100, d.cls_index[("bg", "background")])
    return [("esc", xe, ye), ("sc", xs, ys), ("bg", xb, yb)]


def heldout_set(d):
    ids = np.concatenate([d.esc_ids(c, [1, 2, 3, 4, 5]) for c in S.HELDOUT])
    x = np.stack([d.esc_window(i) for i in ids]).astype(np.float32) / 32768.0
    y = np.array([S.HELDOUT.index(d.esc_cat[i]) for i in ids])
    return x, y


def embed(model, feats, x, dev, bs=512):
    out = []
    with torch.no_grad():
        for i in range(0, len(x), bs):
            out.append(model(feats(torch.from_numpy(x[i:i + bs]).to(dev))).cpu())
    return torch.cat(out).numpy()


def few_shot(emb, y, rng, episodes=200, ways=5, shots=5, queries=15):
    z = S.normalize(emb)
    by = [np.flatnonzero(y == c) for c in range(y.max() + 1)]
    ok = n = 0
    for _ in range(episodes):
        cls = rng.choice(len(by), ways, replace=False)
        protos, q, ql = [], [], []
        for j, c in enumerate(cls):
            p = rng.permutation(by[c])
            protos.append(S.normalize(z[p[:shots]].mean(0)))
            q.append(z[p[shots:shots + queries]])
            ql += [j] * queries
        pred = (np.concatenate(q) @ np.stack(protos).T).argmax(1)
        ok += (pred == np.array(ql)).sum()
        n += len(ql)
    return ok / n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--batch", type=int, default=128)
    ap.add_argument("--lr", type=float, default=3e-3)
    ap.add_argument("--wd", type=float, default=1e-4)
    ap.add_argument("--ch", type=int, default=192)
    ap.add_argument("--blocks", type=int, default=5)
    ap.add_argument("--stride_block", type=int, default=1)
    ap.add_argument("--scale", type=float, default=16.0)
    ap.add_argument("--esc_rep", type=int, default=8, help="ESC-50 oversampling per epoch")
    ap.add_argument("--sc_per_word", type=int, default=400, help="Speech Commands clips per word per epoch")
    ap.add_argument("--specaug", type=int, nargs=4, default=[0, 0, 0, 0], metavar=("NT", "MAXT", "NF", "MAXF"),
                    help="SpecAugment: time masks, max frames, frequency masks, max bands (0 0 0 0 = off)")
    ap.add_argument("--max_batches", type=int, default=0, help="stop each epoch early (timing tests)")
    ap.add_argument("--out", default=os.path.join(HERE, "ss_encoder.pth"))
    args = ap.parse_args()

    torch.manual_seed(0)
    rng = np.random.default_rng(0)
    gen = torch.Generator().manual_seed(0)
    dev = "mps" if torch.backends.mps.is_available() else "cpu"
    d = S.Data()
    sampler = Sampler(d, esc_rep=args.esc_rep, sc_per_word=args.sc_per_word)
    feats = K.Features().to(dev)
    cfg = dict(ch=args.ch, blocks=args.blocks, stride_block=args.stride_block)
    model = S.SSEncoder(**cfg).to(dev)
    head = S.CosineHead(len(d.classes), scale=args.scale).to(dev)
    params, macs = S.count_params_macs(S.SSEncoder(**cfg))
    print(f"encoder cfg {model.cfg}: {params} parameters, {macs / 1e6:.2f} M MACs; "
          f"{len(d.classes)} classes; device {dev}", flush=True)

    val = val_set(d, np.random.default_rng(1))
    hx, hy = heldout_set(d)
    n_rows = len(sampler.epoch(np.random.default_rng(0)))
    per_epoch = n_rows // args.batch
    if args.max_batches:
        per_epoch = min(per_epoch, args.max_batches)
    steps = args.epochs * per_epoch
    warm = min(300, steps // 10)
    opt = torch.optim.AdamW([{"params": model.parameters(), "weight_decay": args.wd},
                             {"params": head.parameters(), "weight_decay": 0.0}], lr=args.lr)
    sched = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda s: (s + 1) / warm if s < warm else 0.5 * (1 + math.cos(math.pi * (s - warm) / max(1, steps - warm))))
    print(f"{n_rows} rows per epoch, {per_epoch} batches of {args.batch}, {steps} steps", flush=True)

    for ep in range(args.epochs):
        t0 = time.time()
        model.train()
        head.train()
        rows = sampler.epoch(rng)
        tot = cnt = 0
        for b in range(per_epoch):
            x, y, bg = sampler.batch(rows[b * args.batch:(b + 1) * args.batch], rng)
            x, y, bg = x.to(dev), y.to(dev), bg.to(dev)
            nz = noise_batch(d, len(x), rng).to(dev)
            with torch.no_grad():
                x = augment_rows(x, nz, bg, gen)
                f = feats(x)
                if any(args.specaug):
                    f = spec_augment(f, *args.specaug, gen)
            loss = F.cross_entropy(head(model(f)), y)
            opt.zero_grad()
            loss.backward()
            opt.step()
            sched.step()
            tot += loss.item()
            cnt += 1
        t_train = time.time() - t0

        model.eval()
        head.eval()
        accs = []
        for name, vx, vy in val:
            with torch.no_grad():
                p = head(torch.from_numpy(embed(model, feats, vx, dev)).to(dev)).argmax(1).cpu().numpy()
            accs.append(f"{name} {np.mean(p == vy):.3f}")
        fs = few_shot(embed(model, feats, hx, dev), hy, np.random.default_rng(2))
        ck = {"encoder": {k: v.cpu() for k, v in model.state_dict().items()},
              "head": {k: v.cpu() for k, v in head.state_dict().items()},
              "cfg": model.cfg, "classes": [(k, str(c)) for k, c in d.classes], "epoch": ep + 1, "args": vars(args)}
        torch.save(ck, args.out)
        print(f"epoch {ep + 1}/{args.epochs}: loss {tot / cnt:.3f}  val acc {'  '.join(accs)}  "
              f"heldout 5w5s (monitor only) {fs:.3f}  lr {sched.get_last_lr()[0]:.2e}  "
              f"({t_train:.0f} s train, {time.time() - t0:.0f} s total)", flush=True)
    print(f"saved {args.out}")


if __name__ == "__main__":
    sys.exit(main())
