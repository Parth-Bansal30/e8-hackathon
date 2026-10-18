#!/usr/bin/env python3
"""Quantize and export the Sound Sense encoder for the Alif E8 Ethos-U85.

Same ExecuTorch Arm flow as export_kws.py (PT2E int8, int8 IO, whole graph on the NPU). The
features are the keyword model's, so no feature tables are written here (firmware/app/kws.c and
kws_tables.h are reused as they are).

Usage (inside et-venv, with et-venv/bin on PATH for flatc):
    python export_ss.py --untrained       # random weights: check lowering, latency and parity
    python export_ss.py                   # after train_ss.py
Outputs:
    ss_encoder.pte, ss_encoder_io.h
    ../firmware/soundsense/assets/ss_model_data.h       (ss_encoder_pte, ss_encoder_pte_len)
    ../firmware/soundsense/assets/ss_encoder_io.h       (SS_IN_SCALE, SS_IN_ZP, SS_OUT_SCALE, SS_OUT_ZP, SS_EMB)
    ../firmware/soundsense/assets/ss_testvec.h          (one clip, its int8 features and embedding)
    ../firmware/soundsense/assets/ss_builtin_profiles.h (9 profiles from the int8 model)
"""
import argparse
import contextlib
import json
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
import torch

import kws_common as K
import ss_common as S

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "firmware", "soundsense", "assets")

TARGET, SYSTEM_CONFIG, MEMORY_MODE = "ethos-u85-256", "Ethos_U85_SYS_DRAM_Mid", "Shared_Sram"

# Built-in profiles, in the order the firmware expects. Alerting ones raise an alert; speech is an
# indicator and background absorbs room noise.
PROFILE_NAMES = ["knock", "dog", "baby", "alarm", "glass", "siren", "water", "speech", "background"]
PROFILE_ALERTING = [1, 1, 1, 1, 1, 1, 1, 0, 0]


def load_model(ckpt=None, untrained=False, cfg=None):
    if untrained:
        torch.manual_seed(0)
        m = S.SSEncoder(**(cfg or {}))
    else:
        m, _ = S.load_encoder(ckpt)
    return K.fold_batchnorm(m.eval())


def features(x, bs=500):
    """Float audio [N, 16000] (numpy, [-1, 1)) -> float features [N, 1, 49, 40] (torch)."""
    f = K.Features()
    with torch.no_grad():
        return torch.cat([f(torch.from_numpy(np.ascontiguousarray(x[i:i + bs], dtype=np.float32)))
                          for i in range(0, len(x), bs)])


def calib_audio(d, n, rng):
    """Real training-distribution clips for calibration: ESC-50 fold 1-4 windows of the trained
    classes, Speech Commands train clips and background crops (no held-out classes)."""
    ids = np.concatenate([d.esc_ids(c, [1, 2, 3, 4]) for c in d.esc_train_cats])
    ne, ns = n // 2, n // 3
    esc = [d.esc_window(i) for i in rng.choice(ids, ne, replace=False)]
    sc = list(np.asarray(d.sc_train_x[np.sort(rng.choice(len(d.sc_train_y), ns, replace=False))]))
    x = np.stack(esc + sc).astype(np.float32) / 32768.0
    return np.concatenate([x, d.noise_crops(S.NOISE_FILES, "train", n - ne - ns, rng)])


def quantize(model, calib):
    """PT2E int8 quantization as export_kws.py does it. Returns (quantized module, compile spec)."""
    from executorch.backends.arm.ethosu import EthosUCompileSpec
    from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import convert_pt2e, prepare_pt2e

    spec = EthosUCompileSpec(TARGET, system_config=SYSTEM_CONFIG, memory_mode=MEMORY_MODE,
                             extra_flags=["--verbose-cycle-estimate"])
    graph = torch.export.export(model, (calib[:1],)).module()
    quantizer = EthosUQuantizer(spec)
    quantizer.set_global(get_symmetric_quantization_config())
    prepared = prepare_pt2e(graph, quantizer)
    with torch.no_grad():
        for i in range(len(calib)):
            prepared(calib[i:i + 1])
    return convert_pt2e(prepared), spec


