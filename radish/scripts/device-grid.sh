#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
BATCH=${RADISH_BATCH_CHUNKS:-1}
[[ $BATCH =~ ^[1-4]$ ]] || { echo 'RADISH_BATCH_CHUNKS must be 1..4' >&2; exit 2; }
SHARED_GROUPS=${RADISH_SHARED_GROUPS:-1}
[[ $SHARED_GROUPS =~ ^[1-8]$ ]] || { echo 'RADISH_SHARED_GROUPS must be 1..8' >&2; exit 2; }
MODE=${1:-}
[[ -z $MODE || $MODE == --shared || $MODE == --compile-only || $MODE == --shared-compile ]] || {
    echo 'usage: device-grid.sh [--shared|--compile-only|--shared-compile]' >&2
    exit 2
}
SHADER=grid
ENV_ARGS=
TAG=grid
if [[ $MODE == --shared* ]]; then SHADER=grid-shared; TAG=grid-shared; fi
if [[ $MODE == *compile* ]]; then
    ENV_ARGS='--setenv=RADISH_GRID_COMPILE_ONLY=1 --setenv=IR3_SHADER_DEBUG=disasm'
    TAG+=-compile
fi
[[ $BATCH == 1 ]] || TAG+=-batch$BATCH
[[ $SHARED_GROUPS == 1 ]] || TAG+=-shared-groups$SHARED_GROUPS
ENV_ARGS+=" --setenv=RADISH_BATCH_CHUNKS=$BATCH --setenv=RADISH_SHARED_GROUPS=$SHARED_GROUPS --setenv=RADISH_PROFILE=1"
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
"${ADB[@]}" shell 'mkdir -p /var/tmp/radish'
"${ADB[@]}" push "$WORK/build/src/freedreno/radish/libvulkan_radish.so" /var/tmp/radish/libvulkan_radish.so
"${ADB[@]}" push "$WORK/grid" /var/tmp/radish/grid
"${ADB[@]}" push "$WORK/$SHADER.spv" /var/tmp/radish/grid.spv
"${ADB[@]}" shell 'chmod 755 /var/tmp/radish/grid'
"${ADB[@]}" shell "systemd-run --quiet --wait --pipe --collect -p MemoryMax=384M -p RuntimeMaxSec=45 -p LimitCORE=0 $ENV_ARGS /var/tmp/radish/grid /var/tmp/radish/libvulkan_radish.so /var/tmp/radish/grid.spv" 2>&1 | tee "$WORK/evidence/$TAG.txt"
