#!/usr/bin/env bash
# Run radish-core-bench on a model directory under $PREFIX (param/bin/inputs).
# usage: core-bench.sh LABEL DRIVER_SHA12 MODEL_DIR BIN_NAME REPEAT [--cpu] [VAR=VALUE ...]
set -euo pipefail
. "$(dirname "$0")/common.sh"
LABEL=$1 SHA=$2 M=$3 BIN=$4 REPEAT=$5; shift 5
CPU= ENVS=()
for e in "$@"; do [[ $e == --cpu ]] && CPU=--cpu || ENVS+=(--setenv="$e"); done
OUT=$SRC/runs/$LABEL; mkdir -p "$OUT"
BASE=(--setenv=RADISH_NCNN_TILED=1 --setenv=RADISH_NCNN_PLANAR=1 --setenv=RADISH_NCNN_PLANAR_PACKED_A=1 --setenv=RADISH_NCNN_NORM_SUM4=1 --setenv=RADISH_NCNN_NORM_OUTPLACE=1 --setenv=RADISH_NCNN_PAD_CHANNELS=1 --setenv=RADISH_NCNN_RESHAPE_ALIAS=1 --setenv=RADISH_BATCH_CHUNKS=8 --setenv=RADISH_TILED_BATCH_CHUNKS=8 --setenv=RADISH_SHARED_GROUPS=8
      --setenv=RADISH_NCNN_GEMM=1 --setenv=RADISH_NCNN_WINOGRAD=1 --setenv=RADISH_BARRIER_GROUPS=32 --setenv=RADISH_GROUPS=4096
      --setenv=LD_LIBRARY_PATH=$PREFIX/lib --setenv=VK_DRIVER_FILES=$PREFIX/lib/icd-$SHA.json)
flock -w 1800 "$LOCK" bash -c '
    . "'"$(dirname "$0")"'/common.sh"
    before=$(gpu_fault_count)
    adbsh "systemd-run --unit=radish-core-'"$LABEL"' --wait --pipe --collect -p MemoryMax=400M -p MemorySwapMax=64M -p RuntimeMaxSec=300 -p LimitCORE=0 '"${BASE[*]} ${ENVS[*]}"' '"$PREFIX"'/bin/'"$BIN"' '"$CPU"' '"$PREFIX/$M"'/core.ncnn.param '"$PREFIX/$M"'/core.ncnn.bin '"$PREFIX/$M"' '"$REPEAT"'" > "'"$OUT"'/log.txt" 2>&1 || echo "exit $?" >> "'"$OUT"'/log.txt"
    echo "gpu_fault_lines before=$before after=$(gpu_fault_count)" >> "'"$OUT"'/log.txt"
    adbsh "rm -f '"$PREFIX/$M"'/got.f32"
'
grep -E "^load|^extract|vs reference|output|exit|fault|Memory peak|FAIL|failed" "$OUT/log.txt"
