#!/bin/zsh
# Print new output from the RTT up-buffer (channel 0) of the E8 HP core by reading memory over J-Link.
# Usage: tools/rtt_dump.sh <app.elf>        e.g. tools/rtt_dump.sh firmware/out/app/E8-HP/debug/app.elf
# Works even when JLinkRTTLogger can't attach, as long as the app called printf once.
# Each call prints what was written since the previous call and marks it as read. Without that the
# buffer fills after a minute or so and the firmware drops everything after (RTT's default no-block mode).
set -e
ELF=${1:?usage: rtt_dump.sh <app.elf>}
GCC=${CMSIS_TOOLS:-$(print -l ~/.vcpkg/artifacts/*/compilers.arm.arm.none.eabi.gcc/13.3.1(N:h:h) | head -1)}/compilers.arm.arm.none.eabi.gcc/13.3.1/bin
DEV=AE822FA0E5597LS0_M55_HP
TMP=$(mktemp -d)
trap 'rm -rf $TMP' EXIT
jlink() { JLinkExe -NoGui 1 -device $DEV -if SWD -speed 4000 -autoconnect 0 -CommandFile $1 2>/dev/null; }

CB=0x$($GCC/arm-none-eabi-nm $ELF | awk '$3=="_SEGGER_RTT"{print $1}')
# Up-buffer 0 descriptor starts 24 bytes into the control block:
# sName, pBuffer, SizeOfBuffer, WrOff, RdOff, Flags
DESC=$(printf '0x%x' $(( CB + 24 )))
printf 'connect\nmem32 %s 5\nqc\n' $DESC > $TMP/desc.jlink
read -r BUF SIZE WR RD <<<$(jlink $TMP/desc.jlink | awk -v d=${DESC#0x} 'toupper($1)==toupper(d){print "0x"$4, "0x"$5, "0x"$6, "0x"$7}')
[ -z "$BUF" ] && { echo "could not read RTT control block at $CB" >&2; exit 1; }
[ $(( WR )) -eq $(( RD )) ] && { echo "(no new RTT output)"; exit 0; }

# Unread data runs from RdOff to WrOff, wrapping at the end of the buffer.
{
    echo connect
    if [ $(( WR )) -gt $(( RD )) ]; then
        printf 'savebin %s 0x%x 0x%x\n' $TMP/a.bin $(( BUF + RD )) $(( WR - RD ))
    else
        printf 'savebin %s 0x%x 0x%x\n' $TMP/a.bin $(( BUF + RD )) $(( SIZE - RD ))
        [ $(( WR )) -gt 0 ] && printf 'savebin %s 0x%x 0x%x\n' $TMP/b.bin $(( BUF )) $(( WR ))
    fi
    printf 'w4 0x%x 0x%x\n' $(( DESC + 16 )) $(( WR ))
    echo qc
} > $TMP/dump.jlink
jlink $TMP/dump.jlink > /dev/null
cat $TMP/a.bin $TMP/b.bin 2>/dev/null | tr -d '\r'
exit 0
