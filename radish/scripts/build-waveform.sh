#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
OUTPUT=${RADISH_WAVEFORM_OUTPUT:-$WORK/kokoro-waveform}
SYSROOT=${RADISH_SYSROOT:-$TOP/streams/gpu-mesa16/work/sysroot}
GCC_INSTALL=${RADISH_GCC_INSTALL:-$SYSROOT/usr/lib/gcc/aarch64-linux-gnu/16}
GCC_INSTALL=$(realpath -e "$GCC_INSTALL")
clang++ --target=aarch64-linux-gnu --sysroot="$SYSROOT" \
    --gcc-install-dir="$GCC_INSTALL" -mcpu=cortex-a53 -std=c++11 -O2 \
    -ffp-contract=off -Wall -Wextra -Werror -fuse-ld=lld --rtlib=libgcc \
    -L"$SYSROOT/usr/lib" -Wl,-rpath-link,"$SYSROOT/usr/lib" \
    "$TOP/radish/tests/kokoro_waveform.cpp" -o "$OUTPUT"
