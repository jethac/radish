#!/usr/bin/env bash
# Build the driver from the radish/main checkout and install it into the
# tablet's private prefix as lib/libvulkan_radish-<sha12>.so with a matching
# ICD manifest lib/icd-<sha12>.json. Prints the ICD path. System Mesa is not
# touched; selection is per process with VK_DRIVER_FILES.
set -euo pipefail
. "$(dirname "$0")/common.sh"
PATH=/home/jethac/Projects/lenovo-8/streams/gpu-mesa16/work/venv/bin:$PATH \
    ninja -C "$SRC/build" src/freedreno/radish/libvulkan_radish.so >&2
mkdir -p "$SRC/deploy"
llvm-strip --strip-debug -o "$SRC/deploy/libvulkan_radish.so" "$SRC/build/src/freedreno/radish/libvulkan_radish.so"
SHA=$(sha256sum "$SRC/deploy/libvulkan_radish.so" | cut -c1-12)
LIB=$PREFIX/lib/libvulkan_radish-$SHA.so
ICD=$PREFIX/lib/icd-$SHA.json
printf '{"file_format_version":"1.0.0","ICD":{"library_path":"%s","api_version":"1.0.0"}}\n' "$LIB" > "$SRC/deploy/icd.json"
flock -w 1800 "$LOCK" bash -c "
    adb -s $SERIAL shell 'mkdir -p $PREFIX/lib && test -f $LIB' 2>/dev/null || {
        adb -s $SERIAL push -q '$SRC/deploy/libvulkan_radish.so' '$LIB.tmp' >&2 &&
        adb -s $SERIAL shell 'mv $LIB.tmp $LIB'; }
    adb -s $SERIAL push -q '$SRC/deploy/icd.json' '$ICD' >&2
    adb -s $SERIAL shell 'sha256sum $LIB' >&2"
echo "$ICD"
