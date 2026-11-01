#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
exec 9>"${RADISH_DEVICE_LOCK:-$TOP/.device.lock}"
flock -w 1800 9
ADB=(adb -s "$SERIAL")
mkdir -p "$WORK/evidence"
cat > "$WORK/radish_icd.json" <<'JSON'
{"file_format_version":"1.0.0","ICD":{"library_path":"/var/tmp/radish/libvulkan_radish.so","api_version":"1.0.0"}}
JSON
"${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice; mkdir -p /var/tmp/radish'
"${ADB[@]}" shell dmesg > "$WORK/evidence/loader-dmesg-before.txt"
finish() {
    local result=$?
    trap - EXIT
    "${ADB[@]}" shell dmesg > "$WORK/evidence/loader-dmesg-after.txt" || result=1
    "${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice' || result=1
    python3 "$TOP/radish/scripts/check-gpu-log.py" \
        "$WORK/evidence/loader-dmesg-before.txt" \
        "$WORK/evidence/loader-dmesg-after.txt" || result=1
    exit "$result"
}
trap finish EXIT
"${ADB[@]}" push "$WORK/build-loader/loader/libvulkan.so" /var/tmp/radish/libvulkan.so.1
"${ADB[@]}" push "$WORK/radish_icd.json" /var/tmp/radish/radish_icd.json
"${ADB[@]}" push "$WORK/build/src/freedreno/radish/libvulkan_radish.so" /var/tmp/radish/libvulkan_radish.so
"${ADB[@]}" push "$WORK/lifecycle" /var/tmp/radish/lifecycle
"${ADB[@]}" shell 'chmod 755 /var/tmp/radish/lifecycle; systemd-run --quiet --wait --pipe --collect -p MemoryMax=256M -p RuntimeMaxSec=45 --setenv=RADISH_USE_LOADER=1 --setenv=VK_DRIVER_FILES=/var/tmp/radish/radish_icd.json /var/tmp/radish/lifecycle /var/tmp/radish/libvulkan.so.1' 2>&1 | tee "$WORK/evidence/loader.txt"
