#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
SHARED32=${RADISH_NCNN_SHARED32:-0}
[[ $SHARED32 == 0 || $SHARED32 == 1 ]] || { echo 'RADISH_NCNN_SHARED32 must be 0 or 1' >&2; exit 2; }
TILED=${RADISH_NCNN_TILED:-0}
[[ $TILED == 0 || $TILED == 1 ]] || { echo 'RADISH_NCNN_TILED must be 0 or 1' >&2; exit 2; }
TILED32=${RADISH_NCNN_TILED32:-0}
[[ $TILED32 == 0 || $TILED32 == 1 ]] || { echo 'RADISH_NCNN_TILED32 must be 0 or 1' >&2; exit 2; }
PLANAR=${RADISH_NCNN_PLANAR:-0}
PACKED_A=${RADISH_NCNN_PLANAR_PACKED_A:-0}
[[ $PACKED_A == 0 || $PACKED_A == 1 ]] || { echo 'RADISH_NCNN_PLANAR_PACKED_A must be 0 or 1' >&2; exit 2; }
[[ $PLANAR == 0 || $PLANAR == 1 ]] || { echo 'RADISH_NCNN_PLANAR must be 0 or 1' >&2; exit 2; }
NORM_SUM4=${RADISH_NCNN_NORM_SUM4:-0}
[[ $NORM_SUM4 == 0 || $NORM_SUM4 == 1 ]] || { echo 'RADISH_NCNN_NORM_SUM4 must be 0 or 1' >&2; exit 2; }
NORM_OUTPLACE=${RADISH_NCNN_NORM_OUTPLACE-0}
[[ $NORM_OUTPLACE == 0 || $NORM_OUTPLACE == 1 ]] || { echo 'RADISH_NCNN_NORM_OUTPLACE must be 0 or 1' >&2; exit 2; }
RESHAPE_ALIAS=${RADISH_NCNN_RESHAPE_ALIAS:-0}
[[ $RESHAPE_ALIAS == 0 || $RESHAPE_ALIAS == 1 ]] || { echo 'RADISH_NCNN_RESHAPE_ALIAS must be 0 or 1' >&2; exit 2; }
SHARED_GROUPS=${RADISH_SHARED_GROUPS:-1}
[[ $SHARED_GROUPS =~ ^[1-8]$ ]] || { echo 'RADISH_SHARED_GROUPS must be 1..8' >&2; exit 2; }
TILED_BATCH=${RADISH_TILED_BATCH_CHUNKS:-1}
[[ $TILED_BATCH =~ ^(1|2|4|8)$ ]] || { echo 'RADISH_TILED_BATCH_CHUNKS must be 1, 2, 4 or 8' >&2; exit 2; }
TILED_GROUPS_X=${RADISH_TILED_GROUPS_X:-1}
[[ $TILED_GROUPS_X == 1 || $TILED_GROUPS_X == 2 ]] || { echo 'RADISH_TILED_GROUPS_X must be 1 or 2' >&2; exit 2; }
EXTRA_ENV=
TAG=operators
TILED_ONLY=0
PADDED_TILED_ONLY=0
RESHAPE_ONLY=0
NORM_SUM4_ONLY=0
NORM_OUTPLACE_ONLY=0
COMPILE_ONLY=0
OTHER_SELECTION=0
DILATION=1
for arg in "$@"; do
    case $arg in
        --conv|--inner|--smoke|--fp16-packed) TAG+=-${arg#--}; OTHER_SELECTION=1 ;;
        --tiled-only) TAG+=-tiled-only; TILED_ONLY=1 ;;
        --padded-tiled-only) TAG+=-padded-tiled-only; PADDED_TILED_ONLY=1 ;;
        --reshape-only) TAG+=-reshape-only; RESHAPE_ONLY=1 ;;
        --norm-sum4-only) TAG+=-norm-sum4-only; NORM_SUM4_ONLY=1 ;;
        --norm-outplace-only)
            [[ $NORM_OUTPLACE_ONLY == 0 ]] || { echo 'select --norm-outplace-only once' >&2; exit 2; }
            TAG+=-norm-outplace-only; NORM_OUTPLACE_ONLY=1 ;;
        --dilation3|--dilation5)
            [[ $DILATION == 1 ]] || { echo 'select at most one of --dilation3 or --dilation5' >&2; exit 2; }
            DILATION=${arg#--dilation}; TAG+=-dilation$DILATION ;;
        --compile-only) TAG+=-compile; COMPILE_ONLY=1; EXTRA_ENV='--setenv=IR3_SHADER_DEBUG=disasm' ;;
        *) echo 'usage: device-operators.sh [--conv] [--inner] [--compile-only] [--smoke] [--fp16-packed] | --tiled-only | --padded-tiled-only [--compile-only] [--dilation3 | --dilation5] | --reshape-only | --norm-sum4-only | --norm-outplace-only [--compile-only]' >&2; exit 2 ;;
    esac
done
if [[ $DILATION != 1 && $TILED_ONLY != 1 && $PADDED_TILED_ONLY != 1 ]]; then
    echo '--dilation3 and --dilation5 require --tiled-only or --padded-tiled-only' >&2
    exit 2
fi
if [[ ( $TILED_ONLY == 1 && $PADDED_TILED_ONLY == 1 ) || ( ( $TILED_ONLY == 1 || $PADDED_TILED_ONLY == 1 ) && $OTHER_SELECTION == 1 ) ]]; then
    echo '--tiled-only and --padded-tiled-only select one FP32 case and cannot be combined with another case selector' >&2
    exit 2
