#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
MODE=${1:-loop}
RUN=${2:-}
case $MODE in loop|array|div|array-div|loop-long|loop-load) ;; *) echo 'invalid control mode' >&2; exit 2 ;; esac
case $RUN in ''|--compile-only|--smoke) ;; *) echo 'invalid control run option' >&2; exit 2 ;; esac
ENV_ARGS=
TAG=control-$MODE
if [[ $RUN == --compile-only ]]; then
    ENV_ARGS='--setenv=RADISH_CONTROL_COMPILE_ONLY=1 --setenv=IR3_SHADER_DEBUG=disasm'
    TAG+=-compile
elif [[ $RUN == --smoke ]]; then
    ENV_ARGS='--setenv=RADISH_CONTROL_SMOKE=1'
    TAG+=-smoke
fi
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
"${ADB[@]}" push "$WORK/control" /var/tmp/radish/control
"${ADB[@]}" push "$WORK/control-$MODE.spv" /var/tmp/radish/control.spv
"${ADB[@]}" shell 'chmod 755 /var/tmp/radish/control'
"${ADB[@]}" shell "systemd-run --quiet --wait --pipe --collect -p MemoryMax=384M -p RuntimeMaxSec=45 -p LimitCORE=0 $ENV_ARGS /var/tmp/radish/control /var/tmp/radish/libvulkan_radish.so /var/tmp/radish/control.spv $MODE" 2>&1 | tee "$WORK/evidence/$TAG.txt"
