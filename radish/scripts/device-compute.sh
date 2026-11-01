#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
BATCH=${RADISH_BATCH_CHUNKS:-1}
[[ $BATCH =~ ^([1-4]|8)$ ]] || { echo 'RADISH_BATCH_CHUNKS must be 1, 2, 3, 4 or 8' >&2; exit 2; }
SHARED_GROUPS=${RADISH_SHARED_GROUPS:-1}
[[ $SHARED_GROUPS =~ ^[1-8]$ ]] || { echo 'RADISH_SHARED_GROUPS must be 1..8' >&2; exit 2; }
SMOKE=0
COMPILE_ONLY=0
COHERENT=0
CHAIN=0
SHARED=0
SHARED_SMALL=0
SHARED_VARIANT=
SHARED_REJECT=0
MIX_ROUNDS=
MIX_SHADER=
SPAN_MODE=
if [[ ${1:-} == --smoke ]]; then SMOKE=1; fi
if [[ ${1:-} == --compile-only ]]; then COMPILE_ONLY=1; fi
if [[ ${1:-} == --write-span ]]; then SPAN_MODE=WRITE; fi
if [[ ${1:-} == --copy-span ]]; then SPAN_MODE=COPY; fi
if [[ ${1:-} == --chain ]]; then CHAIN=1; fi
if [[ ${1:-} == --coherent ]]; then COHERENT=1; fi
if [[ ${1:-} == --shared ]]; then SHARED=1; fi
if [[ ${1:-} == --shared-compile ]]; then SHARED=1; COMPILE_ONLY=1; fi
if [[ ${1:-} == --shared-small ]]; then SHARED=1; SHARED_SMALL=1; fi
if [[ ${1:-} == --shared-low ]]; then SHARED=1; SHARED_VARIANT=low; fi
if [[ ${1:-} == --shared-mid ]]; then SHARED=1; SHARED_VARIANT=mid; fi
if [[ ${1:-} == --shared-reject ]]; then SHARED=1; SHARED_REJECT=1; fi
case ${1:-} in
    --short-shared*) SHARED=1; MIX_ROUNDS=0; MIX_SHADER=short-shared ;;
    --long-shared*) SHARED=1; MIX_ROUNDS=128; MIX_SHADER=long-shared ;;
esac
if [[ -n $MIX_SHADER && ${1:-} == *-compile ]]; then COMPILE_ONLY=1; fi
if [[ -n $MIX_SHADER && ${1:-} == *-smoke ]]; then SMOKE=1; fi
exec 9>"${RADISH_DEVICE_LOCK:-$TOP/.device.lock}"
flock -w 1800 9
mkdir -p "$WORK/evidence"
ADB=(adb -s "$SERIAL")
TAG=compute
if (( SMOKE )); then TAG=compute-smoke; fi
if (( COMPILE_ONLY )); then TAG=compile; fi
if [[ -n $SPAN_MODE ]]; then TAG=span-$SPAN_MODE; fi
if (( CHAIN )); then TAG=compute-chain; fi
if (( COHERENT )); then TAG=compute-coherent; fi
if (( SHARED )); then TAG=compute-shared-${RADISH_LOCAL_SIZE:-64}; fi
if (( SHARED_SMALL )); then TAG=compute-shared-small-${RADISH_LOCAL_SIZE:-64}; fi
if [[ -n $SHARED_VARIANT ]]; then TAG=compute-shared-$SHARED_VARIANT-${RADISH_LOCAL_SIZE:-64}; fi
if (( SHARED_REJECT )); then TAG=compute-shared-reject; fi
if [[ -n $MIX_SHADER ]]; then TAG=compute${1#-}; fi
[[ $BATCH == 1 ]] || TAG+=-batch$BATCH
[[ $SHARED_GROUPS == 1 ]] || TAG+=-shared-groups$SHARED_GROUPS
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
"${ADB[@]}" push "$WORK/compute" /var/tmp/radish/compute
for shader in index vector-add; do
    suffix=
    if [[ -n $SPAN_MODE ]]; then suffix=-consecutive; fi
    source_shader=$shader$suffix
    if (( SHARED )) && [[ $shader == index ]]; then source_shader=shared-reduce; fi
    if (( SHARED_SMALL )) && [[ $shader == index ]]; then source_shader=shared-reduce-small; fi
    if [[ -n $SHARED_VARIANT && $shader == index ]]; then source_shader=shared-reduce-$SHARED_VARIANT; fi
    if [[ -n $MIX_SHADER && $shader == index ]]; then source_shader=$MIX_SHADER; fi
    "${ADB[@]}" push "$WORK/$source_shader.spv" "/var/tmp/radish/$shader.spv"
done
"${ADB[@]}" shell 'chmod 755 /var/tmp/radish/compute'
ENV_ARG=
if (( SMOKE )); then ENV_ARG='--setenv=RADISH_SMOKE=1'; fi
if (( COMPILE_ONLY )); then ENV_ARG='--setenv=RADISH_COMPILE_ONLY=1 --setenv=IR3_SHADER_DEBUG=disasm'; fi
if [[ -n $SPAN_MODE ]]; then ENV_ARG="--setenv=RADISH_SPAN=1 --setenv=RADISH_${SPAN_MODE}_SPAN=1"; fi
if (( CHAIN )); then ENV_ARG="--setenv=RADISH_CHAIN=1"; fi
if (( COHERENT )); then ENV_ARG="--setenv=RADISH_COHERENT=1"; fi
if (( SHARED )); then ENV_ARG="--setenv=RADISH_SHARED=1 --setenv=RADISH_LOCAL_SIZE=${RADISH_LOCAL_SIZE:-64}"; fi
if (( SHARED && COMPILE_ONLY )); then ENV_ARG+=" --setenv=RADISH_COMPILE_ONLY=1 --setenv=IR3_SHADER_DEBUG=disasm"; fi
if (( SHARED_REJECT )); then ENV_ARG+=" --setenv=RADISH_EXPECT_SHARED_REJECT=1"; fi
if [[ -n $MIX_SHADER ]]; then ENV_ARG+=" --setenv=RADISH_MIX_ROUNDS=$MIX_ROUNDS"; fi
if (( SMOKE )); then ENV_ARG+=" --setenv=RADISH_SMOKE=1"; fi
ENV_ARG+=" --setenv=RADISH_BATCH_CHUNKS=$BATCH --setenv=RADISH_SHARED_GROUPS=$SHARED_GROUPS --setenv=RADISH_PROFILE=1"
"${ADB[@]}" shell "systemd-run --quiet --wait --pipe --collect -p MemoryMax=384M -p RuntimeMaxSec=45 $ENV_ARG /var/tmp/radish/compute /var/tmp/radish/libvulkan_radish.so /var/tmp/radish/index.spv /var/tmp/radish/vector-add.spv" 2>&1 | tee "$WORK/evidence/$TAG.txt"
