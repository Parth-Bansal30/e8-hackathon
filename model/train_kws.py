#!/usr/bin/env python3
"""Train the DS-CNN keyword spotter on Google Speech Commands v0.02 (12 classes).

Usage (inside et-venv):
    python train_kws.py                    # builds the int16 cache on first run, then trains
    python train_kws.py --epochs 1         # quick smoke test
Writes kws_model.pth next to this script.
"""
import argparse
import os
import sys
import time

import numpy as np
import torch
import torch.nn as nn

from kws_common import CLIP, LABELS, DSCNN, Features, build_cache, silence_clips

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "speech_commands")
CACHE = os.path.join(HERE, "kws_cache")


def load(split):
    return (np.load(os.path.join(CACHE, f"{split}_x.npy"), mmap_mode="r"),
            np.load(os.path.join(CACHE, f"{split}_y.npy")))


def make_batch(x, y, idx, noise, rng, augment):
    audio = x[np.sort(idx)].astype(np.float32) / 32768.0
    labels = y[np.sort(idx)]
    sil = labels == 0
    audio[sil] = silence_clips(noise, int(sil.sum()), rng)
    if augment:
        for i in np.flatnonzero(~sil):
            shift = rng.integers(-1600, 1601)  # +-100 ms
            audio[i] = np.roll(audio[i], shift)
            if shift > 0:
                audio[i, :shift] = 0
            elif shift < 0:
                audio[i, shift:] = 0
            if rng.random() < 0.8:
                s = rng.integers(0, len(noise) - CLIP)
                audio[i] += noise[s:s + CLIP] / 32768.0 * rng.uniform(0, 0.1)
            audio[i] *= rng.uniform(0.3, 2.0)
        np.clip(audio, -1.0, 32767 / 32768.0, out=audio)
    return torch.from_numpy(audio), torch.from_numpy(labels)


def evaluate(model, feats, x, y, noise, dev, seed=123):
    rng = np.random.default_rng(seed)  # same noise for silence rows every time
    model.eval()
    correct, conf = 0, np.zeros((len(LABELS), len(LABELS)), dtype=np.int64)
    with torch.no_grad():
        for i in range(0, len(y), 512):
            a, l = make_batch(x, y, np.arange(i, min(i + 512, len(y))), noise, rng, False)
            p = model(feats(a.to(dev))).argmax(1).cpu()
            correct += (p == l).sum().item()
            np.add.at(conf, (l.numpy(), p.numpy()), 1)
    return correct / len(y), conf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--epochs", type=int, default=25)
    ap.add_argument("--batch", type=int, default=128)
    ap.add_argument("--lr", type=float, default=3e-3)
    ap.add_argument("--out", default=os.path.join(HERE, "kws_model.pth"))
    args = ap.parse_args()

    if not os.path.exists(os.path.join(CACHE, "test_y.npy")):
        t = time.time()
        build_cache(DATA, CACHE)
        print(f"cache built in {time.time() - t:.0f} s", flush=True)

    torch.manual_seed(0)
    rng = np.random.default_rng(0)
    dev = "mps" if torch.backends.mps.is_available() else "cpu"
    noise = np.load(os.path.join(CACHE, "noise.npy"))
    xtr, ytr = load("train")
    xva, yva = load("val")
    xte, yte = load("test")
    feats = Features().to(dev)
    model = DSCNN().to(dev)
    print(f"{sum(p.numel() for p in model.parameters())} parameters, device {dev}", flush=True)

    steps = args.epochs * (len(ytr) // args.batch)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=args.lr, total_steps=steps)
    best = 0.0
    for ep in range(args.epochs):
        t = time.time()
        model.train()
        perm = rng.permutation(len(ytr))
        for i in range(len(ytr) // args.batch):
            a, l = make_batch(xtr, ytr, perm[i * args.batch:(i + 1) * args.batch], noise, rng, True)
            loss = nn.functional.cross_entropy(model(feats(a.to(dev))), l.to(dev), label_smoothing=0.05)
            opt.zero_grad()
            loss.backward()
            opt.step()
            sched.step()
        acc, _ = evaluate(model, feats, xva, yva, noise, dev)
        print(f"epoch {ep + 1}/{args.epochs}: loss {loss.item():.3f}  val acc {acc:.4f}  ({time.time() - t:.0f} s)",
              flush=True)
        if acc > best:
            best = acc
            torch.save(model.state_dict(), args.out)

    model.load_state_dict(torch.load(args.out, map_location=dev))
    acc, conf = evaluate(model, feats, xte, yte, noise, dev)
    print(f"best val acc {best:.4f}; test acc {acc:.4f}")
    print("confusion (rows = truth):", " ".join(f"{l[:4]:>5}" for l in LABELS))
    for l, row in zip(LABELS, conf):
        print(f"{l:>10} " + " ".join(f"{v:5d}" for v in row))


if __name__ == "__main__":
    sys.exit(main())
