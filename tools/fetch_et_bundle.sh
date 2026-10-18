#!/bin/zsh
# Download the prebuilt ExecuTorch 1.0 runtime (headers + Cortex-M55/Ethos-U static libs) that the
# firmware links against, from Arm's education repo, and unpack it into firmware/third_party.
# The libraries are not committed here; the checksum pins the exact bundle the firmware was tested with.
set -e
URL=https://raw.githubusercontent.com/arm-education/alif-ethos-u85-npu-mnist/main/et_bundle.tar.gz
SHA256=7e64d9bdf7be4bff307157e366ff57f1c4aa79b62612c13e56b3de3e3c0c1cb2
DEST=${0:A:h:h}/firmware/third_party/executorch
TMP=$(mktemp -d)
trap 'rm -rf $TMP' EXIT

curl -fsSL -o $TMP/et_bundle.tar.gz $URL
echo "$SHA256  $TMP/et_bundle.tar.gz" | shasum -a 256 -c - > /dev/null || {
    echo "checksum mismatch: upstream et_bundle.tar.gz changed" >&2; exit 1; }
mkdir -p $DEST
tar -xzf $TMP/et_bundle.tar.gz -C $DEST
ls $DEST/et_bundle/lib | wc -l | xargs echo "ExecuTorch libs ready:"
