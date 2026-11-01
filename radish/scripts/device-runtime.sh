#!/usr/bin/env bash
main() {
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
MODELS=${RADISH_FRONTEND_MODELS:-$WORK/kokoro-frontend-models}
BINARY=${RADISH_RUNTIME_BINARY:-$WORK/build-ncnn-simpleomp/radish-kokoro-runtime}
DRIVER=${RADISH_RUNTIME_DRIVER:-$WORK/build/src/freedreno/radish/libvulkan_radish.so}
LOADER=${RADISH_RUNTIME_LOADER:-$WORK/build-loader/loader/libvulkan.so}
CAPTURE=$TOP/radish/scripts/kokoro-runtime-capture.py
LOADER_SHA=75699a38b1f26c1e8bfa2062e6593177bb07d4e3e346486d117fab8a0de0104a
TEMPLATE=$WORK/kokoro-core-shapes-instancenorm
TAIL=$WORK/kokoro-tail.bin
KIND=${1:-}
case $KIND in runtime-short|runtime-full) ;; *) echo 'usage: device-runtime.sh runtime-short|runtime-full [run-label] [--snake|--affine-snake|--snake-deconv2d|--affine-snake-deconv2d|--norm-affine-snake-deconv2d] [--vulkan|--cpu-packed] [--threads N] [--ort-threads N] [--max-frames N] [--repeat N] [--request-loop --next-input runtime-short|runtime-full ... [--reuse-pipeline-cache]]' >&2; exit 2 ;; esac
shift
RUN=$(date +%s%N)
if [[ $# -gt 0 && $1 != --* ]]; then RUN=$1; shift; fi
[[ $RUN =~ ^[A-Za-z0-9_-]{1,48}$ ]] || { echo 'invalid run label' >&2; exit 2; }
BACKEND=vulkan
BACKEND_SET=false
THREADS=4
ORT_THREADS=4
MAX_FRAMES=512
REPEAT=1
CORE_GRAPH=instancenorm
REQUEST_LOOP=false
REUSE_PIPELINE_CACHE=false
NEXT_INPUTS=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --reuse-pipeline-cache)
            ! $REUSE_PIPELINE_CACHE || { echo 'duplicate --reuse-pipeline-cache' >&2; exit 2; }
            REUSE_PIPELINE_CACHE=true ;;
        --request-loop)
            ! $REQUEST_LOOP || { echo 'duplicate --request-loop' >&2; exit 2; }
            REQUEST_LOOP=true ;;
        --next-input)
            [[ $# -ge 2 && ( $2 == runtime-short || $2 == runtime-full ) ]] || { echo 'next input must be runtime-short or runtime-full' >&2; exit 2; }
            NEXT_INPUTS+=("$2"); shift ;;
        --snake|--affine-snake|--snake-deconv2d|--affine-snake-deconv2d|--norm-affine-snake-deconv2d)
            [[ $CORE_GRAPH == instancenorm ]] || { echo 'select one core graph' >&2; exit 2; }
            CORE_GRAPH=${1#--}; TEMPLATE=$WORK/kokoro-core-shapes-$CORE_GRAPH ;;
        --vulkan|--cpu-packed)
            if $BACKEND_SET; then echo 'select one backend' >&2; exit 2; fi
            BACKEND=${1#--}; BACKEND_SET=true ;;
        --threads|--ort-threads|--max-frames|--repeat)
            [[ $# -ge 2 ]] || { echo 'missing option value' >&2; exit 2; }
            case $1 in
                --threads) THREADS=$2 ;;
                --ort-threads) ORT_THREADS=$2 ;;
                --max-frames) MAX_FRAMES=$2 ;;
                --repeat) REPEAT=$2 ;;
            esac
            shift ;;
        *) echo "unsupported argument: $1" >&2; exit 2 ;;
    esac
    shift
