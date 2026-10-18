#!/bin/zsh
# Build a firmware project for the E8 HP core and flash it to MRAM with SETOOLS.
# Usage: tools/build_flash.sh <project> [debug|release]     e.g. tools/build_flash.sh app
# Stops before flashing if the build fails.
#
# Machine-specific paths, overridable from the environment:
#   ALIF_SETOOLS   Alif SETOOLS folder (with app-write-mram; serial port set in isp_config_data.cfg)
#   CMSIS_TOOLS    vcpkg artifacts folder holding cmsis-toolbox 2.15, arm-none-eabi-gcc 13.3.1, cmake, ninja
#                  (default: the ~/.vcpkg/artifacts/<hash> folder that has toolbox 2.15.0)
set -e
PROJ=${1:?usage: build_flash.sh <project> [debug|release]}
BUILD=${2:-debug}
FW=${0:A:h:h}/firmware
SET=${ALIF_SETOOLS:-~/alif/app-release-exec-macos}
A=${CMSIS_TOOLS:-$(print -l ~/.vcpkg/artifacts/*/tools.open.cmsis.pack.cmsis.toolbox/2.15.0(N:h:h) | head -1)}
[ -x "$A/compilers.arm.arm.none.eabi.gcc/13.3.1/bin/arm-none-eabi-gcc" ] || {
    echo "Toolchain not found: set CMSIS_TOOLS to the vcpkg artifacts folder (see docs/HANDOFF.md)"; exit 1; }
export PATH="$A/tools.open.cmsis.pack.cmsis.toolbox/2.15.0/bin:$A/compilers.arm.arm.none.eabi.gcc/13.3.1/bin:${$(find $A/tools.kitware.cmake -name cmake -type f | head -1):h}:${$(find $A/tools.ninja.build.ninja -name ninja -type f | head -1):h}:$PATH"
export GCC_TOOLCHAIN_13_3_1="$A/compilers.arm.arm.none.eabi.gcc/13.3.1/bin"
export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

[ -d $FW/third_party/executorch/et_bundle/lib ] || { echo "ExecuTorch libs missing: run tools/fetch_et_bundle.sh"; exit 1; }

cd $FW
LOG=/tmp/build_$PROJ.log
if ! cbuild alif.csolution.yml --context $PROJ.$BUILD+E8-HP --packs --update-rte > $LOG 2>&1; then
    grep -E "error" $LOG | head -20
    echo "BUILD FAILED (full log: $LOG)"; exit 1
fi
grep -A7 "Memory region" $LOG || true   # absent when nothing was relinked

cd $SET
cp $FW/out/$PROJ/E8-HP/$BUILD/$PROJ.bin build/images/alif-img.bin
cp $FW/.alif/M55_HP_cfg.json alif-img.json
trap 'rm -f build/images/alif-img.bin alif-img.json' EXIT
./app-gen-toc -f alif-img.json > /dev/null
./app-write-mram -p < /dev/null 2>&1 | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' | tr '\r' '\n' | grep -E "ERROR|RECOVERY|seconds"
echo "FLASHED $PROJ ($BUILD)"
