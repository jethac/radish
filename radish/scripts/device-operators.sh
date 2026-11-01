#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
SHARED32=${RADISH_NCNN_SHARED32:-0}
[[ $SHARED32 == 0 || $SHARED32 == 1 ]] || { echo 'RADISH_NCNN_SHARED32 must be 0 or 1' >&2; exit 2; }
SHARED_GROUPS=${RADISH_SHARED_GROUPS:-1}
[[ $SHARED_GROUPS =~ ^[1-8]$ ]] || { echo 'RADISH_SHARED_GROUPS must be 1..8' >&2; exit 2; }
EXTRA_ENV=
TAG=operators
for arg in "$@"; do
    case $arg in
        --conv|--inner|--smoke|--fp16-packed) TAG+=-${arg#--} ;;
        --compile-only) TAG+=-compile; EXTRA_ENV='--setenv=IR3_SHADER_DEBUG=disasm' ;;
        *) echo 'usage: device-operators.sh [--conv] [--inner] [--compile-only] [--smoke] [--fp16-packed]' >&2; exit 2 ;;
    esac
done
EXTRA_ENV+=" --setenv=RADISH_NCNN_SHARED32=$SHARED32 --setenv=RADISH_SHARED_GROUPS=$SHARED_GROUPS --setenv=RADISH_PROFILE=1"
[[ $SHARED32 == 0 ]] || TAG+=-shared32
[[ $SHARED_GROUPS == 1 ]] || TAG+=-shared-groups$SHARED_GROUPS
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
"${ADB[@]}" push "$WORK/build-ncnn/radish-operators" /var/tmp/radish/radish-operators
"${ADB[@]}" push "$WORK/build/src/freedreno/radish/libvulkan_radish.so" /var/tmp/radish/libvulkan_radish.so
"${ADB[@]}" shell "chmod 755 /var/tmp/radish/radish-operators; systemd-run --quiet --wait --pipe --collect -p MemoryMax=384M -p RuntimeMaxSec=90 -p LimitCORE=0 $EXTRA_ENV --setenv=LD_LIBRARY_PATH=/var/tmp/radish --setenv=VK_DRIVER_FILES=/var/tmp/radish/radish_icd.json /var/tmp/radish/radish-operators $*" 2>&1 | tee "$WORK/evidence/$TAG.txt"
if rg -n 'vkCreateComputePipelines failed|new_pipeline failed' "$WORK/evidence/$TAG.txt"; then exit 1; fi
