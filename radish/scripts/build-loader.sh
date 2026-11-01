#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SYSROOT=${RADISH_SYSROOT:-$TOP/streams/gpu-mesa16/work/sysroot}
mkdir -p "$WORK"
fetch_pinned() {
    local dir=$1 url=$2 revision=$3
    if [[ ! -d $dir/.git ]]; then
        git init "$dir"
        git -C "$dir" remote add origin "$url"
        git -C "$dir" fetch --depth 1 origin "$revision"
        git -C "$dir" checkout --detach FETCH_HEAD
    fi
    [[ $(git -C "$dir" rev-parse HEAD) == "$revision" ]] || {
        echo "Unexpected revision in $dir; preserve it and set RADISH_WORK to a fresh directory" >&2
        exit 1
    }
}
fetch_pinned "$WORK/vulkan-loader" git@github.com:KhronosGroup/Vulkan-Loader.git aa6dee8385afe4b2cc0152d086af2184743e16fd
fetch_pinned "$WORK/vulkan-headers" git@github.com:KhronosGroup/Vulkan-Headers.git 9fbc2a782ee3f8a4db64e7d991e23da75e893f44
cmake -S "$WORK/vulkan-headers" -B "$WORK/build-headers" \
    -DCMAKE_INSTALL_PREFIX="$WORK/headers" -DVULKAN_HEADERS_ENABLE_TESTS=OFF
cmake --install "$WORK/build-headers"
cat > "$WORK/loader-toolchain.cmake" <<CMAKE
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_SYSROOT $SYSROOT)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a53")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a53")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld --rtlib=libgcc -L$SYSROOT/usr/lib -Wl,-rpath-link,$SYSROOT/usr/lib")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld --rtlib=libgcc -L$SYSROOT/usr/lib -Wl,-rpath-link,$SYSROOT/usr/lib")
CMAKE
cmake -G Ninja -S "$WORK/vulkan-loader" -B "$WORK/build-loader" \
    -DCMAKE_TOOLCHAIN_FILE="$WORK/loader-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DVulkanHeaders_DIR="$WORK/headers/share/cmake/VulkanHeaders" \
    -DBUILD_TESTS=OFF -DBUILD_WSI_XCB_SUPPORT=OFF -DBUILD_WSI_XLIB_SUPPORT=OFF \
    -DBUILD_WSI_WAYLAND_SUPPORT=OFF
cmake --build "$WORK/build-loader" -j"${RADISH_JOBS:-6}"
