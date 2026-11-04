#!/usr/bin/env bash
# Stream the tablet's kernel log to a host file (reconnecting after adb drops),
# so a wedge leaves the last kernel lines on the host.
# usage: dmesg-follow.sh FILE
set -u
. "$(dirname "$0")/common.sh"
OUT=$1
while true; do
    adb -s "$SERIAL" wait-for-device
    echo "=== $(date '+%F %T') connected: $(adbsh 'cat /proc/version; cut -d" " -f1 /proc/uptime' 2>/dev/null | tr '\n' ' ')" >> "$OUT"
    adb -s "$SERIAL" shell 'dmesg -w -T' >> "$OUT" 2>&1
    echo "=== $(date '+%F %T') adb stream ended" >> "$OUT"
    sleep 5
done
