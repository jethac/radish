#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
exec 9>"${RADISH_DEVICE_LOCK:-$TOP/.device.lock}"
flock -w 1800 9
mkdir -p "$WORK/evidence"
ADB=(adb -s "$SERIAL")
"${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice'
"${ADB[@]}" shell dmesg > "$WORK/evidence/lifecycle-dmesg-before.txt"
"${ADB[@]}" shell 'mkdir -p /var/tmp/radish'
"${ADB[@]}" push "$WORK/build/src/freedreno/radish/libvulkan_radish.so" /var/tmp/radish/libvulkan_radish.so
"${ADB[@]}" push "$WORK/lifecycle" /var/tmp/radish/lifecycle
"${ADB[@]}" shell 'chmod 755 /var/tmp/radish/lifecycle; systemd-run --quiet --wait --pipe --collect -p MemoryMax=256M -p RuntimeMaxSec=45 /var/tmp/radish/lifecycle /var/tmp/radish/libvulkan_radish.so' 2>&1 | tee "$WORK/evidence/lifecycle.txt"
"${ADB[@]}" shell dmesg > "$WORK/evidence/lifecycle-dmesg-after.txt"
"${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice'
python3 "$TOP/radish/scripts/check-gpu-log.py" \
    "$WORK/evidence/lifecycle-dmesg-before.txt" \
    "$WORK/evidence/lifecycle-dmesg-after.txt"
