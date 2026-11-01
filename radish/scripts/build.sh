#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SRC=${RADISH_MESA:-$WORK/mesa}
SYSROOT=${RADISH_SYSROOT:-$TOP/streams/gpu-mesa16/work/sysroot}
VENV=${RADISH_VENV:-$TOP/streams/gpu-mesa16/work/venv}
GCC_INSTALL=${RADISH_GCC_INSTALL:-$SYSROOT/usr/lib/gcc/aarch64-linux-gnu/16}
GCC_INSTALL=$(realpath -e "$GCC_INSTALL")
export PATH=$VENV/bin:$PATH
export TMPDIR=$WORK/tmp
mkdir -p "$TMPDIR"
CROSS_TMP=$(mktemp "$WORK/cross-aarch64.XXXXXX")
trap 'rm -f "$CROSS_TMP"' EXIT
cat > "$CROSS_TMP" <<CROSS
[binaries]
c = ['clang', '--target=aarch64-linux-gnu', '--sysroot=$SYSROOT', '-mcpu=cortex-a53']
cpp = ['clang++', '--target=aarch64-linux-gnu', '--sysroot=$SYSROOT', '-mcpu=cortex-a53']
ar = 'llvm-ar'
strip = 'llvm-strip'
pkg-config = 'pkg-config'
[built-in options]
c_args = ['-Wno-unused-command-line-argument']
cpp_args = ['-Wno-unused-command-line-argument', '--gcc-install-dir=$GCC_INSTALL']
c_link_args = ['-fuse-ld=lld', '--rtlib=libgcc', '-L$SYSROOT/usr/lib', '-Wl,-rpath-link,$SYSROOT/usr/lib']
cpp_link_args = ['-fuse-ld=lld', '--rtlib=libgcc', '-L$SYSROOT/usr/lib', '-Wl,-rpath-link,$SYSROOT/usr/lib']
[properties]
sys_root = '$SYSROOT'
pkg_config_libdir = '$SYSROOT/usr/lib/pkgconfig'
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'cortex-a53'
endian = 'little'
CROSS
CROSS_CHANGED=0
if ! cmp -s "$CROSS_TMP" "$WORK/cross-aarch64.ini"; then
    mv "$CROSS_TMP" "$WORK/cross-aarch64.ini"
    CROSS_CHANGED=1
fi
if [[ ! -f $WORK/build/build.ninja ]]; then
    meson setup "$WORK/build" "$SRC" --cross-file "$WORK/cross-aarch64.ini" \
        --prefix=/opt/radish --libdir=lib --wrap-mode=nofallback \
        -Dbuildtype=debugoptimized -Dplatforms= -Degl=disabled \
        -Dgles1=disabled -Dgles2=disabled -Dopengl=false -Dglx=disabled \
        -Dgbm=disabled -Dgallium-drivers= -Dvulkan-drivers=radish \
        -Dfreedreno-kmds=msm -Dllvm=disabled -Dxmlconfig=disabled \
        -Dzstd=disabled -Dlmsensors=disabled -Dvalgrind=disabled \
        -Dlibunwind=disabled -Dgallium-rusticl=false -Dvideo-codecs= \
        -Dteflon=false -Dtools= -Dbuild-tests=false
elif (( CROSS_CHANGED )); then
    meson configure "$WORK/build" \
        "-Dcpp_args=['-Wno-unused-command-line-argument','--gcc-install-dir=$GCC_INSTALL']"
fi
ninja -C "$WORK/build" -j"${RADISH_JOBS:-6}" src/freedreno/radish/libvulkan_radish.so
for program in lifecycle compute transfer grid control; do
    clang --target=aarch64-linux-gnu --sysroot="$SYSROOT" -mcpu=cortex-a53 \
        -I"$SRC/include" "$TOP/radish/tests/$program.c" -ldl \
        -fuse-ld=lld --rtlib=libgcc -L"$SYSROOT/usr/lib" \
        -Wl,-rpath-link,"$SYSROOT/usr/lib" -o "$WORK/$program"
done
for mode in loop array div array-div loop-long loop-load; do
    CONTROL_FLAGS=()
    if [[ $mode == array* ]]; then CONTROL_FLAGS+=(-DCONTROL_ARRAY=1); fi
    if [[ $mode == *div ]]; then CONTROL_FLAGS+=(-DCONTROL_DIV=1); fi
    if [[ $mode == loop-long ]]; then CONTROL_FLAGS+=(-DCONTROL_LONG=1); fi
    if [[ $mode == loop-load ]]; then CONTROL_FLAGS+=(-DCONTROL_LOAD=1); fi
    glslangValidator -V --target-env vulkan1.0 "${CONTROL_FLAGS[@]}" \
        "$TOP/radish/tests/shaders/control.comp" -o "$WORK/control-$mode.spv"
done
glslangValidator -V --target-env vulkan1.0 \
    "$TOP/radish/tests/shaders/long-shared.comp" -o "$WORK/short-shared.spv"
glslangValidator -V --target-env vulkan1.0 -DRADISH_LONG_SHADER=1 \
    "$TOP/radish/tests/shaders/long-shared.comp" -o "$WORK/long-shared.spv"
glslangValidator -V --target-env vulkan1.0 \
    "$TOP/radish/tests/shaders/grid.comp" -o "$WORK/grid.spv"
glslangValidator -V --target-env vulkan1.0 -DRADISH_GRID_SHARED=1 \
    "$TOP/radish/tests/shaders/grid.comp" -o "$WORK/grid-shared.spv"
glslangValidator -V --target-env vulkan1.0 \
    "$TOP/radish/tests/shaders/shared-reduce.comp" -o "$WORK/shared-reduce.spv"
glslangValidator -V --target-env vulkan1.0 -DRADISH_SHARED_SMALL=1 \
    "$TOP/radish/tests/shaders/shared-reduce.comp" -o "$WORK/shared-reduce-small.spv"
for variant in low mid; do
    glslangValidator -V --target-env vulkan1.0 "-DRADISH_SHARED_${variant^^}=1" \
        "$TOP/radish/tests/shaders/shared-reduce.comp" -o "$WORK/shared-reduce-$variant.spv"
done
for shader in index vector-add; do
    glslangValidator -V --target-env vulkan1.0 \
        "$TOP/radish/tests/shaders/$shader.comp" -o "$WORK/$shader.spv"
    glslangValidator -V --target-env vulkan1.0 -DRADISH_CONSECUTIVE=1 \
        "$TOP/radish/tests/shaders/$shader.comp" -o "$WORK/$shader-consecutive.spv"
done
