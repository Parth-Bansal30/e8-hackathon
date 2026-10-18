#!/usr/bin/env python3
"""Turn a raw recorder take (interleaved int16: mic 4, mic 5 at 16 kHz) into a stereo wav.

    tools/raw2wav.py take.raw take.wav [--mono-ch 0|1]
"""
import argparse
import wave

import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("raw")
    ap.add_argument("wav")
    ap.add_argument("--rate", type=int, default=16000)
    ap.add_argument("--mono-ch", type=int, choices=(0, 1), help="write one channel only")
    a = ap.parse_args()
    x = np.fromfile(a.raw, dtype="<i2")
    x = x[: len(x) // 2 * 2].reshape(-1, 2)
    if a.mono_ch is not None:
        x = x[:, a.mono_ch:a.mono_ch + 1]
    with wave.open(a.wav, "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(2)
        w.setframerate(a.rate)
        w.writeframes(np.ascontiguousarray(x).tobytes())
    rms = np.sqrt(np.mean((x / 32768.0) ** 2, axis=0) + 1e-12)
    pk = np.abs(x).max(axis=0) / 32768.0
    print(f"{a.wav}: {len(x) / a.rate:.1f} s, {x.shape[1]} ch, rms "
          + " / ".join(f"{20 * np.log10(r):.1f}" for r in rms) + " dBFS, peak "
          + " / ".join(f"{20 * np.log10(max(p, 1e-6)):.1f}" for p in pk) + " dBFS")


if __name__ == "__main__":
    main()
