#!/usr/bin/env python3
"""Train (optional), quantize and export MNIST for the Alif E8 Ethos-U85.

Differences from the Arm tutorial flow:
  * calibrates int8 ranges on N real MNIST images instead of one sample
  * reports float and int8 accuracy on the host before anything is flashed

Usage (inside et-venv):
    python export_mnist.py --train --calib 500 --int8-io --out mnist_int8io.pte
"""
import argparse
import gzip
import os
import struct
import sys

import torch
import torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "mnist_data")
MEAN, STD = 0.1307, 0.3081
BASE_URL = "https://storage.googleapis.com/cvdf-datasets/mnist/"


class MNISTModel(nn.Module):
    """Same architecture as arm-education/alif-ethos-u85-npu-mnist."""

    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(1, 16, 3, padding=1)
        self.relu1 = nn.ReLU()
        self.pool1 = nn.MaxPool2d(2, 2)
        self.conv2 = nn.Conv2d(16, 32, 3, padding=1)
        self.relu2 = nn.ReLU()
        self.pool2 = nn.MaxPool2d(2, 2)
        self.fc1 = nn.Linear(32 * 7 * 7, 64)
        self.relu3 = nn.ReLU()
        self.fc2 = nn.Linear(64, 10)

    def forward(self, x):
        x = self.pool1(self.relu1(self.conv1(x)))
        x = self.pool2(self.relu2(self.conv2(x)))
        x = torch.flatten(x, 1)
        return self.fc2(self.relu3(self.fc1(x)))


def load_split(kind):
    names = {"train": ("train-images-idx3-ubyte.gz", "train-labels-idx1-ubyte.gz"),
             "test": ("t10k-images-idx3-ubyte.gz", "t10k-labels-idx1-ubyte.gz")}[kind]
    os.makedirs(DATA, exist_ok=True)
    for n in names:
        p = os.path.join(DATA, n)
        if not os.path.exists(p):
            import urllib.request
            urllib.request.urlretrieve(BASE_URL + n, p)
    with gzip.open(os.path.join(DATA, names[0])) as f:
        _, count, rows, cols = struct.unpack(">IIII", f.read(16))
        x = torch.frombuffer(bytearray(f.read()), dtype=torch.uint8).view(count, 1, rows, cols)
    with gzip.open(os.path.join(DATA, names[1])) as f:
        f.read(8)
        y = torch.frombuffer(bytearray(f.read()), dtype=torch.uint8).long()
    return ((x.float() / 255.0) - MEAN) / STD, y


