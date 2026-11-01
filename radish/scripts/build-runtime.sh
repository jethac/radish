#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
BUILD=${RADISH_NCNN_BUILD_DIR:-$WORK/build-ncnn-simpleomp}
INCLUDE=$WORK/ort-1.24.4/include
# Fetch and verify the same API headers as the standalone frontend.
"$TOP/radish/scripts/build-frontend.sh"
RADISH_NCNN_BUILD_DIR="$BUILD" RADISH_NCNN_SIMPLEOMP=ON "$TOP/radish/scripts/build-ncnn.sh"
cmake -S "$TOP/radish/tests/ncnn" -B "$BUILD" -DRADISH_ORT_INCLUDE="$INCLUDE"
cmake --build "$BUILD" -j"${RADISH_JOBS:-6}" --target radish-kokoro-runtime
