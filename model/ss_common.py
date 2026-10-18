"""Sound Sense: shared encoder, data and matching code.

The encoder turns 1 s of 16 kHz audio into a 64-d embedding. Its input is exactly the keyword
model's 49x40 log-mel patch (kws_common.Features), so the board reuses firmware/app/kws.c
unchanged.

Sound profiles (built-in and taught) are the normalised mean of normalised embeddings, each with
its own threshold. A sound matches the profile with the highest cosine similarity if it clears
that profile's threshold; otherwise it is "unknown". firmware/soundsense/ss_match.c implements
the same rules on the board.

Data (see docs/HANDOFF-gates.md G1):
  ESC-50 (model/esc50/), resampled 44.1 -> 16 kHz, 5 s clips
  Speech Commands v0.02 (model/speech_commands/): all 35 words, plus _background_noise_
Both are read once into model/ss_cache/ by build_cache().
"""
import csv
import os
import wave

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

import kws_common as K

HERE = os.path.dirname(os.path.abspath(__file__))
ESC_DIR = os.path.join(HERE, "esc50", "ESC-50-master")
SC_DIR = os.path.join(HERE, "speech_commands")
CACHE = os.path.join(HERE, "ss_cache")

SR, CLIP = K.SR, K.CLIP
ESC_SR, ESC_LEN = 44100, 5 * 16000
EMB = 64

# Built-in sounds: product name -> ESC-50 category. "water" also uses crops of running_tap.wav.
BUILTINS = {
    "knock": "door_wood_knock",
    "dog": "dog",
    "baby": "crying_baby",
    "alarm": "clock_alarm",
    "glass": "glass_breaking",
    "siren": "siren",
    "water": "pouring_water",
}
# Never trained on: stand-ins for the sounds users will teach.
HELDOUT = ["church_bells", "clock_tick", "can_opening", "mouse_click", "keyboard_typing",
           "door_wood_creaks", "vacuum_cleaner", "washing_machine", "footsteps", "sneezing"]
TAP = "running_tap"
NOISE_FILES = ["doing_the_dishes", "dude_miaowing", "exercise_bike", "pink_noise", "white_noise"]
NOISE_TRAIN_FRAC = 0.8   # the last 20% of each noise file (and of running_tap) is kept for testing


# ---------------------------------------------------------------- model

class SSEncoder(nn.Module):
    """DS-CNN encoder: [B, 1, 49, 40] log-mel -> [B, 64] embedding (no activation on the output).

    Like kws_common.DSCNN, wider, with the depthwise conv of one block at stride 2 so the later
    blocks see a wider context for less compute. BatchNorm is folded before export
    (kws_common.fold_batchnorm handles the body)."""

    def __init__(self, ch=128, blocks=5, stride_block=2, emb=EMB):
        super().__init__()
        layers = [nn.Conv2d(1, ch, (10, 4), stride=(2, 2), padding=(5, 1)), nn.BatchNorm2d(ch), nn.ReLU()]
        for i in range(blocks):
            s = 2 if i == stride_block else 1
            layers += [nn.Conv2d(ch, ch, 3, stride=s, padding=1, groups=ch), nn.BatchNorm2d(ch), nn.ReLU(),
                       nn.Conv2d(ch, ch, 1), nn.BatchNorm2d(ch), nn.ReLU()]
        self.body = nn.Sequential(*layers)
        with torch.no_grad():
            h, w = self.body(torch.zeros(1, 1, K.FRAMES, K.NMEL)).shape[2:]
        self.pool = nn.AvgPool2d((h, w))
        self.fc = nn.Linear(ch, emb)
        self.cfg = dict(ch=ch, blocks=blocks, stride_block=stride_block, emb=emb)

    def forward(self, x):
        return self.fc(torch.flatten(self.pool(self.body(x)), 1))


class CosineHead(nn.Module):
    """Training-only classifier: scaled cosine between the normalised embedding and class weights."""

    def __init__(self, n_classes, emb=EMB, scale=16.0):
        super().__init__()
        self.w = nn.Parameter(torch.randn(n_classes, emb) * 0.05)
        self.scale = scale

    def forward(self, e):
        return self.scale * F.normalize(e, dim=1) @ F.normalize(self.w, dim=1).t()


def count_params_macs(model):
    """Parameters and multiply-accumulates per call, from the conv and linear layers."""
    macs = [0]

    def hook(m, inp, out):
        if isinstance(m, nn.Conv2d):
            k = m.kernel_size[0] * m.kernel_size[1] * m.in_channels // m.groups
            macs[0] += out.numel() * k
        elif isinstance(m, nn.Linear):
            macs[0] += m.in_features * m.out_features
    hs = [m.register_forward_hook(hook) for m in model.modules() if isinstance(m, (nn.Conv2d, nn.Linear))]
    with torch.no_grad():
        model.eval()(torch.zeros(1, 1, K.FRAMES, K.NMEL))
    for h in hs:
        h.remove()
    return sum(p.numel() for p in model.parameters()), macs[0]


