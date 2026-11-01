#!/usr/bin/env bash
main() {
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
KIND=${1:-core-shapes}
case "$KIND" in core|core-layouts|core-shapes) ;; *) echo 'invalid fixture kind' >&2; exit 2 ;; esac
TAG=waveform-$KIND
UNIT=radish-$TAG-$(date +%s%N)
for file in kokoro-waveform kokoro-tail.bin kokoro-reference-original.f32; do
    test -f "$WORK/$file"
done
exec 9>"${RADISH_DEVICE_LOCK:-$TOP/.device.lock}"
flock -w 1800 9
ADB=(adb -s "$SERIAL")
mkdir -p "$WORK/evidence"
"${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice'
"${ADB[@]}" shell dmesg > "$WORK/evidence/$TAG-dmesg-before.txt"
finish() {
    local result=$?
    trap - EXIT
    "${ADB[@]}" shell dmesg > "$WORK/evidence/$TAG-dmesg-after.txt" || result=1
    "${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice' || result=1
    python3 "$TOP/radish/scripts/check-gpu-log.py" \
        "$WORK/evidence/$TAG-dmesg-before.txt" \
        "$WORK/evidence/$TAG-dmesg-after.txt" || result=1
    exit "$result"
}
trap finish EXIT
"${ADB[@]}" shell "test -f /var/tmp/radish/kokoro-$KIND/ncnn-vulkan-output.f32"
for file in kokoro-waveform kokoro-tail.bin kokoro-reference-original.f32; do
    "${ADB[@]}" push "$WORK/$file" "/var/tmp/radish/$file"
done
"${ADB[@]}" shell "chmod 755 /var/tmp/radish/kokoro-waveform; systemd-run --unit=$UNIT --wait --pipe --collect -p MemoryMax=32M -p MemorySwapMax=0 -p RuntimeMaxSec=15 -p LimitCORE=0 /var/tmp/radish/kokoro-waveform /var/tmp/radish/kokoro-$KIND/ncnn-vulkan-output.f32 /var/tmp/radish/kokoro-tail.bin /var/tmp/radish/kokoro-reference-original.f32 /var/tmp/radish/kokoro-$KIND/ncnn-waveform.f32" 2>&1 | tee "$WORK/evidence/$TAG.txt"
SOURCE=$WORK/kokoro-$KIND
[[ $KIND != core ]] || SOURCE=$WORK/kokoro-core-fixed
"${ADB[@]}" pull "/var/tmp/radish/kokoro-$KIND/ncnn-waveform.f32" "$SOURCE/ncnn-device-waveform.f32"
}
main "$@"