fi
if [[ $RESHAPE_ONLY == 1 && ( $OTHER_SELECTION == 1 || $TILED_ONLY == 1 || $PADDED_TILED_ONLY == 1 || $DILATION != 1 ) ]]; then
    echo '--reshape-only is FP32 and cannot be combined with another case selector' >&2
    exit 2
fi
if [[ $NORM_SUM4_ONLY == 1 && ( $OTHER_SELECTION == 1 || $TILED_ONLY == 1 || $PADDED_TILED_ONLY == 1 || $RESHAPE_ONLY == 1 || $DILATION != 1 ) ]]; then
    echo '--norm-sum4-only is FP32 and cannot be combined with another case selector' >&2
    exit 2
fi
if [[ $NORM_OUTPLACE_ONLY == 1 && ( $OTHER_SELECTION == 1 || $TILED_ONLY == 1 || $PADDED_TILED_ONLY == 1 || $RESHAPE_ONLY == 1 || $NORM_SUM4_ONLY == 1 || $DILATION != 1 ) ]]; then
    echo '--norm-outplace-only is FP32 and cannot be combined with another case selector' >&2
    exit 2
fi
if [[ ( $NORM_OUTPLACE == 1 || $NORM_OUTPLACE_ONLY == 1 ) && ( $NORM_SUM4 != 1 || $RESHAPE_ALIAS != 1 ) ]]; then
    echo 'norm outplace mode requires RADISH_NCNN_NORM_SUM4=1 and RADISH_NCNN_RESHAPE_ALIAS=1' >&2
    exit 2
fi
EXTRA_ENV+=" --setenv=RADISH_NCNN_SHARED32=$SHARED32 --setenv=RADISH_NCNN_TILED=$TILED --setenv=RADISH_NCNN_TILED32=$TILED32 --setenv=RADISH_NCNN_PLANAR=$PLANAR --setenv=RADISH_NCNN_PLANAR_PACKED_A=$PACKED_A --setenv=RADISH_NCNN_NORM_SUM4=$NORM_SUM4 --setenv=RADISH_NCNN_NORM_OUTPLACE=$NORM_OUTPLACE --setenv=RADISH_NCNN_RESHAPE_ALIAS=$RESHAPE_ALIAS --setenv=RADISH_SHARED_GROUPS=$SHARED_GROUPS --setenv=RADISH_TILED_BATCH_CHUNKS=$TILED_BATCH --setenv=RADISH_TILED_GROUPS_X=$TILED_GROUPS_X --setenv=RADISH_PROFILE=1"
TAG+=-tiled-batch$TILED_BATCH
[[ $SHARED32 == 0 ]] || TAG+=-shared32
[[ $TILED == 0 ]] || TAG+=-tiled
[[ $TILED32 == 0 ]] || TAG+=-tiled32
[[ $PLANAR == 0 ]] || TAG+=-planar
[[ $PACKED_A == 0 ]] || TAG+=-packed-a
[[ $NORM_SUM4 == 0 ]] || TAG+=-norm-sum4
[[ $NORM_OUTPLACE == 0 ]] || TAG+=-norm-outplace
[[ $TILED_GROUPS_X == 1 ]] || TAG+=-tiled-groups-x$TILED_GROUPS_X
[[ $RESHAPE_ALIAS == 0 ]] || TAG+=-reshape-alias
[[ $SHARED_GROUPS == 1 ]] || TAG+=-shared-groups$SHARED_GROUPS
NORM_DUMP=
if [[ ( $NORM_SUM4_ONLY == 1 || $NORM_OUTPLACE_ONLY == 1 ) && $COMPILE_ONLY == 0 ]]; then
    NORM_DUMP=/var/tmp/radish/$TAG-$(date +%s%N).f32
    EXTRA_ENV+=" --setenv=RADISH_NCNN_NORM_OUTPUT=$NORM_DUMP"
fi
[[ $PACKED_A == 0 ]] || echo 'Radish controls: planar_packed_a=1'
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
if [[ -n $NORM_DUMP ]]; then rm -f -- "$WORK/evidence/$TAG-output.f32"; fi
"${ADB[@]}" shell "chmod 755 /var/tmp/radish/radish-operators; systemd-run --quiet --wait --pipe --collect -p MemoryMax=384M -p RuntimeMaxSec=90 -p LimitCORE=0 $EXTRA_ENV --setenv=LD_LIBRARY_PATH=/var/tmp/radish --setenv=VK_DRIVER_FILES=/var/tmp/radish/radish_icd.json /var/tmp/radish/radish-operators $*" 2>&1 | tee "$WORK/evidence/$TAG.txt"
if rg -n 'vkCreateComputePipelines failed|new_pipeline failed' "$WORK/evidence/$TAG.txt"; then exit 1; fi
if [[ -n $NORM_DUMP ]]; then
    NORM_STAGE=$(mktemp "$WORK/evidence/$TAG-output.f32.XXXXXX")
    "${ADB[@]}" pull "$NORM_DUMP" "$NORM_STAGE"
    python3 - "$NORM_STAGE" <<'PY'
import math, pathlib, struct, sys
raw=pathlib.Path(sys.argv[1]).read_bytes()
if len(raw)!=36606*4 or not all(math.isfinite(x[0]) for x in struct.iter_unpack('<f',raw)):
    raise ValueError('invalid normalization probe output')
PY
    mv -- "$NORM_STAGE" "$WORK/evidence/$TAG-output.f32"
    sha256sum "$WORK/evidence/$TAG-output.f32"
fi