def run(fn, f, bs=1):
    """Embeddings [N, 64] from a model over features [N, 1, 49, 40]. The quantized graph was
    exported at batch 1, so it runs one row at a time."""
    with torch.no_grad():
        return torch.cat([fn(f[i:i + bs]) for i in range(0, len(f), bs)]).numpy()


def builtin_examples(d, rng):
    """Float audio per built-in profile, from training data only (ESC-50 folds 1-4, Speech
    Commands train, the train part of the noise files)."""
    ex = {}
    for name in PROFILE_NAMES[:7]:
        ids = d.esc_ids(S.BUILTINS[name], [1, 2, 3, 4])
        x = np.stack([d.esc_window(i) for i in ids]).astype(np.float32) / 32768.0
        if name == "water":
            tap = d.tap_windows("train").astype(np.float32) / 32768.0
            x = np.concatenate([x, tap[np.linspace(0, len(tap) - 1, len(ids)).astype(int)]])
        ex[name] = x
    sc = np.sort(rng.choice(len(d.sc_train_y), 200, replace=False))
    ex["speech"] = np.asarray(d.sc_train_x[sc], dtype=np.float32) / 32768.0
    ex["background"] = d.noise_crops(S.NOISE_FILES, "train", 200, rng)
    return ex


def builtin_profiles(embed_fn, d, rng):
    """(names, protos [9, 64], thresholds [9]) from embed_fn(float audio) -> embeddings."""
    protos, thrs = [], []
    for name, x in builtin_examples(d, rng).items():
        p, t = S.make_profile(embed_fn(x))
        protos.append(p)
        thrs.append(t)
    return PROFILE_NAMES, np.stack(protos), np.array(thrs)


@contextlib.contextmanager
def capture_output(buf):
    """Copy everything written to fds 1 and 2 (Vela prints its summary there) into buf["text"],
    and still show it."""
    sys.stdout.flush()
    sys.stderr.flush()
    tmp = tempfile.TemporaryFile()
    saved = os.dup(1), os.dup(2)
    os.dup2(tmp.fileno(), 1)
    os.dup2(tmp.fileno(), 2)
    try:
        yield buf
    finally:
        sys.stdout.flush()
        sys.stderr.flush()
        os.dup2(saved[0], 1)
        os.dup2(saved[1], 2)
        os.close(saved[0])
        os.close(saved[1])
        tmp.seek(0)
        buf["text"] = tmp.read().decode(errors="replace")
        tmp.close()
        sys.stdout.write(buf["text"])
        sys.stdout.flush()


def vela_summary(text):
    pats = {"total_cycles": r"Total cycles\s+(\d+)", "npu_cycles": r"NPU cycles\s+(\d+)",
            "inference_ms_at_vela_clock": r"Batch Inference time\s+([\d.]+) ms",
            "vela_clock_mhz": r"Accelerator clock\s+(\d+) MHz", "cpu_operators": r"CPU operators = (\d+)",
            "npu_operators": r"NPU operators = (\d+)", "sram_used_kib": r"Total SRAM used\s+([\d.]+) KiB",
            "dram_used_kib": r"Total DRAM used\s+([\d.]+) KiB"}
    out = {}
    for k, p in pats.items():
        m = re.findall(p, text)
        if m:
            out[k] = float(m[-1]) if "." in m[-1] else int(m[-1])
    return out


def q_io(x, qp):
    return np.clip(np.round(x / qp["scale"]) + qp["zero_point"], -128, 127).astype(np.int8)


