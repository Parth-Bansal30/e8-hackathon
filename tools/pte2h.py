#!/usr/bin/env python3
"""Turn an ExecuTorch .pte into a C header the firmware can link into MRAM.

Usage: tools/pte2h.py model/mnist_int8io.pte firmware/npu_display/assets/mnist_model_data.h
The array is named after the file (mnist_int8io.pte -> mnist_int8io_pte, mnist_int8io_pte_len)
and 16-byte aligned, which the ExecuTorch loader needs.
"""
import os
import re
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1:]
    data = open(src, "rb").read()
    name = re.sub(r"\W", "_", os.path.basename(src))
    with open(dst, "w") as f:
        f.write("#include <stdint.h>\n")
        f.write(f"const uint8_t __attribute__((aligned(16))) {name}[] = {{\n")
        for i in range(0, len(data), 12):
            f.write("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 12]) + ",\n")
        f.write("};\n")
        f.write(f"unsigned int {name}_len = {len(data)};\n")
    print(f"{dst}: {name}[{len(data)}]")


if __name__ == "__main__":
    main()
