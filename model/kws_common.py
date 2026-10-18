"""Keyword spotting: shared feature extraction, model and dataset code.

The features are deliberately simple so the board computes exactly the same thing in C
(firmware/app/kws_features.c):
  1 s of 16 kHz audio, scaled to [-1, 1)
  -> 49 frames of 512 samples, hop 320 (32 ms window, 20 ms step), periodic Hann window
  -> 512-point real FFT, power spectrum (257 bins)
  -> 40 triangular mel bands (HTK mel scale, 20..7600 Hz)
  -> natural log(x + 1e-6)
  -> subtract the mean of the whole 49x40 patch (makes it independent of mic gain)
  -> divide by FEAT_STD
The model input is [1, 1, 49, 40].
"""
import math
import os
import wave

import numpy as np
import torch
import torch.nn as nn

SR = 16000
CLIP = 16000
WIN = 512
HOP = 320
FRAMES = 1 + (CLIP - WIN) // HOP  # 49
NMEL = 40
FMIN, FMAX = 20.0, 7600.0
LOG_EPS = 1e-6
FEAT_STD = 4.0  # fixed scale so typical features land in about [-3, 3]

KEYWORDS = ["yes", "no", "up", "down", "left", "right", "on", "off", "stop", "go"]
LABELS = ["_silence_", "_unknown_"] + KEYWORDS


def hann():
    n = np.arange(WIN)
    return (0.5 - 0.5 * np.cos(2 * np.pi * n / WIN)).astype(np.float32)


