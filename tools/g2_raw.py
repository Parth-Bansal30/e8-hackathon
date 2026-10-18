#!/usr/bin/env python3
"""Raw accelerometer data from the knock bench (firmware/imu_bench) for offline G2 analysis.

dump:   copies the bench's 40 s accelerometer ring (bench_imu) over J-Link together with a
        (ms since boot, DWT cycle) reference, and writes <out>.npz with every frame's time in ms
        since boot, the same clock as the "t=" of the bench's event lines.
watch:  follows the bench log and dumps the ring each time a new trial starts, so every set
        is captured before the ring wraps (sets must be under about 30 s).
gated:  the fusion question asked directly: for each heard onset in a set (the log's HEARD and
        FELT+HEARD lines), is there vibration within +-W ms? The high-passed magnitude is
        compared with K x its own noise, measured in the 0.5 s before the onset. Reports the felt %
        and audio-minus-vibration times for K = 2..6.

    tools/g2_raw.py dump docs/gates/g2_raw/50cm
    tools/g2_raw.py watch docs/gates/g2_rtt.log docs/gates/g2_raw
    tools/g2_raw.py gated docs/gates/g2_rtt.log docs/gates/g2_raw/set2.npz --trial 2
"""
import os
import re
import subprocess
import sys
import tempfile
import time

import numpy as np

T = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(T)
sys.path.insert(0, T)
import jlink   # noqa: E402

ELF = os.path.join(ROOT, "firmware/out/imu_bench/E8-HP/debug/imu_bench.elf")
N = 40 * 800
REC = np.dtype([("x", "<i2"), ("y", "<i2"), ("z", "<i2"), ("t", "<u2"), ("cyc", "<u4")])
DWT_CYCCNT = 0xE0001004
HZ = 400e6
LSB_PER_G = 8192.0


def dump(out):
    b = jlink.Board(ELF)
    a_ms, a_n, a_ring = b.addr("g_ms"), b.addr("bench_imu_n"), b.addr("bench_imu")
    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, "imu.bin")
        txt = b.run([f"mem32 0x{a_ms:08X} 1", f"mem32 0x{DWT_CYCCNT:08X} 1", f"mem32 0x{a_n:08X} 1",
                     f"savebin {raw} 0x{a_ring:08X} 0x{N * REC.itemsize:X}", f"mem32 0x{a_n:08X} 1"])
        vals = [int(m.group(1), 16) for m in re.finditer(r"^[0-9A-Fa-f]{8} = ([0-9A-Fa-f]{8})", txt, re.M)]
        ref_ms, ref_cyc, n0, n1 = vals[:4]
        ring = np.fromfile(raw, dtype=REC)
    # frames n1-N+1 .. n1-1 are in the ring; those written during the dump (n0..n1) may be torn,
    # and the oldest (n1 - N .. n1 - N + (n1 - n0)) were overwritten: keep n1 - N + (n1 - n0) .. n0 - 1
    first = n1 - N + (n1 - n0) + 1
    idx = np.arange(first, n0)
    fr = ring[idx % N]
    cyc = fr["cyc"].astype(np.int64)
    d = (np.diff(cyc) + (1 << 31)) % (1 << 32) - (1 << 31)   # unwrap the 32-bit counter (signed steps)
    cyc_u = np.concatenate([[0], np.cumsum(d)]) + 0
    back = (ref_cyc - fr["cyc"][-1]) % (1 << 32)      # newest kept frame is 'back' cycles before ref
    ms = ref_ms - (back + (cyc_u[-1] - cyc_u)) / HZ * 1000.0
    acc = np.stack([fr["x"], fr["y"], fr["z"]], 1) / LSB_PER_G * 1000.0   # mg
    step = d / HZ * 1000.0
    print(f"frame-time steps: median {np.median(step):.3f} ms, backward {np.mean(step < 0) * 100:.2f}% "
          f"(min {step.min():+.1f} ms), over 3 ms {np.mean(step > 3) * 100:.2f}% (max {step.max():+.1f} ms)")
    np.savez(out, ms=ms, acc=acc, sensor_t=fr["t"], n0=n0, n1=n1)
    print(f"{out}.npz: {len(idx)} frames, {ms[0] / 1000:.1f}..{ms[-1] / 1000:.1f} s since boot "
          f"({(ms[-1] - ms[0]) / 1000:.1f} s, {len(idx) / ((ms[-1] - ms[0]) / 1000):.1f} Hz)")


