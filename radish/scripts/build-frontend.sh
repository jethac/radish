#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SYSROOT=${RADISH_SYSROOT:-$TOP/streams/gpu-mesa16/work/sysroot}
GCC_INSTALL=${RADISH_GCC_INSTALL:-$SYSROOT/usr/lib/gcc/aarch64-linux-gnu/16}
GCC_INSTALL=$(realpath -e "$GCC_INSTALL")
INCLUDE=$WORK/ort-1.24.4/include
mkdir -p "$INCLUDE"
# Official MIT-licensed API24 headers. The installed1.28.2 runtime supports
# this older API; the helper checks its version and GetApi(24) at startup.
fetch_header() {
    local name=$1 expected=$2 temporary
    if [[ ! -f $INCLUDE/$name ]]; then
        temporary=$(mktemp "$INCLUDE/.header-XXXXXX")
        if ! curl --fail --location --silent --show-error \
            "https://raw.githubusercontent.com/microsoft/onnxruntime/v1.24.4/include/onnxruntime/core/session/$name" \
            --output "$temporary"; then
            rm -f -- "$temporary"
            return 1
        fi
        if [[ $(sha256sum "$temporary" | cut -d ' ' -f 1) != "$expected" ]]; then
            rm -f -- "$temporary"
            echo "incorrect pinned header hash: $name" >&2
            return 1
        fi
        mv -- "$temporary" "$INCLUDE/$name"
    fi
    [[ $(sha256sum "$INCLUDE/$name" | cut -d ' ' -f 1) == "$expected" ]] || {
        echo "incorrect pinned header hash: $name" >&2
        return 1
    }
}
fetch_header onnxruntime_c_api.h 9ed0d7054a4e74249467365b25b415d36f51a44a6349e2a994a1812e4723d1e2
fetch_header onnxruntime_ep_c_api.h e94ac3490697bdb107844fa9d403952ee7136134132a057eae5c1207b57ce352
clang++ --target=aarch64-linux-gnu --sysroot="$SYSROOT" \
    --gcc-install-dir="$GCC_INSTALL" -mcpu=cortex-a53 -std=c++11 -O2 \
    -ffp-contract=off -Wall -Wextra -Werror -fuse-ld=lld --rtlib=libgcc \
    -I"$INCLUDE" -L"$SYSROOT/usr/lib" -Wl,-rpath-link,"$SYSROOT/usr/lib" \
    "$TOP/radish/tests/kokoro_frontend.cpp" -ldl -o "$WORK/kokoro-frontend"