def mel_bands():
    """Per band: first FFT bin and its weights. The same table goes into the C header."""
    def hz2mel(f):
        return 2595.0 * math.log10(1.0 + f / 700.0)

    def mel2hz(m):
        return 700.0 * (10 ** (m / 2595.0) - 1.0)

    edges = [mel2hz(hz2mel(FMIN) + i * (hz2mel(FMAX) - hz2mel(FMIN)) / (NMEL + 1)) for i in range(NMEL + 2)]
    bin_hz = SR / WIN
    bands = []
    for b in range(NMEL):
        lo, mid, hi = edges[b], edges[b + 1], edges[b + 2]
        first, w = None, []
        for k in range(WIN // 2 + 1):
            f = k * bin_hz
            v = (f - lo) / (mid - lo) if f <= mid else (hi - f) / (hi - mid)
            if v > 0:
                if first is None:
                    first = k
                w.append(v)
            elif first is not None:
                break
        bands.append((first, np.array(w, dtype=np.float32)))
    return bands


def mel_matrix():
    m = np.zeros((WIN // 2 + 1, NMEL), dtype=np.float32)
    for b, (first, w) in enumerate(mel_bands()):
        m[first:first + len(w), b] = w
    return m


class Features(nn.Module):
    """Batched torch version of the board's feature pipeline: [B, 16000] -> [B, 1, 49, 40]."""

    def __init__(self):
        super().__init__()
        self.register_buffer("win", torch.from_numpy(hann()))
        self.register_buffer("mel", torch.from_numpy(mel_matrix()))

    def forward(self, audio):
        frames = audio.unfold(1, WIN, HOP) * self.win            # [B, 49, 512]
        spec = torch.fft.rfft(frames, n=WIN)
        power = spec.real ** 2 + spec.imag ** 2                  # [B, 49, 257]
        logmel = torch.log(power @ self.mel + LOG_EPS)           # [B, 49, 40]
        logmel = logmel - logmel.mean(dim=(1, 2), keepdim=True)
        return (logmel / FEAT_STD).unsqueeze(1)


class DSCNN(nn.Module):
    """DS-CNN-S from 'Hello Edge' (Zhang et al. 2017), 64 channels. BatchNorm is folded before export."""

    def __init__(self, n_classes=len(LABELS), ch=64):
        super().__init__()
        layers = [nn.Conv2d(1, ch, (10, 4), stride=(2, 2), padding=(5, 1)), nn.BatchNorm2d(ch), nn.ReLU()]
        for _ in range(4):
            layers += [nn.Conv2d(ch, ch, 3, padding=1, groups=ch), nn.BatchNorm2d(ch), nn.ReLU(),
                       nn.Conv2d(ch, ch, 1), nn.BatchNorm2d(ch), nn.ReLU()]
        self.body = nn.Sequential(*layers)
        self.pool = nn.AvgPool2d((25, 20))  # the feature map is 25x20 after the first conv
        self.fc = nn.Linear(ch, n_classes)

    def forward(self, x):
        x = self.pool(self.body(x))
        return self.fc(torch.flatten(x, 1))


def fold_batchnorm(model):
    """Return an eval-mode copy with every Conv2d+BatchNorm2d pair folded into the conv."""
    from torch.nn.utils.fusion import fuse_conv_bn_eval
    import copy
    m = copy.deepcopy(model).eval()
    out = []
    mods = list(m.body)
    i = 0
    while i < len(mods):
        if isinstance(mods[i], nn.Conv2d) and i + 1 < len(mods) and isinstance(mods[i + 1], nn.BatchNorm2d):
            out.append(fuse_conv_bn_eval(mods[i], mods[i + 1]))
            i += 2
        else:
            out.append(mods[i])
            i += 1
    m.body = nn.Sequential(*out)
    return m


def read_wav(path):
    with wave.open(path, "rb") as w:
        assert w.getframerate() == SR and w.getsampwidth() == 2 and w.getnchannels() == 1, path
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    if len(x) < CLIP:
        x = np.pad(x, (0, CLIP - len(x)))
    return x[:CLIP]


def build_cache(data_dir, cache_dir, unknown_frac=0.1, silence_frac=0.1, seed=0):
    """Read the dataset once into int16 arrays per split (train/val/test), plus the noise clips."""
    os.makedirs(cache_dir, exist_ok=True)
    rng = np.random.default_rng(seed)

    def listfile(name):
        with open(os.path.join(data_dir, name)) as f:
            return set(l.strip() for l in f if l.strip())

    val, test = listfile("validation_list.txt"), listfile("testing_list.txt")
    words = sorted(d for d in os.listdir(data_dir)
                   if os.path.isdir(os.path.join(data_dir, d)) and not d.startswith("_"))
    split_files = {"train": [], "val": [], "test": []}
    for w in words:
        for fn in sorted(os.listdir(os.path.join(data_dir, w))):
            if not fn.endswith(".wav"):
                continue
            rel = f"{w}/{fn}"
            s = "val" if rel in val else "test" if rel in test else "train"
            split_files[s].append(rel)

    noise = []
    nd = os.path.join(data_dir, "_background_noise_")
    for fn in sorted(os.listdir(nd)):
        if fn.endswith(".wav"):
            with wave.open(os.path.join(nd, fn), "rb") as w:
                noise.append(np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16))
    np.save(os.path.join(cache_dir, "noise.npy"), np.concatenate(noise))

    for split, files in split_files.items():
        known = [f for f in files if f.split("/")[0] in KEYWORDS]
        unknown = [f for f in files if f.split("/")[0] not in KEYWORDS]
        n_unk = int(len(known) * unknown_frac / (1 - unknown_frac - silence_frac))
        n_sil = int(len(known) * silence_frac / (1 - unknown_frac - silence_frac))
        unknown = list(rng.choice(unknown, size=min(n_unk, len(unknown)), replace=False))
        chosen = known + unknown
        x = np.zeros((len(chosen) + n_sil, CLIP), dtype=np.int16)
        y = np.zeros(len(chosen) + n_sil, dtype=np.int64)
        for i, rel in enumerate(chosen):
            x[i] = read_wav(os.path.join(data_dir, rel))
            word = rel.split("/")[0]
            y[i] = LABELS.index(word) if word in KEYWORDS else 1
        # Silence rows stay zero here; training and evaluation fill them with background noise.
        y[len(chosen):] = 0
        np.save(os.path.join(cache_dir, f"{split}_x.npy"), x)
        np.save(os.path.join(cache_dir, f"{split}_y.npy"), y)
        print(f"{split}: {len(known)} keyword, {len(unknown)} unknown, {n_sil} silence clips", flush=True)


def silence_clips(noise, n, rng):
    """n 1-second background-noise crops at random levels (some near-silent)."""
    out = np.zeros((n, CLIP), dtype=np.float32)
    for i in range(n):
        s = rng.integers(0, len(noise) - CLIP)
        out[i] = noise[s:s + CLIP] / 32768.0 * rng.uniform(0.0, 1.0)
    return out
