#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SRC=${RADISH_NCNN:-$WORK/ncnn}
BUILD=${RADISH_NCNN_BUILD_DIR:-$WORK/build-ncnn}
SIMPLEOMP=${RADISH_NCNN_SIMPLEOMP:-OFF}
case "$SIMPLEOMP" in
    ON|OFF) ;;
    *) echo 'RADISH_NCNN_SIMPLEOMP must be ON or OFF' >&2; exit 1 ;;
esac
SYSROOT=${RADISH_SYSROOT:-$TOP/streams/gpu-mesa16/work/sysroot}
GCC_INSTALL=${RADISH_GCC_INSTALL:-$SYSROOT/usr/lib/gcc/aarch64-linux-gnu/16}
GCC_INSTALL=$(realpath -e "$GCC_INSTALL")
[[ $(git -C "$SRC" rev-parse HEAD) == f18f3ffc94dbc76a277e94556c588ab2c2d0e1d9 ]]
[[ $(git -C "$SRC/glslang" rev-parse HEAD) == 780b6a209132dc74e99458f62ebe5fa7e57c41cf ]]
PATCH=$TOP/radish/patches/0005-ncnn-reduction-workgroup32-cumulative.patch
OLD_PATCH=$TOP/radish/patches/0002-ncnn-reduction-workgroup.patch
if git -C "$SRC" apply --check "$PATCH" 2>/dev/null; then
    git -C "$SRC" apply "$PATCH"
elif git -C "$SRC" apply --check --reverse "$PATCH" 2>/dev/null; then
    :
elif git -C "$SRC" apply --check --reverse "$OLD_PATCH" 2>/dev/null; then
    git -C "$SRC" apply <<'UPGRADE'
diff --git a/src/layer/vulkan/reduction_vulkan.cpp b/src/layer/vulkan/reduction_vulkan.cpp
--- a/src/layer/vulkan/reduction_vulkan.cpp
+++ b/src/layer/vulkan/reduction_vulkan.cpp
@@ -25,2 +25,2 @@
     specializations[0].i = operation;
-    specializations[1].i = opt.use_subgroup_ops ? 256 : 64;
+    specializations[1].i = opt.use_subgroup_ops ? 256 : 32;
UPGRADE
    git -C "$SRC" apply --check --reverse "$PATCH"
else
    echo 'ncnn reduction adaptation conflicts with this checkout; preserve its changes' >&2
    exit 1
fi
for EXTRA_PATCH in "$TOP/radish/patches/0008-ncnn-radish-pipeline-cache.patch" \
                   "$TOP/radish/patches/0019-ncnn-radish-reshape-alias.patch"; do
    if git -C "$SRC" apply --check "$EXTRA_PATCH" 2>/dev/null; then
        git -C "$SRC" apply "$EXTRA_PATCH"
    elif ! git -C "$SRC" apply --check --reverse "$EXTRA_PATCH" 2>/dev/null; then
        echo "ncnn adaptation conflicts with this checkout: $EXTRA_PATCH; preserve its changes" >&2
        exit 1
    fi
