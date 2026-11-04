#!/usr/bin/env bash
# Build and run a graphics test (tests/gfx/NAME.c) on the tablet, once,
# with RADISH_GRAPHICS=1 and a 60 s cap; GPU fault/hang lines are printed.
# usage: gfx-test.sh NAME DRIVER_SHA12 [ARGS...]
set -euo pipefail
. "$(dirname "$0")/common.sh"
TOP=$(cd "$(dirname "$0")/../.." && pwd)
NAME=$1 SHA=$2; shift 2
OUT=$SRC/gfx; mkdir -p "$OUT"
SYSROOT=/home/jethac/Projects/lenovo-8/streams/gpu-mesa16/work/sysroot
clang --target=aarch64-linux-gnu --sysroot=$SYSROOT --gcc-install-dir=$(realpath $SYSROOT/usr/lib/gcc/aarch64-linux-gnu/16) \
    -mcpu=cortex-a53 -O2 -Wall -Werror ${GFX_CFLAGS:-} -fuse-ld=lld --rtlib=libgcc -L$SYSROOT/usr/lib \
    -I"$SRC/mesa/include" -I"$TOP/tests/gfx" "$TOP/tests/gfx/$NAME.c" -ldl -lm -o "$OUT/$NAME"
flock -w 1800 "$LOCK" bash -c '
    . "'"$(dirname "$0")"'/common.sh"
    on_baseline || exit 75
    adbsh "mkdir -p '"$PREFIX"'/gfx"
    adb -s $SERIAL push -q '"$OUT/$NAME"' '"$PREFIX"'/gfx/
    before=$(adbsh "dmesg | grep -v audit | grep -iE \"hangcheck|gpu fault|iommu fault|recover\"" || true)
    adbsh "systemd-run --wait --pipe --collect -p MemoryMax=200M -p RuntimeMaxSec=60 -p LimitCORE=0 \
      --setenv=RADISH_GRAPHICS=1 ${GFX_ENV:-} --setenv=LD_LIBRARY_PATH='"$PREFIX"'/lib --setenv=VK_DRIVER_FILES='"$PREFIX"'/lib/icd-'"$SHA"'.json \
      '"$PREFIX"'/gfx/'"$NAME"' '"$*"' 2>&1" || true
    after=$(adbsh "dmesg | grep -v audit | grep -iE \"hangcheck|gpu fault|iommu fault|recover\"" || true)
    diff <(echo "$before") <(echo "$after") | grep "^>" && echo "NEW GPU FAULT/HANG LINES ABOVE" || echo "no new GPU fault lines"
    adbsh "rm -f '"$PREFIX"'/gfx/'"$NAME"'"
'