done
[[ $THREADS =~ ^[1-6]$ && $ORT_THREADS =~ ^[1-6]$ ]] || { echo 'thread counts must be 1..6' >&2; exit 2; }
[[ $MAX_FRAMES =~ ^[1-9][0-9]{0,3}$ && $MAX_FRAMES -le 4096 ]] || { echo 'max frames must be 1..4096' >&2; exit 2; }
[[ $REPEAT =~ ^([1-9]|10)$ ]] || { echo 'repeat must be 1..10' >&2; exit 2; }
if $REQUEST_LOOP; then
    [[ ${#NEXT_INPUTS[@]} -ge 1 && ${#NEXT_INPUTS[@]} -le 3 && $REPEAT == 1 && ( $CORE_GRAPH == affine-snake-deconv2d || $CORE_GRAPH == norm-affine-snake-deconv2d ) ]] || { echo 'request loop requires 2..4 inputs, repeat 1 and an explicit affine/deconv2d core graph' >&2; exit 2; }
else
    [[ ${#NEXT_INPUTS[@]} == 0 ]] || { echo '--next-input requires --request-loop' >&2; exit 2; }
fi
TILED=${RADISH_NCNN_TILED:-1}
TILED32=${RADISH_NCNN_TILED32:-0}
PLANAR=${RADISH_NCNN_PLANAR:-0}
[[ $PLANAR == 0 || $PLANAR == 1 ]] || { echo 'RADISH_NCNN_PLANAR must be 0 or 1' >&2; exit 2; }
PAD_CHANNELS=${RADISH_NCNN_PAD_CHANNELS:-0}
[[ $PAD_CHANNELS == 0 || $PAD_CHANNELS == 1 ]] || { echo 'RADISH_NCNN_PAD_CHANNELS must be 0 or 1' >&2; exit 2; }
NORM_SUM4=${RADISH_NCNN_NORM_SUM4:-0}
[[ $NORM_SUM4 == 0 || $NORM_SUM4 == 1 ]] || { echo 'RADISH_NCNN_NORM_SUM4 must be 0 or 1' >&2; exit 2; }
NORM_OUTPLACE=${RADISH_NCNN_NORM_OUTPLACE:-0}
[[ $NORM_OUTPLACE == 0 || $NORM_OUTPLACE == 1 ]] || { echo 'RADISH_NCNN_NORM_OUTPLACE must be 0 or 1' >&2; exit 2; }
SHARED32=${RADISH_NCNN_SHARED32:-0}
MIXED=${RADISH_NCNN_MIXED_STORAGE:-0}
RESHAPE_ALIAS=${RADISH_NCNN_RESHAPE_ALIAS:-1}
CACHE=${RADISH_NCNN_PIPELINE_CACHE:-1}
TRACE=${RADISH_NCNN_TRACE:-1}
PROFILE=${RADISH_PROFILE:-1}
BATCH=${RADISH_BATCH_CHUNKS:-4}
TILED_BATCH=${RADISH_TILED_BATCH_CHUNKS:-8}
[[ $TILED_BATCH =~ ^(1|2|4|8)$ ]] || { echo 'RADISH_TILED_BATCH_CHUNKS must be 1, 2, 4 or 8' >&2; exit 2; }
TILED_GROUPS_X=${RADISH_TILED_GROUPS_X:-1}
[[ $TILED_GROUPS_X == 1 || $TILED_GROUPS_X == 2 ]] || { echo 'RADISH_TILED_GROUPS_X must be 1 or 2' >&2; exit 2; }
GROUPS_PER_CHUNK=${RADISH_SHARED_GROUPS:-8}
for toggle in "$TILED" "$TILED32" "$SHARED32" "$MIXED" "$CACHE" "$TRACE" "$PROFILE" "$RESHAPE_ALIAS"; do
    [[ $toggle == 0 || $toggle == 1 ]] || { echo 'Radish toggles must be 0 or 1' >&2; exit 2; }
done
if [[ $NORM_OUTPLACE == 1 ]]; then
    [[ $BACKEND == vulkan && $MIXED == 0 && $NORM_SUM4 == 1 && $RESHAPE_ALIAS == 1 ]] || { echo 'NORM_OUTPLACE requires Vulkan FP32, NORM_SUM4=1 and RESHAPE_ALIAS=1' >&2; exit 2; }
fi
if [[ $CORE_GRAPH == norm-affine-snake-deconv2d && $BACKEND == vulkan ]]; then
    [[ $NORM_SUM4 == 1 && $MIXED == 0 ]] || { echo 'NormAffineSnake requires Vulkan FP32 and NORM_SUM4=1' >&2; exit 2; }
fi
if $REUSE_PIPELINE_CACHE; then
    [[ $REQUEST_LOOP == true && $BACKEND == vulkan && $CACHE == 1 ]] || { echo '--reuse-pipeline-cache requires a Vulkan request loop and RADISH_NCNN_PIPELINE_CACHE=1' >&2; exit 2; }
fi
[[ $BATCH =~ ^([1-4]|8)$ && $GROUPS_PER_CHUNK =~ ^[1-8]$ ]] || { echo 'batch must be 1, 2, 3, 4 or 8 and shared groups 1..8' >&2; exit 2; }
VALIDATOR=$TOP/radish/scripts/validate-kokoro-snake-fixture.py
CORE_ARGS=
if [[ $CORE_GRAPH == norm-affine-snake-deconv2d ]]; then
    CORE_ARGS=--norm-affine-snake
elif [[ $CORE_GRAPH == affine-snake || $CORE_GRAPH == affine-snake-deconv2d ]]; then
    VALIDATOR=$TOP/radish/scripts/validate-kokoro-affine-snake-fixture.py
    CORE_ARGS=--affine-snake
fi
if [[ $CORE_GRAPH == *-deconv2d ]]; then
    VALIDATOR=$TOP/radish/scripts/validate-kokoro-deconv2d-fixture.py
    CORE_ARGS+=" --deconv2d"
fi
SOURCE=$WORK/kokoro-$KIND
TAG=runtime-$KIND-$BACKEND-$RUN
[[ $CORE_GRAPH == instancenorm ]] || TAG+=-$CORE_GRAPH
[[ $PLANAR == 0 ]] || TAG+=-planar
[[ $NORM_SUM4 == 0 ]] || TAG+=-norm-sum4
[[ $NORM_OUTPLACE == 0 ]] || TAG+=-norm-outplace
[[ $PAD_CHANNELS == 0 ]] || TAG+=-pad-channels
[[ $TILED_GROUPS_X == 1 ]] || TAG+=-tiled-groups-x$TILED_GROUPS_X
if $REQUEST_LOOP; then TAG+=-loop; fi
if $REUSE_PIPELINE_CACHE; then TAG+=-pipeline-cache; fi
UNIT=radish-$TAG
REMOTE=/var/tmp/radish/$TAG
OUTPUT=$WORK/kokoro-$KIND-single-$BACKEND-$RUN
[[ $CORE_GRAPH == instancenorm ]] || OUTPUT+=-$CORE_GRAPH
[[ $PLANAR == 0 ]] || OUTPUT+=-planar
[[ $NORM_SUM4 == 0 ]] || OUTPUT+=-norm-sum4
[[ $NORM_OUTPLACE == 0 ]] || OUTPUT+=-norm-outplace
[[ $PAD_CHANNELS == 0 ]] || OUTPUT+=-pad-channels
[[ $TILED_GROUPS_X == 1 ]] || OUTPUT+=-tiled-groups-x$TILED_GROUPS_X
if $REQUEST_LOOP; then OUTPUT+=-loop; fi
if $REUSE_PIPELINE_CACHE; then OUTPUT+=-pipeline-cache; fi
[[ ! -e $OUTPUT && ! -L $OUTPUT ]] || { echo 'host output already exists' >&2; exit 2; }
mkdir -p "$WORK/evidence"
[[ ! -e $WORK/evidence/$TAG-inputs.json && ! -e $WORK/evidence/$TAG.txt && ! -e $WORK/evidence/$TAG-icd.json ]] || { echo 'run label already has evidence' >&2; exit 2; }
PREPARE_ARGS=()
LOOP_ARGS=
INPUT_KINDS=("$KIND")
INPUT_REMOTE=$REMOTE/input
if $REQUEST_LOOP; then
    PREPARE_ARGS+=(--request-loop)
    INPUT_KINDS+=("${NEXT_INPUTS[@]}")
    INPUT_REMOTE=$REMOTE/input-$KIND
    LOOP_ARGS=--request-loop
    for next in "${NEXT_INPUTS[@]}"; do
        PREPARE_ARGS+=(--next-source "$WORK/kokoro-$next")
        LOOP_ARGS+=" --next-input $REMOTE/input-$next"
    done
fi
if $REUSE_PIPELINE_CACHE; then
    PREPARE_ARGS+=(--reuse-pipeline-cache)
    LOOP_ARGS+=" --reuse-pipeline-cache"
fi
python3 "$CAPTURE" prepare --source "$SOURCE" --models "$MODELS" --template "$TEMPLATE" \
    --binary "$BINARY" --driver "$DRIVER" --loader "$LOADER" --tail "$TAIL" \
    --output "$WORK/evidence/$TAG-inputs.json" --icd "$WORK/evidence/$TAG-icd.json" \
    --remote "$REMOTE" --backend "$BACKEND" --threads "$THREADS" --ort-threads "$ORT_THREADS" \
    --max-frames "$MAX_FRAMES" --repeat "$REPEAT" --core-graph "$CORE_GRAPH" --validator "$VALIDATOR" \
    --wrapper "$TOP/radish/scripts/device-runtime.sh" --source-root "$TOP" \
    --env RADISH_NCNN_TILED=$TILED --env RADISH_NCNN_TILED32=$TILED32 --env RADISH_NCNN_PLANAR=$PLANAR \
    --env RADISH_NCNN_PAD_CHANNELS=$PAD_CHANNELS --env RADISH_NCNN_NORM_SUM4=$NORM_SUM4 --env RADISH_NCNN_NORM_OUTPLACE=$NORM_OUTPLACE --env RADISH_NCNN_SHARED32=$SHARED32 \
    --env RADISH_NCNN_MIXED_STORAGE=$MIXED --env RADISH_NCNN_RESHAPE_ALIAS=$RESHAPE_ALIAS \
    --env RADISH_NCNN_PIPELINE_CACHE=$CACHE --env RADISH_NCNN_TRACE=$TRACE --env RADISH_PROFILE=$PROFILE \
    --env RADISH_BATCH_CHUNKS=$BATCH --env RADISH_TILED_BATCH_CHUNKS=$TILED_BATCH \
    --env RADISH_TILED_GROUPS_X=$TILED_GROUPS_X --env RADISH_SHARED_GROUPS=$GROUPS_PER_CHUNK "${PREPARE_ARGS[@]}"
exec 9>"${RADISH_DEVICE_LOCK:-$TOP/.device.lock}"
flock -w 1800 9
python3 "$CAPTURE" recheck --record "$WORK/evidence/$TAG-inputs.json"
ADB=(adb -s "$SERIAL")
PULL_STAGE=
CHECKED=false
STARTED=false
DRAINED=false
drain_own_unit() {
    local state
    while true; do
        timeout 25s "${ADB[@]}" shell "systemctl stop '$UNIT'" >/dev/null 2>&1 || true
        if state=$(timeout 10s "${ADB[@]}" shell "systemctl show '$UNIT' -p LoadState -p ActiveState -p SubState" 2>/dev/null); then
            state=${state//$'\r'/}
            if [[ $state == *'LoadState=not-found'* || ( $state == *'ActiveState=inactive'* && $state == *'SubState=dead'* ) || ( $state == *'ActiveState=failed'* && $state == *'SubState=failed'* ) ]]; then
                DRAINED=true
                return
            fi
        fi
        echo "Waiting for this run's unit $UNIT to stop; retaining the device lock." >&2
        sleep 5
    done
}
collect_evidence() {
    local result=0
    "${ADB[@]}" shell dmesg > "$WORK/evidence/$TAG-dmesg-after.txt" || result=1
    "${ADB[@]}" shell cat /proc/meminfo > "$WORK/evidence/$TAG-mem-after.txt" || result=1
    "${ADB[@]}" shell 'df -Pk /var/tmp' > "$WORK/evidence/$TAG-disk-after.txt" || result=1
    "${ADB[@]}" shell "journalctl --no-pager -u $UNIT" > "$WORK/evidence/$TAG-service.txt" || result=1
    "${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice' || result=1
    python3 "$TOP/radish/scripts/check-gpu-log.py" "$WORK/evidence/$TAG-dmesg-before.txt" "$WORK/evidence/$TAG-dmesg-after.txt" || result=1
    CHECKED=true
    return "$result"
}
finish() {
    local result=$?
    trap - EXIT
    if $STARTED && ! $DRAINED; then drain_own_unit; fi
    if ! $CHECKED; then collect_evidence || result=1; fi
    if [[ -n $PULL_STAGE ]]; then rm -rf -- "$PULL_STAGE"; fi
    exit "$result"
}
"${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice'
"${ADB[@]}" shell dmesg > "$WORK/evidence/$TAG-dmesg-before.txt"
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
"${ADB[@]}" shell cat /proc/meminfo > "$WORK/evidence/$TAG-mem-before.txt"
"${ADB[@]}" shell 'df -Pk /var/tmp' > "$WORK/evidence/$TAG-disk-before.txt"
AVAILABLE=$(awk '$1 == "MemAvailable:" {print $2}' "$WORK/evidence/$TAG-mem-before.txt")
[[ $AVAILABLE =~ ^[0-9]+$ && $AVAILABLE -ge $(((640 + 192) * 1024)) ]] || { echo 'insufficient available memory for 640MiB cap plus 192MiB reserve' >&2; exit 1; }
WEIGHT_SHA=0f77cf77ff79a7724662f2e79a4d50284eb498abb2e519469a188e33f6c051ae
DEVICE_WEIGHTS=/var/tmp/radish/kokoro-core-shapes-instancenorm/decoder.ncnn.bin
UPLOAD_WEIGHTS=false
if ! "${ADB[@]}" shell "test -f $DEVICE_WEIGHTS && test ! -L $DEVICE_WEIGHTS"; then
    DEVICE_WEIGHTS=/var/tmp/radish/kokoro-core-$WEIGHT_SHA.bin
    if ! "${ADB[@]}" shell "test -f $DEVICE_WEIGHTS && test ! -L $DEVICE_WEIGHTS"; then UPLOAD_WEIGHTS=true; fi
fi
if ! $UPLOAD_WEIGHTS; then
    "${ADB[@]}" shell "test -f '$DEVICE_WEIGHTS' && test ! -L '$DEVICE_WEIGHTS'"
    "${ADB[@]}" shell "sha256sum $DEVICE_WEIGHTS" > "$WORK/evidence/$TAG-weights.txt"
    [[ $(awk '{print $1}' "$WORK/evidence/$TAG-weights.txt" | tr -d '\r') == "$WEIGHT_SHA" ]] || { echo 'existing shared device weights have an unexpected hash' >&2; exit 1; }
fi
GRAPH_NAMES=(frontend-only.onnx harmonic-source.onnx)
GRAPH_HASHES=(56ba27008352a2191b631ebbf269bf4506d690a1b83d8ff8ff5524b11291ddd3 4bdcb9f2f54d6efbfdcefce384300352933cc71fa562930f23eaeb3443528fd2)
GRAPH_REUSE=()
for index in 0 1; do
    file=${GRAPH_NAMES[$index]}
    digest=${GRAPH_HASHES[$index]}
    reuse=$("${ADB[@]}" shell "for candidate in /var/tmp/radish/runtime-*/models/$file; do
        if test -f \"\$candidate\" && test ! -L \"\$candidate\"; then
            candidate_hash=\$(sha256sum \"\$candidate\") || exit 1
            if test \"\${candidate_hash%% *}\" = '$digest'; then printf '%s\\n' \"\$candidate\"; break; fi
        fi
    done" | tr -d '\r')
    if [[ -n $reuse ]]; then
        [[ $reuse =~ ^/var/tmp/radish/runtime-[A-Za-z0-9_-]+/models/(frontend-only|harmonic-source)\.onnx$ && $reuse == */"$file" ]] || { echo 'invalid remote graph reuse path' >&2; exit 1; }
    fi
    GRAPH_REUSE+=("$reuse")
done
python3 - "$WORK/evidence/$TAG-inputs.json" "$WORK/evidence/$TAG-graphs.json" "${GRAPH_REUSE[@]}" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]));graphs={}
for name,reused in zip(('frontend-only.onnx','harmonic-source.onnx'),sys.argv[3:5]):
    graphs[name]={'method':'hardlink' if reused else 'upload','reused_from':reused or None,
                  'remote_path':r['remote_directory']+'/models/'+name,**r['frontend_inputs']['graphs'][name]}
with open(sys.argv[2],'x') as stream:json.dump(graphs,stream,indent=2);stream.write('\n')
PY
verify_remote_graphs() {
    local destination=$1
    "${ADB[@]}" shell "test -f '$REMOTE/models/frontend-only.onnx' && test ! -L '$REMOTE/models/frontend-only.onnx' && test -f '$REMOTE/models/harmonic-source.onnx' && test ! -L '$REMOTE/models/harmonic-source.onnx' && sha256sum '$REMOTE/models/frontend-only.onnx' '$REMOTE/models/harmonic-source.onnx'" > "$destination"
    python3 - "$WORK/evidence/$TAG-graphs.json" "$destination" <<'PY'
import json,pathlib,sys
graphs=json.load(open(sys.argv[1]));observed={}
for line in pathlib.Path(sys.argv[2]).read_text().splitlines():
    digest,path=line.split(None,1)
    if path in observed:raise ValueError('duplicate remote graph hash')
    observed[path]=digest
if observed!={entry['remote_path']:entry['sha256'] for entry in graphs.values()}:
    raise ValueError('remote graph hash mismatch')
PY
}
DISK_ARGS=()
if $UPLOAD_WEIGHTS; then DISK_ARGS+=(--upload-weights); fi
DISK_NEEDED=$(python3 "$CAPTURE" disk-needed --record "$WORK/evidence/$TAG-inputs.json" \
    --shared-graphs "$WORK/evidence/$TAG-graphs.json" "${DISK_ARGS[@]}")
DISK=$(awk 'NR == 2 {print $4}' "$WORK/evidence/$TAG-disk-before.txt")
[[ $DISK =~ ^[0-9]+$ && $DISK_NEEDED =~ ^[0-9]+$ && $DISK -ge $DISK_NEEDED ]] || { echo "insufficient device disk; need ${DISK_NEEDED}KiB for payload/output/reserve" >&2; exit 1; }
"${ADB[@]}" shell 'test -r /usr/local/lib/straylight-listen/libonnxruntime.so'
"${ADB[@]}" shell "mkdir -p /var/tmp/radish && mkdir $REMOTE && mkdir $REMOTE/models"
if $UPLOAD_WEIGHTS; then
    "${ADB[@]}" push "$TEMPLATE/decoder.ncnn.bin" "$REMOTE/weights-upload.bin"
    "${ADB[@]}" shell "sha256sum $REMOTE/weights-upload.bin" > "$WORK/evidence/$TAG-weights.txt"
    [[ $(awk '{print $1}' "$WORK/evidence/$TAG-weights.txt" | tr -d '\r') == "$WEIGHT_SHA" ]] || { echo 'uploaded device weight hash mismatch' >&2; exit 1; }
    "${ADB[@]}" shell "ln $REMOTE/weights-upload.bin $DEVICE_WEIGHTS && rm $REMOTE/weights-upload.bin"
fi
"${ADB[@]}" push "$BINARY" "$REMOTE/radish-kokoro-runtime"
"${ADB[@]}" push "$DRIVER" "$REMOTE/libvulkan_radish.so"
"${ADB[@]}" push "$LOADER" "$REMOTE/libvulkan.so.1"
"${ADB[@]}" shell "sha256sum $REMOTE/libvulkan.so.1" > "$WORK/evidence/$TAG-loader.txt"
[[ $(awk '{print $1}' "$WORK/evidence/$TAG-loader.txt" | tr -d '\r') == "$LOADER_SHA" ]] || { echo 'uploaded private Vulkan loader hash mismatch' >&2; exit 1; }
"${ADB[@]}" push "$WORK/evidence/$TAG-icd.json" "$REMOTE/radish_icd.json"
"${ADB[@]}" push "$TEMPLATE/decoder.ncnn.param" "$REMOTE/decoder.ncnn.param"
"${ADB[@]}" shell "sha256sum '$REMOTE/decoder.ncnn.param'" > "$WORK/evidence/$TAG-param.txt"
PARAM_SHA=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["core_graph"]["param_sha256"])' "$WORK/evidence/$TAG-inputs.json")
[[ $(awk '{print $1}' "$WORK/evidence/$TAG-param.txt" | tr -d '\r') == "$PARAM_SHA" ]] || { echo 'uploaded parameter hash mismatch' >&2; exit 1; }
"${ADB[@]}" push "$TAIL" "$REMOTE/kokoro-tail.bin"
for index in 0 1; do
    file=${GRAPH_NAMES[$index]}
    if [[ -n ${GRAPH_REUSE[$index]} ]]; then
        "${ADB[@]}" shell "ln '${GRAPH_REUSE[$index]}' '$REMOTE/models/$file'"
    else
        "${ADB[@]}" push "$MODELS/$file" "$REMOTE/$file-upload"
        "${ADB[@]}" shell "ln '$REMOTE/$file-upload' '$REMOTE/models/$file' && rm '$REMOTE/$file-upload'"
    fi
done
verify_remote_graphs "$WORK/evidence/$TAG-graphs-before.txt"
declare -A STAGED_INPUTS=()
for input_kind in "${INPUT_KINDS[@]}"; do
    if [[ -n ${STAGED_INPUTS[$input_kind]:-} ]]; then continue; fi
    input_remote=$REMOTE/input
    if $REQUEST_LOOP; then input_remote=$REMOTE/input-$input_kind; fi
    "${ADB[@]}" shell "mkdir '$input_remote'"
    for file in ids.i64 style.f32 speed.f32; do "${ADB[@]}" push "$WORK/kokoro-$input_kind/$file" "$input_remote/$file"; done
    STAGED_INPUTS[$input_kind]=1
done
REMOTE_CHECK=$(python3 "$CAPTURE" remote-script --record "$WORK/evidence/$TAG-inputs.json" --shared-weights "$DEVICE_WEIGHTS")
"${ADB[@]}" shell "$REMOTE_CHECK" > "$WORK/evidence/$TAG-assets-before.txt"
python3 "$CAPTURE" verify-remote --record "$WORK/evidence/$TAG-inputs.json" --shared-weights "$DEVICE_WEIGHTS" --hashes "$WORK/evidence/$TAG-assets-before.txt"
python3 "$CAPTURE" recheck --record "$WORK/evidence/$TAG-inputs.json"
echo "Runtime backend=$BACKEND graph=$CORE_GRAPH repeat=$REPEAT threads=$THREADS ort_threads=$ORT_THREADS; shared weights=$DEVICE_WEIGHTS"
echo "GPU controls: tiled=$TILED tiled32=$TILED32 planar=$PLANAR norm_sum4=$NORM_SUM4 norm_outplace=$NORM_OUTPLACE pad_channels=$PAD_CHANNELS batch=$BATCH tiled_batch=$TILED_BATCH tiled_groups_x=$TILED_GROUPS_X shared_groups=$GROUPS_PER_CHUNK shared32=$SHARED32 mixed=$MIXED cache=$CACHE trace=$TRACE profile=$PROFILE reshape_alias=$RESHAPE_ALIAS"
STARTED=true
timeout 330s "${ADB[@]}" shell "chmod 755 $REMOTE/radish-kokoro-runtime && systemd-run --unit=$UNIT --wait --pipe --collect -p MemoryMax=640M -p MemorySwapMax=128M -p RuntimeMaxSec=300 -p TimeoutStopSec=15 -p LimitCORE=0 --setenv=RADISH_NCNN_TILED=$TILED --setenv=RADISH_NCNN_TILED32=$TILED32 --setenv=RADISH_NCNN_PLANAR=$PLANAR --setenv=RADISH_NCNN_NORM_SUM4=$NORM_SUM4 --setenv=RADISH_NCNN_NORM_OUTPLACE=$NORM_OUTPLACE --setenv=RADISH_NCNN_PAD_CHANNELS=$PAD_CHANNELS --setenv=RADISH_NCNN_SHARED32=$SHARED32 --setenv=RADISH_NCNN_MIXED_STORAGE=$MIXED --setenv=RADISH_NCNN_RESHAPE_ALIAS=$RESHAPE_ALIAS --setenv=RADISH_NCNN_PIPELINE_CACHE=$CACHE --setenv=RADISH_NCNN_TRACE=$TRACE --setenv=RADISH_PROFILE=$PROFILE --setenv=RADISH_BATCH_CHUNKS=$BATCH --setenv=RADISH_TILED_BATCH_CHUNKS=$TILED_BATCH --setenv=RADISH_TILED_GROUPS_X=$TILED_GROUPS_X --setenv=RADISH_SHARED_GROUPS=$GROUPS_PER_CHUNK --setenv=LD_LIBRARY_PATH=$REMOTE --setenv=VK_DRIVER_FILES=$REMOTE/radish_icd.json $REMOTE/radish-kokoro-runtime $CORE_ARGS $LOOP_ARGS --$BACKEND --threads $THREADS --ort-threads $ORT_THREADS --max-frames $MAX_FRAMES --repeat $REPEAT $REMOTE/models $INPUT_REMOTE $REMOTE/decoder.ncnn.param $DEVICE_WEIGHTS $REMOTE/kokoro-tail.bin $REMOTE/output" 2>&1 | tee "$WORK/evidence/$TAG.txt"
DRAINED=true
if rg -n 'vkCreateComputePipelines failed|new_pipeline failed' "$WORK/evidence/$TAG.txt"; then exit 1; fi
"${ADB[@]}" shell "$REMOTE_CHECK" > "$WORK/evidence/$TAG-assets-after.txt"
python3 "$CAPTURE" verify-remote --record "$WORK/evidence/$TAG-inputs.json" --shared-weights "$DEVICE_WEIGHTS" --hashes "$WORK/evidence/$TAG-assets-after.txt"
"${ADB[@]}" shell "test -d '$REMOTE/output' && test ! -L '$REMOTE/output' && test -z \"\$(find '$REMOTE/output' -type l -print -quit)\" && find '$REMOTE/output' -type f -exec sha256sum {} +" > "$WORK/evidence/$TAG-output-hashes.txt"
PULL_STAGE=$(mktemp -d "$WORK/.runtime-pull-$KIND-XXXXXX")
"${ADB[@]}" pull "$REMOTE/output" "$PULL_STAGE/output"
collect_evidence
verify_remote_graphs "$WORK/evidence/$TAG-graphs-after.txt"
FINAL_ARGS=()
for suffix in .txt -service.txt -weights.txt -loader.txt -param.txt -graphs.json -graphs-before.txt -graphs-after.txt \
    -dmesg-before.txt -dmesg-after.txt -mem-before.txt -mem-after.txt -disk-before.txt -disk-after.txt; do
    FINAL_ARGS+=(--evidence "$WORK/evidence/$TAG$suffix")
done
python3 "$CAPTURE" finalize --stage "$PULL_STAGE/output" --output "$OUTPUT" \
    --record "$WORK/evidence/$TAG-inputs.json" --shared-weights "$DEVICE_WEIGHTS" \
    --shared-graphs "$WORK/evidence/$TAG-graphs.json" --remote-before "$WORK/evidence/$TAG-assets-before.txt" \
    --remote-after "$WORK/evidence/$TAG-assets-after.txt" --output-hashes "$WORK/evidence/$TAG-output-hashes.txt" \
    "${FINAL_ARGS[@]}"
}
main "$@"
