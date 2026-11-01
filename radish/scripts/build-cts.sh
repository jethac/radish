#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SRC=${RADISH_CTS_SOURCE:-$WORK/downloads/vk-gl-cts}
BUILD=${RADISH_CTS_BUILD:-$WORK/build-cts}
SYSROOT=${RADISH_SYSROOT:-$TOP/streams/gpu-mesa16/work/sysroot}
TOOLCHAIN=${RADISH_CTS_TOOLCHAIN:-$WORK/loader-toolchain.cmake}
GCC_INSTALL=${RADISH_GCC_INSTALL:-$SYSROOT/usr/lib/gcc/aarch64-linux-gnu/16}
JOBS=${RADISH_JOBS:-4}
if [[ $# -gt 1 || ($# -eq 1 && $1 != --fetch-only) ]]; then
    echo "usage: $0 [--fetch-only]" >&2
    exit 2
fi
[[ $JOBS =~ ^[1-9][0-9]*$ ]] || { echo 'RADISH_JOBS must be a positive integer' >&2; exit 2; }
[[ -f $TOOLCHAIN ]] || { echo "Missing toolchain: $TOOLCHAIN" >&2; exit 1; }
GCC_INSTALL=$(realpath -e "$GCC_INSTALL")
mkdir -p "$BUILD"
exec 9>"$BUILD/.build-cts.lock"
flock -n 9 || { echo "Another CTS preparation/build owns $BUILD" >&2; exit 1; }
python3 "$TOP/radish/scripts/fetch-cts-deps.py" --source "$SRC" --build "$BUILD"
if [[ ${1:-} == --fetch-only ]]; then exit 0; fi
run_logged() {
    local log=$1
    shift
    printf '%q ' "$@" >> "$BUILD/commands.log"
    printf '\n' >> "$BUILD/commands.log"
    "$@" 2>&1 | tee -a "$log"
}
{
    clang --version
    cmake --version
    ninja --version
    python3 --version
    sha256sum "$TOOLCHAIN"
} > "$BUILD/tool-versions.log"
run_logged "$BUILD/configure.log" cmake -G Ninja -S "$SRC" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDEQP_TARGET=vulkan_headless \
    -DDE_CPU=DE_CPU_ARM_64 -DSELECTED_BUILD_TARGETS=deqp-vk \
    -DDEQP_DISABLE_VK_VIDEO_TESTS=ON \
    -DCMAKE_C_FLAGS="-mcpu=cortex-a53 --gcc-install-dir=$GCC_INSTALL" \
    -DCMAKE_CXX_FLAGS="-mcpu=cortex-a53 --gcc-install-dir=$GCC_INSTALL" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
TIMEFORMAT='elapsed_seconds=%R user_seconds=%U system_seconds=%S'
{ time run_logged "$BUILD/build.log" nice -n 10 cmake --build "$BUILD" --target deqp-vk -j"$JOBS"; } \
    2>> "$BUILD/build-resource.log"
sha256sum "$BUILD/external/vulkancts/modules/vulkan/deqp-vk" | tee "$BUILD/BINARY.sha256"
