#!/usr/bin/env bash
main() {
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
MODEL_BINARY=${RADISH_MODEL_BINARY:-$WORK/build-ncnn/radish-model}
CACHE=${RADISH_NCNN_PIPELINE_CACHE:-1}
[[ $CACHE == 0 || $CACHE == 1 ]] || { echo 'RADISH_NCNN_PIPELINE_CACHE must be 0 or 1' >&2; exit 2; }
TRACE=${RADISH_NCNN_TRACE:-1}
PROFILE=${RADISH_PROFILE:-0}
SHARED32=${RADISH_NCNN_SHARED32:-0}
TILED=${RADISH_NCNN_TILED:-0}
MIXED_STORAGE=${RADISH_NCNN_MIXED_STORAGE:-0}
RESHAPE_ALIAS=${RADISH_NCNN_RESHAPE_ALIAS:-0}
BATCH=${RADISH_BATCH_CHUNKS:-1}
[[ $BATCH =~ ^[1-4]$ ]] || { echo 'RADISH_BATCH_CHUNKS must be 1..4' >&2; exit 2; }
SHARED_GROUPS=${RADISH_SHARED_GROUPS:-1}
[[ $SHARED_GROUPS =~ ^[1-8]$ ]] || { echo 'RADISH_SHARED_GROUPS must be 1..8' >&2; exit 2; }
for toggle in "$TRACE" "$PROFILE" "$SHARED32" "$TILED" "$MIXED_STORAGE" "$RESHAPE_ALIAS"; do
    [[ $toggle == 0 || $toggle == 1 ]] || { echo 'trace/profile flags must be 0 or 1' >&2; exit 2; }
done
KIND=${1:-}
[[ $KIND =~ ^[A-Za-z0-9_-]+$ ]] || { echo 'invalid fixture name' >&2; exit 2; }
case $KIND in
    style) SOURCE=$WORK/kokoro-style; MEMORY=384; RUNTIME=90; MODEL_ARGS=--style
        FILES=(decoder.ncnn.param decoder.ncnn.bin style.f32 reference.f32) ;;
    core|core-layouts|core-shapes|core-shapes-norm32|core-shapes-instancenorm|runtime-*) SOURCE=$WORK/kokoro-core-fixed; MEMORY=640; RUNTIME=300; MODEL_ARGS=
        [[ $KIND == core ]] || SOURCE=$WORK/kokoro-$KIND
        FILES=(decoder.ncnn.param decoder.ncnn.bin asr.f32 f0.f32 n.f32 style.f32 har.f32 reference.f32) ;;
    *) echo 'usage: device-model.sh style|core|core-layouts|core-shapes [--cpu|--cpu-packed|--vulkan] [--fp16-packed] [--threads N] [--load-only] [--repeat N] [--blob NAME]' >&2; exit 2 ;;
esac
shift
if [[ $KIND == runtime-* ]]; then
    test -f "$SOURCE/runtime-ncnn.json" || { echo 'runtime fixture assembly has not passed validation' >&2; exit 2; }