done
NET_PATCHES=(
    "$TOP/radish/patches/0006-ncnn-execution-trace.patch"
    "$TOP/radish/patches/0015-ncnn-radish-mixed-storage.patch"
)
NET_STAGE=$(mktemp -d)
trap 'rm -rf -- "$NET_STAGE"' EXIT
mkdir -p "$NET_STAGE/src"
git -C "$SRC" show HEAD:src/net.cpp > "$NET_STAGE/src/net.cpp"
NET_MATCH=-1
for ((stage=0; stage<=${#NET_PATCHES[@]}; stage++)); do
    if cmp -s "$SRC/src/net.cpp" "$NET_STAGE/src/net.cpp"; then NET_MATCH=$stage; fi
    if (( stage<${#NET_PATCHES[@]} )); then
        git -C "$NET_STAGE" apply "${NET_PATCHES[stage]}"
    fi
done
if (( NET_MATCH<0 )); then
    echo 'ncnn net adaptations conflict with the known patch stack; preserve checkout changes' >&2
    exit 1
fi
for ((stage=NET_MATCH; stage<${#NET_PATCHES[@]}; stage++)); do
    git -C "$SRC" apply "${NET_PATCHES[stage]}"
done
rm -rf -- "$NET_STAGE"
trap - EXIT
CONV_PATCHES=(
    "$TOP/radish/patches/0010-ncnn-conv1d-padding-shape-hints.patch"
    "$TOP/radish/patches/0011-ncnn-conv1d-gemm-induction.patch"
    "$TOP/radish/patches/0013-ncnn-conv1d-shared32.patch"
    "$TOP/radish/patches/0014-ncnn-conv1d-a5xx-tiled.patch"
    "$TOP/radish/patches/0017-ncnn-conv1d-shared-a-transpose.patch"
    "$TOP/radish/patches/0018-ncnn-conv1d-tiled-dilation.patch"
    "$TOP/radish/patches/0020-ncnn-conv1d-tiled32.patch"
    "$TOP/radish/patches/0021-ncnn-conv1d-tile32x32.patch"
    "$TOP/radish/patches/0024-ncnn-conv1d-planar-input.patch"
)
CONV_FILES=(src/layer/vulkan/convolution1d_vulkan.cpp src/layer/vulkan/convolution1d_vulkan.h
            src/layer/vulkan/shader/convolution1d_packed_gemm.comp
            src/layer/vulkan/shader/convolution1d_radish_tiled.comp
            src/layer/vulkan/shader/convolution1d_radish_tiled32.comp
            src/layer/vulkan/shader/convolution1d_radish_planar.comp
            src/layer/vulkan/shader/convolution1d_radish_planar_pad.comp)
CONV_STAGE=$(mktemp -d)
trap 'rm -rf -- "$CONV_STAGE"' EXIT
for file in "${CONV_FILES[@]}"; do
    if git -C "$SRC" cat-file -e "HEAD:$file" 2>/dev/null; then
        mkdir -p "$CONV_STAGE/$(dirname "$file")"
        git -C "$SRC" show "HEAD:$file" > "$CONV_STAGE/$file"
    fi
done
CONV_MATCH=-1
for ((stage=0; stage<=${#CONV_PATCHES[@]}; stage++)); do
    matches=1
    for file in "${CONV_FILES[@]}"; do
        if [[ -e $SRC/$file || -e $CONV_STAGE/$file ]]; then
            cmp -s "$SRC/$file" "$CONV_STAGE/$file" || matches=0
        fi
    done
    if (( matches )); then CONV_MATCH=$stage; fi
    if (( stage<${#CONV_PATCHES[@]} )); then
        git -C "$CONV_STAGE" apply "${CONV_PATCHES[stage]}"
    fi
done
if (( CONV_MATCH<0 )); then
    echo 'ncnn Conv1D adaptations conflict with the known patch stack; preserve checkout changes' >&2
    exit 1
fi
for ((stage=CONV_MATCH; stage<${#CONV_PATCHES[@]}; stage++)); do
    git -C "$SRC" apply "${CONV_PATCHES[stage]}"
done
rm -rf -- "$CONV_STAGE"
trap - EXIT
cmake -G Ninja -S "$TOP/radish/tests/ncnn" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$WORK/loader-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DNCNN_SOURCE="$SRC" \
    -DCMAKE_CXX_FLAGS="-mcpu=cortex-a53 --gcc-install-dir=$GCC_INSTALL" \
    -DNCNN_VULKAN=ON -DNCNN_SIMPLEVK=ON \
    -DNCNN_OPENMP="$SIMPLEOMP" -DNCNN_SIMPLEOMP="$SIMPLEOMP" -DNCNN_THREADS=ON \
    -DNCNN_ARM82=OFF -DNCNN_ARM82DOT=OFF -DNCNN_ARM82FP16FML=OFF \
    -DNCNN_ARM84BF16=OFF -DNCNN_ARM84I8MM=OFF -DNCNN_ARM86SVE=OFF \
    -DNCNN_RUNTIME_CPU=OFF -DNCNN_BUILD_TESTS=OFF -DNCNN_BUILD_TOOLS=OFF \
    -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BUILD_BENCHMARK=OFF -DNCNN_BENCHMARK=OFF
cmake --build "$BUILD" -j"${RADISH_JOBS:-6}" --target radish-ncnn radish-operators radish-model radish-snake
