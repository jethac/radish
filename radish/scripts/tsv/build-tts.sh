#!/usr/bin/env bash
# Build radish-kokoro-runtime (and the ncnn probes) from the canonical ncnn
# checkout. Output: $SRC/build-tts/radish-kokoro-runtime
set -euo pipefail
. "$(dirname "$0")/common.sh"
TOP=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD:-$SRC/build-tts}
cmake -G Ninja -S "$TOP/tests/ncnn" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$TOP/cmake/aarch64-tsv.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DNCNN_SOURCE="$SRC/ncnn" -DRADISH_ORT_INCLUDE="$SRC/ort-include" \
    -DNCNN_VULKAN=ON -DNCNN_SIMPLEVK=ON -DNCNN_OPENMP=ON -DNCNN_SIMPLEOMP=ON -DNCNN_THREADS=ON \
    -DNCNN_ARM82=OFF -DNCNN_ARM82DOT=OFF -DNCNN_ARM82FP16FML=OFF -DNCNN_ARM84BF16=OFF \
    -DNCNN_ARM84I8MM=OFF -DNCNN_ARM86SVE=OFF -DNCNN_RUNTIME_CPU=OFF -DNCNN_BUILD_TESTS=OFF \
    -DNCNN_BUILD_TOOLS=OFF -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BUILD_BENCHMARK=OFF -DNCNN_BENCHMARK=OFF >/dev/null
cmake --build "$BUILD" -j"${JOBS:-12}" --target radish-kokoro-runtime "$@"
