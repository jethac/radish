#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
BINARY=${RADISH_SNAKE_BINARY:-$WORK/build-ncnn/radish-snake}
RUN=${RADISH_SNAKE_RUN:-$(date +%s%N)}
[[ $RUN =~ ^[A-Za-z0-9_-]{1,48}$ ]] || { echo 'invalid run label' >&2; exit 2; }
TAG=snake
EXTRA_ENV=
for arg in "$@"; do
    case $arg in
        --cpu) TAG+=-cpu ;;
        --compile-only) TAG+=-compile; EXTRA_ENV=--setenv=IR3_SHADER_DEBUG=disasm ;;
        *) echo 'usage: device-snake.sh [--cpu] [--compile-only]' >&2; exit 2 ;;
    esac
done
TAG+=-$RUN
test -f "$BINARY"
mkdir -p "$WORK/evidence"
[[ ! -e $WORK/evidence/$TAG.txt ]] || { echo 'run label already has evidence' >&2; exit 2; }
sha256sum "$BINARY" "$WORK/build/src/freedreno/radish/libvulkan_radish.so"
exec 9>"${RADISH_DEVICE_LOCK:-$TOP/.device.lock}"
flock -w 1800 9
ADB=(adb -s "$SERIAL")
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
"${ADB[@]}" push "$BINARY" /var/tmp/radish/radish-snake
"${ADB[@]}" push "$WORK/build/src/freedreno/radish/libvulkan_radish.so" /var/tmp/radish/libvulkan_radish.so
"${ADB[@]}" shell "chmod 755 /var/tmp/radish/radish-snake; systemd-run --quiet --wait --pipe --collect -p MemoryMax=384M -p MemorySwapMax=128M -p RuntimeMaxSec=90 -p LimitCORE=0 $EXTRA_ENV --setenv=RADISH_PROFILE=1 --setenv=RADISH_BATCH_CHUNKS=1 --setenv=RADISH_TILED_BATCH_CHUNKS=1 --setenv=RADISH_SHARED_GROUPS=1 --setenv=LD_LIBRARY_PATH=/var/tmp/radish --setenv=VK_DRIVER_FILES=/var/tmp/radish/radish_icd.json /var/tmp/radish/radish-snake $*" 2>&1 | tee "$WORK/evidence/$TAG.txt"
if rg -n 'vkCreateComputePipelines failed|new_pipeline failed' "$WORK/evidence/$TAG.txt"; then exit 1; fi
