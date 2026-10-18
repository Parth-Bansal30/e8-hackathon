#!/usr/bin/env python3
"""Talk to the running E8 HP core over J-Link without stopping it: read and write firmware
variables by symbol name, dump memory, and read new RTT output. Used to script recordings and
on-board tests (tools/pull_audio.sh, tools/g3_live.py).

    tools/jlink.py <elf> get rec_bytes            print a uint32 variable
    tools/jlink.py <elf> set rec_cmd 1            write a uint32 variable
    tools/jlink.py <elf> dump 0xA0000000 4096 out.bin
    tools/jlink.py <elf> rtt                      print new RTT output (like tools/rtt_dump.sh)

Each call attaches and detaches; J-Link clears DEMCR.TRCENA on detach, so firmware timing must
re-arm the cycle counter on every read (ss_cycles() does).
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

DEV = "AE822FA0E5597LS0_M55_HP"


def _gcc_bin():
    if os.environ.get("CMSIS_TOOLS"):
        base = os.environ["CMSIS_TOOLS"]
    else:
        hits = glob.glob(os.path.expanduser("~/.vcpkg/artifacts/*/compilers.arm.arm.none.eabi.gcc/13.3.1"))
        if not hits:
            sys.exit("arm-none-eabi toolchain not found: set CMSIS_TOOLS")
        return os.path.join(hits[0], "bin")
    return os.path.join(base, "compilers.arm.arm.none.eabi.gcc", "13.3.1", "bin")


class Board:
    def __init__(self, elf):
        self.elf = elf
        out = subprocess.run([os.path.join(_gcc_bin(), "arm-none-eabi-nm"), "-S", elf],
                             capture_output=True, text=True, check=True).stdout
        self.syms = {}
        for line in out.splitlines():
            p = line.split()
            if len(p) == 4:
                self.syms[p[3]] = (int(p[0], 16), int(p[1], 16))
            elif len(p) == 3:
                self.syms[p[2]] = (int(p[0], 16), 0)

    def addr(self, name):
        if name not in self.syms:
            raise KeyError(f"symbol {name} not in {self.elf}")
        return self.syms[name][0]

    def run(self, cmds):
        with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as f:
            f.write("connect\n" + "\n".join(cmds) + "\nqc\n")
            path = f.name
        try:
            r = subprocess.run(["JLinkExe", "-NoGui", "1", "-device", DEV, "-if", "SWD", "-speed", os.environ.get("JLINK_SPEED", "4000"),
                                "-autoconnect", "0", "-CommandFile", path],
                               capture_output=True, text=True, timeout=600)
        finally:
            os.unlink(path)
        return r.stdout

    def read32(self, addr, n=1):
        out = self.run([f"mem32 0x{addr:08X} {n}"])
        vals = []
        for line in out.splitlines():
            m = re.match(r"^([0-9A-Fa-f]{8}) = (.*)$", line.strip())
            if m:
                vals += [int(v, 16) for v in m.group(2).split()]
        if len(vals) < n:
            raise RuntimeError(f"J-Link read of 0x{addr:08X} failed:\n{out[-800:]}")
        return vals[:n]

    def get(self, name):
        return self.read32(self.addr(name))[0]

    def set(self, name, value):
        self.run([f"w4 0x{self.addr(name):08X} 0x{value & 0xFFFFFFFF:08X}"])

    def dump(self, addr, length, path):
        out = self.run([f"savebin {path} 0x{addr:08X} 0x{length:X}"])
        if not os.path.exists(path) or os.path.getsize(path) != length:
            raise RuntimeError(f"savebin failed:\n{out[-800:]}")

    def rtt(self):
        """New text in RTT up-buffer 0 since the last call (marks it read)."""
        desc = self.addr("_SEGGER_RTT") + 24   # up-buffer 0: sName, pBuffer, SizeOfBuffer, WrOff, RdOff
        _, buf, size, wr, rd = self.read32(desc, 5)
        if wr == rd:
            return ""
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "a.bin"), os.path.join(d, "b.bin")
            cmds = []
            if wr > rd:
                cmds.append(f"savebin {a} 0x{buf + rd:08X} 0x{wr - rd:X}")
            else:
                cmds.append(f"savebin {a} 0x{buf + rd:08X} 0x{size - rd:X}")
                if wr:
                    cmds.append(f"savebin {b} 0x{buf:08X} 0x{wr:X}")
            cmds.append(f"w4 0x{desc + 16:08X} 0x{wr:08X}")
            self.run(cmds)
            data = b"".join(open(p, "rb").read() for p in (a, b) if os.path.exists(p))
        return data.decode("utf-8", "replace").replace("\r", "")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    b = Board(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == "get":
        for n in sys.argv[3:]:
            print(n, b.get(n))
    elif cmd == "set":
        b.set(sys.argv[3], int(sys.argv[4], 0))
    elif cmd == "dump":
        b.dump(int(sys.argv[3], 0), int(sys.argv[4], 0), sys.argv[5])
    elif cmd == "rtt":
        sys.stdout.write(b.rtt())
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
