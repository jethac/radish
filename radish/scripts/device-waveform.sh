#!/usr/bin/env bash
main() {
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
KIND=${1:-core-shapes}
[[ $# -le 1 && $KIND =~ ^[A-Za-z0-9_-]+$ ]] || { echo 'invalid fixture kind' >&2; exit 2; }
case "$KIND" in core|core-layouts|core-shapes|core-shapes-instancenorm|runtime-*) ;; *) echo 'invalid fixture kind' >&2; exit 2 ;; esac
SOURCE=$WORK/kokoro-$KIND
[[ $KIND != core ]] || SOURCE=$WORK/kokoro-core-fixed
MODE=
REFERENCE=$WORK/kokoro-reference-original.f32
DEVICE_REFERENCE=/var/tmp/radish/kokoro-reference-original.f32
if [[ $KIND == runtime-* ]]; then
    test -f "$SOURCE/runtime-ncnn.json" || { echo 'runtime fixture assembly has not passed validation' >&2; exit 2; }
    MODE=--runtime-frames
    REFERENCE=$SOURCE/reference-waveform.f32
    DEVICE_REFERENCE=/var/tmp/radish/kokoro-$KIND/reference-waveform.f32
fi
TAG=waveform-$KIND
if [[ -n ${RADISH_WAVEFORM_RUN:-} ]]; then
    [[ $RADISH_WAVEFORM_RUN =~ ^[A-Za-z0-9_-]+$ ]] || { echo 'invalid run label' >&2; exit 2; }
    TAG+=-$RADISH_WAVEFORM_RUN
fi
UNIT=radish-$TAG-$(date +%s%N)
for file in kokoro-waveform kokoro-tail.bin; do
    test -f "$WORK/$file"
done
test -f "$REFERENCE"
test -d "$SOURCE"
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
for file in kokoro-waveform kokoro-tail.bin; do
    "${ADB[@]}" push "$WORK/$file" "/var/tmp/radish/$file"
done
"${ADB[@]}" push "$REFERENCE" "$DEVICE_REFERENCE"
rm -f -- "$SOURCE/ncnn-device-waveform.f32"
"${ADB[@]}" shell "rm -f /var/tmp/radish/kokoro-$KIND/ncnn-waveform.f32 && chmod 755 /var/tmp/radish/kokoro-waveform && systemd-run --unit=$UNIT --wait --pipe --collect -p MemoryMax=32M -p MemorySwapMax=0 -p RuntimeMaxSec=15 -p LimitCORE=0 /var/tmp/radish/kokoro-waveform $MODE /var/tmp/radish/kokoro-$KIND/ncnn-vulkan-output.f32 /var/tmp/radish/kokoro-tail.bin $DEVICE_REFERENCE /var/tmp/radish/kokoro-$KIND/ncnn-waveform.f32" 2>&1 | tee "$WORK/evidence/$TAG.txt"
"${ADB[@]}" pull "/var/tmp/radish/kokoro-$KIND/ncnn-waveform.f32" "$SOURCE/ncnn-device-waveform.f32"
cp -- "$SOURCE/ncnn-device-waveform.f32" "$WORK/evidence/$TAG-output.f32"
}
main "$@"
