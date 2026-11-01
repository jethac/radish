#!/usr/bin/env bash
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
COMPILER=${RADISH_HOST_CXX:-c++}
mkdir -p "$WORK"
"$COMPILER" -std=c++11 -O2 -Wall -Wextra -Werror \
    "$TOP/radish/tests/kokoro_shapes.cpp" "$TOP/radish/tests/kokoro_shape_probe.cpp" \
    -o "$WORK/kokoro-shape-probe-host"