def load_encoder(path, map_location="cpu"):
    ck = torch.load(path, map_location=map_location)
    m = SSEncoder(**ck["cfg"])
    m.load_state_dict(ck["encoder"])
    return m.eval(), ck


# ---------------------------------------------------------------- matching (mirrored in ss_match.c)

THR_MARGIN, THR_LO, THR_HI = 0.05, 0.5, 0.85


def normalize(e):
    e = np.asarray(e, dtype=np.float32)
    return e / np.maximum(np.linalg.norm(e, axis=-1, keepdims=True), 1e-9)


def make_profile(embs, margin=THR_MARGIN, lo=THR_LO, hi=THR_HI):
    """Profile = normalised mean of the normalised examples. Threshold: each example's similarity
    to the mean of the others; the lowest of those minus the margin, clamped to [lo, hi]."""
    z = normalize(embs)
    total = z.sum(0)
    if len(z) >= 2:
        loo = [float(normalize(total - z[i]) @ z[i]) for i in range(len(z))]
        thr = float(np.clip(min(loo) - margin, lo, hi))
    else:
        thr = lo
    return normalize(total), thr


def match(embs, protos, thrs):
    """Best profile per row and its score; -1 where the best doesn't clear its threshold."""
    s = normalize(embs) @ np.asarray(protos).T
    best = s.argmax(1)
    score = s[np.arange(len(s)), best]
    return np.where(score >= np.asarray(thrs)[best], best, -1), score, s


# ---------------------------------------------------------------- data

