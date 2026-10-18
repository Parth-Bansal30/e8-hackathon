#!/bin/zsh
# Copy a recorder take out of the PSRAM over J-Link and save it as a stereo wav.
# Usage: tools/pull_audio.sh <out.wav> [take]      take = 1, 2, ... (default: the last one)
# Stops a running take first. Prints the dump speed (about 64 KB/s, i.e. one minute per minute of audio).
set -e
OUT=${1:?usage: pull_audio.sh <out.wav> [take]}
T=${0:A:h}
ELF=${ELF:-$T/../firmware/out/recorder/E8-HP/debug/recorder.elf}
J=($T/jlink.py $ELF)
if [ "$($J get rec_state | awk '{print $2}')" = "1" ]; then
    $J set rec_cmd 2
    sleep 0.3
fi
N=$($J get rec_takes | awk '{print $2}')
[ "$N" -gt 0 ] || { echo "no takes recorded"; exit 1; }
K=${2:-$N}
read OFF LEN <<<$(python3 -c "
import sys; sys.path.insert(0, '$T'); import jlink
b = jlink.Board('$ELF'); k = $K - 1
print(b.read32(b.addr('rec_take_off') + 4 * k)[0], b.read32(b.addr('rec_take_len') + 4 * k)[0])")
[ "$LEN" -gt 0 ] || { echo "take $K is empty"; exit 1; }
RAW=${OUT:r}.raw
S=$(python3 -c 'import time; print(time.time())')
$J dump $((0xA0000000 + OFF)) $LEN $RAW
python3 -c "import time; t=time.time()-$S; print(f'take $K of $N: dumped {$LEN/1e6:.2f} MB in {t:.1f} s ({$LEN/1e3/t:.0f} KB/s)')"
python3 $T/raw2wav.py $RAW $OUT
rm -f $RAW
