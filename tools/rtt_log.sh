#!/bin/zsh
# Follow the board's RTT output into a log file (tools/g3_play.py reads docs/gates/ss_rtt.log).
# Usage: tools/rtt_log.sh [project] [debug|release] [log]     defaults: soundsense release docs/gates/ss_rtt.log
# Stop it before rebuilding: a new ELF moves _SEGGER_RTT and the logger reads garbage.
ROOT=${0:A:h:h}
PROJ=${1:-soundsense}
BUILD=${2:-release}
LOG=${3:-$ROOT/docs/gates/ss_rtt.log}
ELF=$ROOT/firmware/out/$PROJ/E8-HP/$BUILD/$PROJ.elf
[ -f $ELF ] || { echo "No ELF at $ELF: build first"; exit 1; }
echo "RTT from $ELF -> $LOG (Ctrl-C to stop)"
while true; do $ROOT/tools/jlink.py $ELF rtt >> $LOG 2>/dev/null; sleep 2; done
