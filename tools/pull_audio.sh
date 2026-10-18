#!/bin/zsh
# Copy the recorder's last take out of the PSRAM over J-Link and save it as a stereo wav.
# Usage: tools/pull_audio.sh <out.wav> [elf]      (default elf: the recorder's debug build)
# Stops the take first if it is still running. Prints the dump speed.
set -e
OUT=${1:?usage: pull_audio.sh <out.wav> [elf]}
T=${0:A:h}
ELF=${2:-$T/../firmware/out/recorder/E8-HP/debug/recorder.elf}
J=($T/jlink.py $ELF)
if [ "$($J get rec_state | awk '{print $2}')" = "1" ]; then
    $J set rec_cmd 2
    sleep 0.3
fi
N=$($J get rec_bytes | awk '{print $2}')
[ "$N" -gt 0 ] || { echo "no audio recorded"; exit 1; }
RAW=${OUT:r}.raw
S=$(python3 -c 'import time; print(time.time())')
$J dump 0xA0000000 $N $RAW
E=$(python3 -c 'import time; print(time.time())')
python3 -c "import sys; n=$N; t=$E-$S; print(f'dumped {n/1e6:.2f} MB in {t:.1f} s ({n/1e3/t:.0f} KB/s)')"
python3 $T/raw2wav.py $RAW $OUT
rm -f $RAW