def accuracy(fn, x, y, n=2000):
    with torch.no_grad():
        pred = torch.cat([fn(x[i:i + 200]).argmax(1) for i in range(0, n, 200)])
    return (pred == y[:n]).float().mean().item()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", action="store_true", help="train instead of loading --ckpt")
    ap.add_argument("--epochs", type=int, default=3)
    ap.add_argument("--ckpt", default=os.path.join(HERE, "mnist_model.pth"))
    ap.add_argument("--calib", type=int, default=500, help="calibration images")
    ap.add_argument("--target", default="ethos-u85-256")
    ap.add_argument("--system_config", default="Ethos_U85_SYS_DRAM_Mid")
    ap.add_argument("--memory_mode", default="Shared_Sram")
    ap.add_argument("--int8-io", action="store_true",
                    help="int8 model input/output (no CPU quant ops); writes <out>_io.h")
    ap.add_argument("--out",default=os.path.join(HERE, "mnist_ethos_u85.pte"))
    args = ap.parse_args()

    torch.manual_seed(0)
    model = MNISTModel()
    xte, yte = load_split("test")

    if args.train:
        xtr, ytr = load_split("train")
        opt = torch.optim.Adam(model.parameters(), lr=1e-3)
        for ep in range(args.epochs):
            model.train()
            perm = torch.randperm(len(xtr))
            for i in range(0, len(xtr), 128):
                idx = perm[i:i + 128]
                opt.zero_grad()
                nn.functional.cross_entropy(model(xtr[idx]), ytr[idx]).backward()
                opt.step()
            model.eval()
            print(f"epoch {ep + 1}: test acc {accuracy(model, xte, yte):.4f}", flush=True)
        torch.save(model.state_dict(), args.ckpt)
    else:
        model.load_state_dict(torch.load(args.ckpt, map_location="cpu"))
    model.eval()
    print(f"float32 accuracy: {accuracy(model, xte, yte):.4f}")

    # --- ExecuTorch Arm flow (mirrors examples/arm/aot_arm_compiler.py in v1.0.0) ---
    from executorch.backends.arm.ethosu import EthosUCompileSpec, EthosUPartitioner
    from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_quantization_config
    from executorch.backends.cortex_m.passes.replace_quant_nodes_pass import ReplaceQuantNodesPass
    from executorch.exir import EdgeCompileConfig, ExecutorchBackendConfig, to_edge_transform_and_lower
    from torchao.quantization.pt2e.quantize_pt2e import convert_pt2e, prepare_pt2e

    example = (xte[:1].contiguous(),)
    spec = EthosUCompileSpec(args.target, system_config=args.system_config,
                             memory_mode=args.memory_mode,
                             extra_flags=["--verbose-cycle-estimate"])

    graph = torch.export.export(model, example).module()
    quantizer = EthosUQuantizer(spec)
    quantizer.set_global(get_symmetric_quantization_config())
    prepared = prepare_pt2e(graph, quantizer)
    # Calibrate on training-like data (held-out test images 2000.. so accuracy below stays fair)
    with torch.no_grad():
        for i in range(2000, 2000 + args.calib):
            prepared(xte[i:i + 1])
    q = convert_pt2e(prepared)
    print(f"int8 (host sim) accuracy: {accuracy(q, xte, yte):.4f}")

    ep = torch.export.export(q, example)
    edge = to_edge_transform_and_lower(ep, partitioner=[EthosUPartitioner(spec)],
                                       compile_config=EdgeCompileConfig(_check_ir_validity=False))
    if args.int8_io:
        # Strip the CPU quantize/dequantize at the graph edges: the whole program
        # becomes one NPU call with int8 in/out; the app does the scaling in C.
        from executorch.exir.passes.quantize_io_pass import extract_io_quant_params
        qp = extract_io_quant_params(edge, input_idxs=(0,), output_idxs=(0,))
        (in_q,), (out_q,) = qp["inputs"].values(), qp["outputs"].values()
        print(f"int8 IO: input scale={in_q['scale']} zp={in_q['zero_point']}  "
              f"output scale={out_q['scale']} zp={out_q['zero_point']}")
        hdr = os.path.splitext(args.out)[0] + "_io.h"
        with open(hdr, "w") as f:
            f.write("/* Generated by export_mnist.py: int8 IO quantization of the model */\n#pragma once\n")
            f.write(f"#define MODEL_IN_SCALE  ({in_q['scale']:.9g}f)\n#define MODEL_IN_ZP     ({in_q['zero_point']})\n")
            f.write(f"#define MODEL_OUT_SCALE ({out_q['scale']:.9g}f)\n#define MODEL_OUT_ZP    ({out_q['zero_point']})\n")
            f.write(f"#define MODEL_NORM_MEAN ({MEAN}f)\n#define MODEL_NORM_STD  ({STD}f)\n")
        print(f"wrote {hdr}")
    else:
        edge = edge.transform([ReplaceQuantNodesPass()])
    prog = edge.to_executorch(config=ExecutorchBackendConfig(extract_delegate_segments=False))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        prog.write_to_file(f)
    print(f"wrote {args.out} ({os.path.getsize(args.out)} bytes)")

    # Expected logits for the first test images, to compare against the board.
    with torch.no_grad():
        for i in range(3):
            print(f"test[{i}] label={yte[i].item()} int8 logits=",
                  [round(v, 3) for v in q(xte[i:i + 1])[0].tolist()])


if __name__ == "__main__":
    sys.exit(main())
