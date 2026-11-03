#!/usr/bin/env bash
# Run the Kokoro decoder (ncnn) through Radish on the tablet: two requests of
# the same Japanese input in one process (request 0 cold, request 1 warm).
# usage: tts.sh LABEL DRIVER_SHA12 [--cpu] [--runtime NAME] [--requests N] [--cache-tag T] [VAR=VALUE ...]
# Extra VAR=VALUE pairs are passed to the process environment.
# Results are pulled to $SRC/runs/LABEL (stdout log, manifests, core output).
set -euo pipefail
. "$(dirname "$0")/common.sh"
LABEL=$1 SHA=$2; shift 2
BACKEND=--vulkan RUNTIME=radish-kokoro-runtime-52c9e565 REQUESTS=2 ENVS=() CACHE_TAG=
while [[ $# -gt 0 ]]; do
    case $1 in
        --cpu) BACKEND=--cpu-packed ;;
        --runtime) RUNTIME=$2; shift ;;
        --requests) REQUESTS=$2; shift ;;
        --cache-tag) CACHE_TAG=-$2; shift ;;
        *=*) ENVS+=(--setenv="$1") ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
    shift
done
R=$PREFIX/runs/$LABEL T=$PREFIX/tts
NEXT=(); for ((i=1;i<REQUESTS;i++)); do NEXT+=(--next-input $T/input-ja-greet); done
LOOP=(--request-loop "${NEXT[@]}")
CACHE=()
if [[ $BACKEND == --vulkan ]]; then
    LOOP+=(--reuse-pipeline-cache)
    CACHE=(--setenv=RADISH_NCNN_PIPELINE_CACHE_FILE=$PREFIX/cache/tts-$SHA$CACHE_TAG.bin)
fi
BASE_ENV=(--setenv=RADISH_NCNN_TILED=1 --setenv=RADISH_NCNN_TILED32=0 --setenv=RADISH_NCNN_PLANAR=1
    --setenv=RADISH_NCNN_PLANAR_PACKED_A=1 --setenv=RADISH_NCNN_NORM_SUM4=1 --setenv=RADISH_NCNN_NORM_OUTPLACE=1
    --setenv=RADISH_NCNN_PAD_CHANNELS=1 --setenv=RADISH_NCNN_SHARED32=0 --setenv=RADISH_NCNN_MIXED_STORAGE=0
    --setenv=RADISH_NCNN_PIPELINE_CACHE=1 --setenv=RADISH_NCNN_TRACE=1 --setenv=RADISH_PROFILE=0
    --setenv=RADISH_BATCH_CHUNKS=8 --setenv=RADISH_TILED_BATCH_CHUNKS=8 --setenv=RADISH_TILED_GROUPS_X=1
    --setenv=RADISH_SHARED_GROUPS=8 --setenv=RADISH_NCNN_RESHAPE_ALIAS=1
    --setenv=LD_LIBRARY_PATH=$PREFIX/lib --setenv=VK_DRIVER_FILES=$PREFIX/lib/icd-$SHA.json)
CMD="systemd-run --unit=radish-tts-$LABEL --wait --pipe --collect -p MemoryMax=640M -p MemorySwapMax=128M \
 -p RuntimeMaxSec=420 -p TimeoutStopSec=15 -p LimitCORE=0 ${BASE_ENV[*]} ${CACHE[*]} ${ENVS[*]} \
 $PREFIX/bin/$RUNTIME --norm-affine-snake --deconv2d $BACKEND --threads 4 --ort-threads 4 --max-frames 512 \
 ${LOOP[*]} $T/models $T/input-ja-greet $T/decoder.ncnn.param $T/decoder.ncnn.bin $T/kokoro-tail.bin $R/output"
OUT=$SRC/runs/$LABEL; mkdir -p "$OUT"
flock -w 1800 "$LOCK" bash -c '
    . "'"$(dirname "$0")"'/common.sh"
    before=$(gpu_fault_count)
    adbsh "rm -rf '"$R"'; mkdir -p '"$R"' '"$PREFIX"'/cache; free -m; cat /sys/class/thermal/thermal_zone8/temp" > "'"$OUT"'/pre.txt"
    adbsh "'"$CMD"'" > "'"$OUT"'/log.txt" 2> "'"$OUT"'/stderr.txt" || echo "exit $?" >> "'"$OUT"'/log.txt"
    after=$(gpu_fault_count)
    echo "gpu_fault_lines before=$before after=$after" | tee "'"$OUT"'/faults.txt"
    adbsh "cat /sys/class/thermal/thermal_zone8/temp; free -m" > "'"$OUT"'/post.txt"
    adbsh "rm -f '"$R"'/output/request-*/boundary/*.f32"
    adb -s $SERIAL pull -q "'"$R"'/." "'"$OUT"'/" >/dev/null || true
    adbsh "rm -rf '"$R"'"
'
grep -E "core extraction|request [0-9] core|synthesis|FAIL|exit" "$OUT/log.txt" || true
python3 - "$OUT" <<'PY'
import json,sys,glob
for f in sorted(glob.glob(sys.argv[1]+'/output/request-*/runtime.json')):
    j=json.load(open(f)); print(f.split('/')[-2], 'frontend %.2f core %.2f tail %.2f synth %.2f' % (j['frontend_wall_seconds'],j['core_scope_seconds'],j['tail_seconds'],j['synthesis_seconds']))
PY