def watch(log, outdir):
    """Dump the ring at every new 'trial N is:' line (the set that just ended), named by order."""
    os.makedirs(outdir, exist_ok=True)
    pat = re.compile(r"\[G2\] trial (\d+) is:")
    read = lambda: [int(m.group(1)) for m in pat.finditer(open(log, errors="replace").read())] if os.path.exists(log) else []
    done = len(read())
    while True:
        trials = read()
        while done < len(trials):
            prev = trials[done - 1] if done else 0
            done += 1
            name = os.path.join(outdir, f"seq{done - 1:02d}_trial{prev}")
            for attempt in range(5):   # the RTT logger shares the probe; retry on a clash
                try:
                    dump(name)
                    break
                except Exception as e:
                    print(f"dump {name} failed ({e}); retrying", flush=True)
                    time.sleep(1)
        if trials and trials[-1] >= 9:
            return
        time.sleep(1)


def hp_mag(acc, fs=800.0, fc=10.0):
    a = np.exp(-2 * np.pi * fc / fs)
    y = np.zeros_like(acc)
    for k in range(1, len(acc)):     # one-pole high-pass per axis, as ss_imu.c
        y[k] = a * (y[k - 1] + acc[k] - acc[k - 1])
    return np.linalg.norm(y, axis=1)


def gated(log, npz, trial, win_ms=30.0):
    z = np.load(npz)
    ms, m = z["ms"], hp_mag(z["acc"])
    ev = re.compile(rf"\[G2\] trial {trial} t=(\d+) (FELT\+HEARD|HEARD ONLY)")
    onsets = [int(x.group(1)) for x in ev.finditer(open(log, errors="replace").read())]
    groups = []
    for t in sorted(onsets):          # one heard onset per knock (rebounds within 400 ms dropped)
        if not groups or t - groups[-1] >= 400:
            groups.append(t)
    groups = [t for t in groups if ms[0] + 600 < t < ms[-1] - 100]
    print(f"{npz}: trial {trial}, {len(groups)} heard knocks inside the raw data")
    rows = []
    for t in groups:
        pre = m[(ms > t - 550) & (ms < t - 50)]
        noise = np.sqrt(np.mean(pre ** 2)) if len(pre) else np.nan
        w = (ms >= t - win_ms) & (ms <= t + win_ms)
        if not w.any():
            continue
        rows.append((t, noise, m[w].max(), ms[w], m[w]))
    for K in (2, 3, 4, 6):
        felt, dts = 0, []
        for t, noise, peak, wms, wm in rows:
            over = np.flatnonzero(wm > K * noise)
            if len(over):
                felt += 1
                dts.append(t - wms[over[0]])
        s = f"K={K}: felt {felt}/{len(rows)}"
        if dts:
            dts = np.array(dts)
            s += f", audio-minus-vibration median {np.median(dts):+.1f} ms, within 10 ms {100 * np.mean(np.abs(dts) <= 10):.0f}%"
        print("  " + s)
    # false triggers: how often the same rule fires in quiet stretches away from any onset
    for K in (2, 3, 4):
        quiet = 0
        tests = 0
        for c in np.arange(ms[0] + 600, ms[-1] - 100, 1000.0):
            if min(abs(c - t) for t in onsets) < 500 if onsets else False:
                continue
            pre = m[(ms > c - 550) & (ms < c - 50)]
            w = (ms >= c - win_ms) & (ms <= c + win_ms)
            if len(pre) and w.any():
                tests += 1
                quiet += m[w].max() > K * np.sqrt(np.mean(pre ** 2))
        print(f"  K={K}: fires in {quiet}/{tests} quiet {2 * win_ms:.0f} ms windows (the chance of a false 'felt')")
    print("  peak/noise per knock:", " ".join(f"{p / n:.1f}" for _, n, p, _, _ in rows))


def main():
    cmd = sys.argv[1]
    if cmd == "dump":
        dump(sys.argv[2])
    elif cmd == "watch":
        watch(sys.argv[2], sys.argv[3])
    elif cmd == "gated":
        trial = int(sys.argv[sys.argv.index("--trial") + 1])
        gated(sys.argv[2], sys.argv[3], trial)


if __name__ == "__main__":
    main()
