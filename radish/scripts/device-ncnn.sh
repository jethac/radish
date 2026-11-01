#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
SHARED_GROUPS=${RADISH_SHARED_GROUPS:-1}
[[ $SHARED_GROUPS =~ ^[1-8]$ ]] || { echo 'RADISH_SHARED_GROUPS must be 1..8' >&2; exit 2; }
MODE=
FP16_PACKED=false
COMPILE_ONLY=false
for arg in "$@"; do
    case $arg in
        --init|--reduce|--reduce-compile|--reduce-smoke)
            [[ -z $MODE || $MODE == "$arg" ]] || { echo 'choose one ncnn mode' >&2; exit 2; }
            MODE=$arg ;;
        --fp16-packed) FP16_PACKED=true ;;
        --compile-only) COMPILE_ONLY=true ;;
        *) echo 'usage: device-ncnn.sh [--init|--reduce|--reduce-compile|--reduce-smoke] [--compile-only] [--fp16-packed]' >&2; exit 2 ;;
    esac
done
if [[ $MODE == --init ]] && { $FP16_PACKED || $COMPILE_ONLY; }; then
    echo '--init cannot be combined with --fp16-packed or --compile-only' >&2
    exit 2
fi
RUN_ARGS=()
[[ -z $MODE ]] || RUN_ARGS+=("$MODE")
if $FP16_PACKED; then RUN_ARGS+=(--fp16-packed); fi
if $COMPILE_ONLY; then RUN_ARGS+=(--compile-only); fi
EXTRA_ENV=
if [[ $MODE == --reduce-compile ]] || $COMPILE_ONLY; then EXTRA_ENV=--setenv=IR3_SHADER_DEBUG=disasm; fi
TAG=ncnn
if [[ $MODE == --init ]]; then TAG=ncnn-init; fi
if [[ $MODE == --reduce* ]]; then TAG=ncnn${MODE#-}; fi
if $COMPILE_ONLY && [[ $MODE != --reduce-compile ]]; then TAG+=-compile; fi
if $FP16_PACKED; then TAG+=-fp16-packed; fi
if [[ -n ${RADISH_NCNN_LOCAL_SIZE:-} ]]; then
    [[ $MODE == --reduce* ]] || { echo 'local-size override requires a reduction mode' >&2; exit 2; }
    case $RADISH_NCNN_LOCAL_SIZE in 32|64) ;; *) echo 'local size must be 32 or 64' >&2; exit 2 ;; esac
    EXTRA_ENV+=" --setenv=RADISH_NCNN_LOCAL_SIZE=$RADISH_NCNN_LOCAL_SIZE"
    TAG+=-local$RADISH_NCNN_LOCAL_SIZE
fi
if [[ -n ${RADISH_NCNN_REDUCTION_STRIDE:-} ]]; then
    [[ ${RADISH_NCNN_LOCAL_SIZE:-} == 64 && $RADISH_NCNN_REDUCTION_STRIDE == 32 ]] || {
        echo 'stride override requires logical32 / physical64' >&2
        exit 2
    }
    EXTRA_ENV+=' --setenv=RADISH_NCNN_REDUCTION_STRIDE=32'
    TAG+=-stride32
fi
[[ $SHARED_GROUPS == 1 ]] || TAG+=-shared-groups$SHARED_GROUPS
EXTRA_ENV+=" --setenv=RADISH_SHARED_GROUPS=$SHARED_GROUPS --setenv=RADISH_PROFILE=1"
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
"${ADB[@]}" push "$WORK/build-ncnn/radish-ncnn" /var/tmp/radish/radish-ncnn
"${ADB[@]}" push "$WORK/build/src/freedreno/radish/libvulkan_radish.so" /var/tmp/radish/libvulkan_radish.so
"${ADB[@]}" shell "chmod 755 /var/tmp/radish/radish-ncnn; systemd-run --quiet --wait --pipe --collect -p MemoryMax=384M -p RuntimeMaxSec=60 -p LimitCORE=0 $EXTRA_ENV --setenv=LD_LIBRARY_PATH=/var/tmp/radish --setenv=VK_DRIVER_FILES=/var/tmp/radish/radish_icd.json /var/tmp/radish/radish-ncnn ${RUN_ARGS[*]}" 2>&1 | tee "$WORK/evidence/$TAG.txt"
if rg -n 'vkCreateComputePipelines failed|new_pipeline failed' "$WORK/evidence/$TAG.txt"; then exit 1; fi