def c_array(fh, t, name, vals, per):
    fh.write(f"static const {t} {name}[{len(vals)}] = {{\n")
    for j in range(0, len(vals), per):
        fh.write("    " + ", ".join(str(int(v)) for v in vals[j:j + per]) + ",\n")
    fh.write("};\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", default=os.path.join(HERE, "ss_encoder.pth"))
    ap.add_argument("--untrained", action="store_true")
    ap.add_argument("--ch", type=int, default=192, help="--untrained only: encoder width")
    ap.add_argument("--stride_block", type=int, default=1, help="--untrained only")
    ap.add_argument("--calib", type=int, default=600)
    ap.add_argument("--out", default=os.path.join(HERE, "ss_encoder.pte"))
    ap.add_argument("--assets", default=ASSETS)
    args = ap.parse_args()
    os.makedirs(args.assets, exist_ok=True)
    tag = "UNTRAINED (random weights, placeholder profiles)" if args.untrained else f"trained, {args.ckpt}"

    d = S.Data()
    rng = np.random.default_rng(7)
    model = load_model(args.ckpt, args.untrained, cfg=dict(ch=args.ch, stride_block=args.stride_block))
    params, macs = S.count_params_macs(model)
    print(f"encoder {model.cfg} ({tag}): {params} parameters (BN folded), {macs / 1e6:.2f} M MACs")

    calib = features(calib_audio(d, args.calib, rng))
    q, spec = quantize(model, calib)

    from executorch.backends.arm.ethosu import EthosUPartitioner
    from executorch.exir import EdgeCompileConfig, ExecutorchBackendConfig, to_edge_transform_and_lower
    from executorch.exir.passes.quantize_io_pass import extract_io_quant_params

    vela = {}
    with capture_output(vela):
        edge = to_edge_transform_and_lower(torch.export.export(q, (calib[:1],)), partitioner=[EthosUPartitioner(spec)],
                                           compile_config=EdgeCompileConfig(_check_ir_validity=False))
    vela = vela_summary(vela["text"])
    qp = extract_io_quant_params(edge, input_idxs=(0,), output_idxs=(0,))
    (in_q,), (out_q,) = qp["inputs"].values(), qp["outputs"].values()
    print(f"int8 IO: input scale={in_q['scale']} zp={in_q['zero_point']}  "
          f"output scale={out_q['scale']} zp={out_q['zero_point']}")
    ops = sorted(set(str(n.target) for n in edge.exported_program().graph.nodes if n.op == "call_function"))
    prog = edge.to_executorch(config=ExecutorchBackendConfig(extract_delegate_segments=False))
    with open(args.out, "wb") as f:
        prog.write_to_file(f)
    pte_size = os.path.getsize(args.out)
    print(f"wrote {args.out} ({pte_size} bytes)")

    io_text = (f"/* Generated by model/export_ss.py ({tag}): int8 IO of the Sound Sense encoder.\n"
               f" * Input [1, 1, {K.FRAMES}, {K.NMEL}] int8 log-mel (kws_common.Features), output [1, {S.EMB}] int8. */\n"
               "#pragma once\n"
               f"#define SS_IN_SCALE  ({in_q['scale']:.9g}f)\n#define SS_IN_ZP     ({in_q['zero_point']})\n"
               f"#define SS_OUT_SCALE ({out_q['scale']:.9g}f)\n#define SS_OUT_ZP    ({out_q['zero_point']})\n"
               f"#define SS_EMB {S.EMB}\n")
    for p in (os.path.splitext(args.out)[0] + "_io.h", os.path.join(args.assets, "ss_encoder_io.h")):
        with open(p, "w") as f:
            f.write(io_text)
        print(f"wrote {p}")
    subprocess.run([sys.executable, os.path.join(HERE, "..", "tools", "pte2h.py"), args.out,
                    os.path.join(args.assets, "ss_model_data.h")], check=True)

    # Boot self-test vector: an ESC-50 fold 5 dog clip, its loudest window.
    i = int(d.esc_ids("dog", [5])[0])
    clip = d.esc_window(i)
    f = features(clip[None].astype(np.float32) / 32768.0)
    qin = q_io(f.numpy().ravel(), in_q)
    emb = run(q, f)[0]
    qout = q_io(emb, out_q)
    p = os.path.join(args.assets, "ss_testvec.h")
    with open(p, "w") as fh:
        fh.write(f"/* Generated by model/export_ss.py ({tag}): boot self-test.\n"
                 f" * ESC-50 clip #{i} ({d.esc_cat[i]}, fold 5, never trained on), its loudest 1 s window\n"
                 f" * (start sample {int(d.esc_loud[i])} at 16 kHz), the host int8 features (model input) and\n"
                 " * the host int8 embedding (model output). */\n#pragma once\n#include <stdint.h>\n")
        fh.write('#define SS_TEST_NAME "dog"\n')
        c_array(fh, "int16_t", "ss_test_clip", clip, 16)
        c_array(fh, "int8_t", "ss_test_input", qin, 40)
        c_array(fh, "int8_t", "ss_test_output", qout, 16)
    print(f"wrote {p} (clip #{i})")

    # Built-in profiles from the int8 model's dequantized embeddings.
    names, protos, thrs = builtin_profiles(lambda x: run(q, features(x)), d, np.random.default_rng(11))
    p = os.path.join(args.assets, "ss_builtin_profiles.h")
    with open(p, "w") as fh:
        fh.write(f"/* Generated by model/export_ss.py ({tag}): built-in sound profiles.\n"
                 " * Each prototype is the unit-norm mean of the unit-norm int8-model embeddings of its\n"
                 " * training examples (ESC-50 folds 1-4; water adds running_tap train windows; speech is\n"
                 " * Speech Commands train; background is noise-file train crops). Threshold rule as\n"
                 " * ss_common.make_profile: lowest leave-one-out similarity - 0.05, clamped to [0.5, 0.85]. */\n"
                 "#pragma once\n#include \"ss_encoder_io.h\"\n")
        fh.write(f"#define SS_NUM_BUILTIN {len(names)}\n")
        fh.write("static const char *const ss_builtin_names[SS_NUM_BUILTIN] = {"
                 + ", ".join(f'"{n}"' for n in names) + "};\n")
        fh.write("static const unsigned char ss_builtin_alerting[SS_NUM_BUILTIN] = {"
                 + ", ".join(str(a) for a in PROFILE_ALERTING) + "};\n")
        fh.write("static const float ss_builtin_thr[SS_NUM_BUILTIN] = {"
                 + ", ".join(f"{t:.4f}f" for t in thrs) + "};\n")
        fh.write("static const float ss_builtin_proto[SS_NUM_BUILTIN][SS_EMB] = {\n")
        for n, pr in zip(names, protos):
            fh.write(f"    /* {n} */ {{" + ", ".join(f"{v:.6f}f" for v in pr) + "},\n")
        fh.write("};\n")
    print(f"wrote {p}: thresholds " + ", ".join(f"{n} {t:.3f}" for n, t in zip(names, thrs)))

    # Host check of the self-test: int8 vs float embedding of the test clip.
    ef = run(model, f)[0]
    print(f"test clip: cos(int8, float) = {float(S.normalize(emb) @ S.normalize(ef)):.4f}; "
          f"best built-in (int8) {names[int((S.normalize(emb) @ protos.T).argmax())]}")
    print(f"params {params}, MACs {macs}, .pte {pte_size} bytes")
    compute_ops = [o for o in ops if o != "<built-in function getitem>"]  # getitem unpacks the delegate's outputs
    print("top-level ops after lowering:", ops, "-> compute ops:", compute_ops)
    if compute_ops != ["executorch_call_delegate"]:
        print("WARNING: not everything is delegated to the NPU")
    print("Vela estimate:", vela)
    info = {"untrained": args.untrained, "epoch": None if args.untrained else S.load_encoder(args.ckpt)[1]["epoch"],
            "cfg": model.cfg, "pte_bytes": pte_size, "top_level_ops": ops, "compute_ops": compute_ops,
            "all_on_npu": compute_ops == ["executorch_call_delegate"], "vela": vela,
            "target": [TARGET, SYSTEM_CONFIG, MEMORY_MODE],
            "io": {"in_scale": in_q["scale"], "in_zp": in_q["zero_point"], "out_scale": out_q["scale"],
                   "out_zp": out_q["zero_point"]},
            "builtin_thresholds": dict(zip(names, map(float, thrs)))}
    os.makedirs(os.path.join(HERE, "out"), exist_ok=True)
    with open(os.path.join(HERE, "out", "ss_export_info.json"), "w") as f:
        json.dump(info, f, indent=1)


if __name__ == "__main__":
    sys.exit(main())