fi
BACKEND=vulkan
CPU_PACKED=false
CPU_REQUESTED=false
FP16_PACKED=false
PACKED_CANDIDATE_READY=false
LOAD_ONLY=false
REPEAT=1
THREADS=4
BLOB=
while [[ $# -gt 0 ]]; do
    case $1 in
        --cpu) BACKEND=cpu; CPU_PACKED=false; CPU_REQUESTED=true ;;
        --cpu-packed) BACKEND=cpu; CPU_PACKED=true; CPU_REQUESTED=true ;;
        --vulkan) BACKEND=vulkan; CPU_PACKED=false ;;
        --fp16-packed) FP16_PACKED=true ;;
        --load-only) LOAD_ONLY=true ;;
        --blob)
            [[ $# -ge 2 && -z $BLOB && $2 =~ ^[A-Za-z0-9_.-]+$ && $2 != --* ]] || {
                echo '--blob requires one name using letters, digits, underscore, dot or hyphen' >&2
                exit 2
            }
            BLOB=$2
            shift ;;
        --repeat)
            [[ $# -ge 2 && $2 =~ ^([1-9]|10)$ ]] || { echo '--repeat requires 1..10' >&2; exit 2; }
            REPEAT=$2
            shift ;;
        --threads)
            [[ $# -ge 2 && $2 =~ ^[1-6]$ ]] || { echo '--threads requires 1..6' >&2; exit 2; }
            THREADS=$2
            shift ;;
        *) echo "unsupported argument: $1" >&2; exit 2 ;;
    esac
    shift
done
if [[ -n $BLOB ]] && { $LOAD_ONLY || [[ $REPEAT != 1 ]]; }; then
    echo '--blob is diagnostic only and cannot be combined with --load-only or --repeat greater than 1' >&2
    exit 2
fi
if $FP16_PACKED && { [[ $BACKEND != vulkan ]] || $CPU_REQUESTED; }; then
    echo '--fp16-packed cannot be combined with --cpu or --cpu-packed' >&2
    exit 2
fi
if $LOAD_ONLY && [[ $REPEAT != 1 ]]; then
    echo '--load-only requires --repeat 1' >&2
    exit 2
fi
MODEL_ARGS+=" --repeat $REPEAT --threads $THREADS"
[[ $BACKEND == cpu ]] || MODEL_ARGS+=' --vulkan'
OUTPUT_BACKEND=$BACKEND
if $CPU_PACKED; then
    MODEL_ARGS+=' --cpu-packed'
    OUTPUT_BACKEND=cpu-packed
fi
if $FP16_PACKED; then
    MODEL_ARGS+=' --fp16-packed'
    OUTPUT_BACKEND=vulkan-fp16-packed
fi
EXTRA_ENV=
TAG=model-$KIND-$OUTPUT_BACKEND
BLOB_FILE=
if [[ -n $BLOB ]]; then
    TAG+=-diagnostic-blob-$BLOB
    BLOB_FILE=ncnn-blob-$BLOB.f32
    if $FP16_PACKED; then BLOB_FILE=ncnn-vulkan-fp16-packed-blob-$BLOB.f32; fi
fi
if $LOAD_ONLY; then
    MODEL_ARGS+=' --load-only'
    TAG+=-load
    [[ $BACKEND != vulkan ]] || EXTRA_ENV=--setenv=IR3_SHADER_DEBUG=disasm
fi
if [[ -n ${RADISH_MODEL_RUN:-} ]]; then
    [[ $RADISH_MODEL_RUN =~ ^[a-zA-Z0-9_-]+$ ]] || { echo 'invalid run label' >&2; exit 2; }
    TAG+=-$RADISH_MODEL_RUN
fi
UNIT=radish-$TAG-$(date +%s%N)
for file in "${FILES[@]}"; do test -f "$SOURCE/$file"; done
test -f "$MODEL_BINARY"
sha256sum "$MODEL_BINARY" "$WORK/build/src/freedreno/radish/libvulkan_radish.so" "$SOURCE/decoder.ncnn.param"
echo "Radish controls: batch=$BATCH shared_groups=$SHARED_GROUPS shared32=$SHARED32 tiled=$TILED mixed_storage=$MIXED_STORAGE reshape_alias=$RESHAPE_ALIAS"
exec 9>"${RADISH_DEVICE_LOCK:-$TOP/.device.lock}"
flock -w 1800 9
ADB=(adb -s "$SERIAL")
mkdir -p "$WORK/evidence"
"${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice'
"${ADB[@]}" shell dmesg > "$WORK/evidence/$TAG-dmesg-before.txt"
"${ADB[@]}" shell cat /proc/meminfo > "$WORK/evidence/$TAG-mem-before.txt"
thermal_snapshot() {
    "${ADB[@]}" shell 'for d in /sys/class/devfreq/*; do for n in name cur_freq governor min_freq max_freq trans_stat; do if test -r "$d/$n"; then printf "%s=" "$d/$n"; cat "$d/$n"; fi; done; done; for z in /sys/class/thermal/thermal_zone*; do for n in type temp; do if test -r "$z/$n"; then printf "%s=" "$z/$n"; cat "$z/$n"; fi; done; done'
}
thermal_snapshot > "$WORK/evidence/$TAG-thermal-before.txt"
AVAILABLE=$(awk '$1 == "MemAvailable:" {print $2}' "$WORK/evidence/$TAG-mem-before.txt")
[[ $AVAILABLE =~ ^[0-9]+$ && $AVAILABLE -ge $(((MEMORY + 192) * 1024)) ]] || {
    echo "insufficient available memory for ${MEMORY}MiB process cap plus 192MiB reserve" >&2
    exit 1
}
finish() {
    local result=$?
    trap - EXIT
    "${ADB[@]}" shell dmesg > "$WORK/evidence/$TAG-dmesg-after.txt" || result=1
    "${ADB[@]}" shell cat /proc/meminfo > "$WORK/evidence/$TAG-mem-after.txt" || result=1
    thermal_snapshot > "$WORK/evidence/$TAG-thermal-after.txt" || result=1
    "${ADB[@]}" shell "journalctl --no-pager -u $UNIT" > "$WORK/evidence/$TAG-service.txt" || result=1
    "${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice' || result=1
    python3 "$TOP/radish/scripts/check-gpu-log.py" \
        "$WORK/evidence/$TAG-dmesg-before.txt" \
        "$WORK/evidence/$TAG-dmesg-after.txt" || result=1
    if $PACKED_CANDIDATE_READY; then
        "${ADB[@]}" pull "/var/tmp/radish/kokoro-$KIND/ncnn-vulkan-fp16-packed-candidate.f32" \
            "$WORK/evidence/$TAG-candidate.f32" || result=1
    fi
    exit "$result"
}
trap finish EXIT
"${ADB[@]}" shell "mkdir -p /var/tmp/radish/kokoro-$KIND"
for file in "${FILES[@]}"; do
    "${ADB[@]}" push --sync "$SOURCE/$file" "/var/tmp/radish/kokoro-$KIND/$file"
done
"${ADB[@]}" push "$MODEL_BINARY" /var/tmp/radish/radish-model
"${ADB[@]}" push "$WORK/build/src/freedreno/radish/libvulkan_radish.so" /var/tmp/radish/libvulkan_radish.so
if $FP16_PACKED && ! $LOAD_ONLY && [[ -z $BLOB ]]; then
    rm -f "$WORK/evidence/$TAG-candidate.f32"
    "${ADB[@]}" shell "rm -f /var/tmp/radish/kokoro-$KIND/ncnn-vulkan-fp16-packed-candidate.f32"
    PACKED_CANDIDATE_READY=true
fi
if [[ -n $BLOB ]]; then
    rm -f "$WORK/evidence/$TAG.f32"
    "${ADB[@]}" shell "rm -f /var/tmp/radish/kokoro-$KIND/$BLOB_FILE"
fi
{
    if [[ -n $BLOB ]]; then
        echo "DIAGNOSTIC: extracting blob $BLOB; no full-model accuracy validation is performed"
    fi
    "${ADB[@]}" shell "chmod 755 /var/tmp/radish/radish-model; systemd-run --unit=$UNIT --wait --pipe --collect -p MemoryMax=${MEMORY}M -p MemorySwapMax=128M -p RuntimeMaxSec=$RUNTIME -p LimitCORE=0 $EXTRA_ENV --setenv=RADISH_NCNN_TRACE=$TRACE --setenv=RADISH_NCNN_PIPELINE_CACHE=$CACHE --setenv=RADISH_NCNN_SHARED32=$SHARED32 --setenv=RADISH_NCNN_TILED=$TILED --setenv=RADISH_NCNN_MIXED_STORAGE=$MIXED_STORAGE --setenv=RADISH_NCNN_RESHAPE_ALIAS=$RESHAPE_ALIAS --setenv=RADISH_PROFILE=$PROFILE --setenv=RADISH_BATCH_CHUNKS=$BATCH --setenv=RADISH_SHARED_GROUPS=$SHARED_GROUPS --setenv=LD_LIBRARY_PATH=/var/tmp/radish --setenv=VK_DRIVER_FILES=/var/tmp/radish/radish_icd.json /var/tmp/radish/radish-model $MODEL_ARGS /var/tmp/radish/kokoro-$KIND $BLOB"
} 2>&1 | tee "$WORK/evidence/$TAG.txt"
if rg -n 'vkCreateComputePipelines failed|new_pipeline failed' "$WORK/evidence/$TAG.txt"; then exit 1; fi
if [[ -n $BLOB ]]; then
    "${ADB[@]}" pull "/var/tmp/radish/kokoro-$KIND/$BLOB_FILE" "$WORK/evidence/$TAG.f32"
elif ! $LOAD_ONLY; then
    "${ADB[@]}" pull "/var/tmp/radish/kokoro-$KIND/ncnn-$OUTPUT_BACKEND-output.f32" "$SOURCE/ncnn-device-$OUTPUT_BACKEND-output.f32"
    cp -- "$SOURCE/ncnn-device-$OUTPUT_BACKEND-output.f32" "$WORK/evidence/$TAG-output.f32"
fi
}
main "$@"