def _read_wav(path):
    with wave.open(path, "rb") as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 1, path
        return w.getframerate(), np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def resample_44k_16k(x):
    """Band-limited FFT resampling of a whole clip, 44.1 -> 16 kHz (scipy's signal.resample).
    The clip is treated as periodic; the edges are padded zeros in ESC-50, so that's harmless."""
    n_out = int(round(len(x) * SR / ESC_SR))
    X = np.fft.rfft(x.astype(np.float64))
    y = np.fft.irfft(X[:n_out // 2 + 1], n=n_out) * (n_out / len(x))
    return np.clip(np.round(y), -32768, 32767).astype(np.int16)


def window_rms(x, hop):
    """RMS of every 1 s window starting at multiples of hop."""
    c = np.concatenate([[0.0], np.cumsum(x.astype(np.float64) ** 2)])
    starts = np.arange(0, len(x) - CLIP + 1, hop)
    return starts, np.sqrt((c[starts + CLIP] - c[starts]) / CLIP)


ESC_HOP = 160      # 10 ms grid for window starts
ESC_NSTART = (ESC_LEN - CLIP) // ESC_HOP + 1


def build_cache(cache=CACHE, sc_train_per_word=800, sc_test_per_word=60, seed=0):
    os.makedirs(cache, exist_ok=True)
    rng = np.random.default_rng(seed)

    # ESC-50
    rows = list(csv.DictReader(open(os.path.join(ESC_DIR, "meta", "esc50.csv"))))
    x = np.zeros((len(rows), ESC_LEN), dtype=np.int16)
    valid = np.zeros((len(rows), ESC_NSTART), dtype=bool)
    loud = np.zeros(len(rows), dtype=np.int64)
    for i, r in enumerate(rows):
        sr, a = _read_wav(os.path.join(ESC_DIR, "audio", r["filename"]))
        assert sr == ESC_SR
        y = resample_44k_16k(a)[:ESC_LEN]
        x[i, :len(y)] = y
        _, rms = window_rms(x[i], ESC_HOP)
        valid[i] = rms >= 0.5 * rms.max()
        loud[i] = rms.argmax() * ESC_HOP
    np.save(os.path.join(cache, "esc_x.npy"), x)
    np.save(os.path.join(cache, "esc_valid.npy"), valid)
    np.save(os.path.join(cache, "esc_loud.npy"), loud)
    np.save(os.path.join(cache, "esc_fold.npy"), np.array([int(r["fold"]) for r in rows]))
    with open(os.path.join(cache, "esc_cat.txt"), "w") as f:
        f.write("\n".join(r["category"] for r in rows) + "\n")
    print(f"ESC-50: {len(rows)} clips", flush=True)

    # Background noise: train / test segments per file; running_tap kept apart for "water".
    nd = os.path.join(SC_DIR, "_background_noise_")
    for name in NOISE_FILES + [TAP]:
        sr, a = _read_wav(os.path.join(nd, name + ".wav"))
        assert sr == SR
        cut = int(len(a) * NOISE_TRAIN_FRAC)
        np.save(os.path.join(cache, f"noise_{name}_train.npy"), a[:cut])
        np.save(os.path.join(cache, f"noise_{name}_test.npy"), a[cut:])

    # Speech Commands: all words, a random subset per word and split.
    def listfile(n):
        return set(l.strip() for l in open(os.path.join(SC_DIR, n)) if l.strip())
    val, test = listfile("validation_list.txt"), listfile("testing_list.txt")
    words = sc_words()
    for split, per in (("train", sc_train_per_word), ("test", sc_test_per_word)):
        files, labels = [], []
        for wi, w in enumerate(words):
            fs = sorted(f"{w}/{fn}" for fn in os.listdir(os.path.join(SC_DIR, w)) if fn.endswith(".wav"))
            fs = [f for f in fs if (f in test) == (split == "test") and f not in val]
            fs = list(rng.choice(fs, size=min(per, len(fs)), replace=False))
            files += fs
            labels += [wi] * len(fs)
        xs = np.zeros((len(files), CLIP), dtype=np.int16)
        for i, f in enumerate(files):
            xs[i] = K.read_wav(os.path.join(SC_DIR, f))
        np.save(os.path.join(cache, f"sc_{split}_x.npy"), xs)
        np.save(os.path.join(cache, f"sc_{split}_y.npy"), np.array(labels))
        print(f"Speech Commands {split}: {len(files)} clips, {len(words)} words", flush=True)


def sc_words():
    return sorted(d for d in os.listdir(SC_DIR) if os.path.isdir(os.path.join(SC_DIR, d)) and not d.startswith("_"))


class Data:
    """The cached datasets plus the class lists the encoder is trained on."""

    def __init__(self, cache=CACHE):
        if not os.path.exists(os.path.join(cache, "sc_test_y.npy")):
            build_cache(cache)
        ld = lambda n: np.load(os.path.join(cache, n), mmap_mode="r")
        self.esc_x = ld("esc_x.npy")
        self.esc_valid = np.load(os.path.join(cache, "esc_valid.npy"))
        self.esc_loud = np.load(os.path.join(cache, "esc_loud.npy"))
        self.esc_fold = np.load(os.path.join(cache, "esc_fold.npy"))
        self.esc_cat = np.array(open(os.path.join(cache, "esc_cat.txt")).read().split())
        self.sc_train_x, self.sc_train_y = ld("sc_train_x.npy"), np.load(os.path.join(cache, "sc_train_y.npy"))
        self.sc_test_x, self.sc_test_y = ld("sc_test_x.npy"), np.load(os.path.join(cache, "sc_test_y.npy"))
        self.noise = {f"{n}_{s}": np.load(os.path.join(cache, f"noise_{n}_{s}.npy"))
                      for n in NOISE_FILES + [TAP] for s in ("train", "test")}
        self.words = sc_words()

        cats = sorted(set(self.esc_cat))
        self.esc_train_cats = [c for c in cats if c not in HELDOUT]           # 40, incl. pouring_water
        self.classes = ([("esc", c) for c in self.esc_train_cats] + [("sc", w) for w in self.words]
                        + [("bg", "background")])
        self.cls_index = {c: i for i, c in enumerate(self.classes)}

    def esc_ids(self, cat, folds):
        return np.flatnonzero((self.esc_cat == cat) & np.isin(self.esc_fold, folds))

    def esc_window(self, i, start=None):
        s = self.esc_loud[i] if start is None else start
        return np.asarray(self.esc_x[i, s:s + CLIP])

    def noise_crops(self, names, split, n, rng, level=(0.0, 1.0)):
        """n 1 s crops from the given noise files' train or test segment, at random levels."""
        out = np.zeros((n, CLIP), dtype=np.float32)
        for k in range(n):
            a = self.noise[f"{names[rng.integers(len(names))]}_{split}"]
            s = rng.integers(0, len(a) - CLIP)
            out[k] = a[s:s + CLIP] / 32768.0 * rng.uniform(*level)
        return out

    def tap_windows(self, split, n=None, rng=None):
        """running_tap: random crops (train), or consecutive 1 s windows (test)."""
        a = self.noise[f"{TAP}_{split}"]
        if rng is None:
            k = len(a) // CLIP if n is None else min(n, len(a) // CLIP)
            return np.stack([a[i * CLIP:(i + 1) * CLIP] for i in range(k)])
        return np.stack([a[s:s + CLIP] for s in rng.integers(0, len(a) - CLIP, size=n)])


def to_float(x):
    return np.asarray(x, dtype=np.float32) / 32768.0
