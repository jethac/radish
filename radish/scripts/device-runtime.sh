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
LOADER_SHA=75699a38b1f26c1e8bfa2062e6593177bb07d4e3e346486d117fab8a0de0104a
TEMPLATE=$WORK/kokoro-core-shapes-instancenorm
TAIL=$WORK/kokoro-tail.bin
KIND=${1:-}
case $KIND in runtime-short|runtime-full) ;; *) echo 'usage: device-runtime.sh runtime-short|runtime-full [run-label] [--snake] [--vulkan|--cpu-packed] [--threads N] [--ort-threads N] [--max-frames N] [--repeat N]' >&2; exit 2 ;; esac
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
while [[ $# -gt 0 ]]; do
    case $1 in
        --snake)
            [[ $CORE_GRAPH == instancenorm ]] || { echo 'duplicate --snake' >&2; exit 2; }
            CORE_GRAPH=snake; TEMPLATE=$WORK/kokoro-core-shapes-snake ;;
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
TILED=${RADISH_NCNN_TILED:-1}
TILED32=${RADISH_NCNN_TILED32:-0}
SHARED32=${RADISH_NCNN_SHARED32:-0}
MIXED=${RADISH_NCNN_MIXED_STORAGE:-0}
RESHAPE_ALIAS=${RADISH_NCNN_RESHAPE_ALIAS:-1}
CACHE=${RADISH_NCNN_PIPELINE_CACHE:-1}
TRACE=${RADISH_NCNN_TRACE:-1}
PROFILE=${RADISH_PROFILE:-1}
BATCH=${RADISH_BATCH_CHUNKS:-4}
TILED_BATCH=${RADISH_TILED_BATCH_CHUNKS:-4}
[[ $TILED_BATCH =~ ^(1|2|4)$ ]] || { echo 'RADISH_TILED_BATCH_CHUNKS must be 1, 2 or 4' >&2; exit 2; }
GROUPS_PER_CHUNK=${RADISH_SHARED_GROUPS:-8}
for toggle in "$TILED" "$TILED32" "$SHARED32" "$MIXED" "$CACHE" "$TRACE" "$PROFILE" "$RESHAPE_ALIAS"; do
    [[ $toggle == 0 || $toggle == 1 ]] || { echo 'Radish toggles must be 0 or 1' >&2; exit 2; }
done
[[ $BATCH =~ ^[1-4]$ && $GROUPS_PER_CHUNK =~ ^[1-8]$ ]] || { echo 'batch must be 1..4 and shared groups 1..8' >&2; exit 2; }
SOURCE=$WORK/kokoro-$KIND
TAG=runtime-$KIND-$BACKEND-$RUN
[[ $CORE_GRAPH != snake ]] || TAG+=-snake
UNIT=radish-$TAG
REMOTE=/var/tmp/radish/$TAG
OUTPUT=$WORK/kokoro-$KIND-single-$BACKEND-$RUN
[[ $CORE_GRAPH != snake ]] || OUTPUT+=-snake
[[ ! -e $OUTPUT && ! -L $OUTPUT ]] || { echo 'host output already exists' >&2; exit 2; }
mkdir -p "$WORK/evidence"
[[ ! -e $WORK/evidence/$TAG-inputs.json && ! -e $WORK/evidence/$TAG.txt && ! -e $WORK/evidence/$TAG-icd.json ]] || { echo 'run label already has evidence' >&2; exit 2; }
python3 - "$SOURCE" "$MODELS" "$TEMPLATE" "$BINARY" "$DRIVER" "$TAIL" "$WORK/evidence/$TAG-inputs.json" "$WORK/evidence/$TAG-icd.json" "$REMOTE" "$BACKEND" "$THREADS" "$ORT_THREADS" "$MAX_FRAMES" "$REPEAT" "$TILED" "$SHARED32" "$MIXED" "$CACHE" "$TRACE" "$PROFILE" "$BATCH" "$GROUPS_PER_CHUNK" "$RESHAPE_ALIAS" "$TILED32" "$TILED_BATCH" "$LOADER" "$CORE_GRAPH" "$TOP/radish/scripts/validate-kokoro-snake-fixture.py" <<'PY'
import hashlib, importlib.util, json, pathlib, sys
source, models, template, binary, driver, tail, output, icd = map(pathlib.Path, sys.argv[1:9])
loader=pathlib.Path(sys.argv[26])
remote, backend = sys.argv[9:11]
threads, ort_threads, max_frames, repeat = map(int, sys.argv[11:15])
def require(ok, message):
    if not ok: raise ValueError(message)
def asset(path):
    require(path.is_file(), 'missing asset: '+str(path))
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1<<20), b''): digest.update(block)
    return {'bytes':path.stat().st_size, 'sha256':digest.hexdigest()}
pin='b13da99ce02f03fb6b605720c46d09b746063ddb651bfaf5156a770204ffc4e9'
capture=json.loads((source/'runtime.json').read_text())
graph_manifest=json.loads((models/'frontend-models.json').read_text())
require(capture['format']=='radish-kokoro-runtime-fixture-v1' and capture['source_sha256']==pin, 'unexpected input capture provenance')
require(graph_manifest['format']=='radish-kokoro-frontend-models-v1' and graph_manifest['source_sha256']==pin, 'unexpected frontend model provenance')
graph_pins={'frontend-only.onnx':(111933749,'56ba27008352a2191b631ebbf269bf4506d690a1b83d8ff8ff5524b11291ddd3'),
            'harmonic-source.onnx':(13810,'4bdcb9f2f54d6efbfdcefce384300352933cc71fa562930f23eaeb3443528fd2')}
graphs={}
for name,(size,digest) in graph_pins.items():
    graphs[name]=asset(models/name)
    require(graphs[name]=={'bytes':size,'sha256':digest}, 'changed graph: '+name)
    require(all(graph_manifest['graphs'][name][key]==value for key,value in graphs[name].items()), 'graph manifest mismatch')
inputs={name:asset(source/name) for name in ('ids.i64','style.f32','speed.f32')}
require(all(value==capture['files'][name] for name,value in inputs.items()), 'raw input differs from capture')
assets={name:{'path':str(path.resolve()),**asset(path)} for name,path in
        [('binary',binary),('driver',driver),('loader',loader),('param',template/'decoder.ncnn.param'),('weights',template/'decoder.ncnn.bin'),('tail',tail)]}
require(assets['loader']['bytes']==703696 and assets['loader']['sha256']=='75699a38b1f26c1e8bfa2062e6593177bb07d4e3e346486d117fab8a0de0104a', 'unexpected private Vulkan loader')
variant=sys.argv[27]
graph_pins={'instancenorm':('6499e4996cea79d28e9c72caab1e2f0ca7d9fab4420a6f198c7e197fb06d347f',1098,1307),
            'snake':('e01c56a4f23d6d3c8f92dbecb0c3fc37f6f74e9f305f58b82d5196278a450d38',906,1067)}
require(variant in graph_pins, 'unsupported core graph')
param_pin,layers,blobs=graph_pins[variant]
require(assets['param']['sha256']==param_pin, 'unexpected selected parameter template')
require((template/'decoder.ncnn.param').read_text().splitlines()[:2]==['7767517',f'{layers} {blobs}'], 'selected graph inventory mismatch')
core_graph={'variant':variant,'layers':layers,'blobs':blobs,'param_sha256':param_pin}
if variant=='snake':
    validator=pathlib.Path(sys.argv[28]).resolve()
    sys.dont_write_bytecode=True
    spec=importlib.util.spec_from_file_location('radish_snake_validation',validator)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    core_graph['host_validation']=module.validate(template)
    core_graph['validator']={'path':str(validator),**asset(validator)}
require(assets['weights']['bytes']==213042160 and assets['weights']['sha256']=='0f77cf77ff79a7724662f2e79a4d50284eb498abb2e519469a188e33f6c051ae', 'unexpected core weights')
require(assets['tail']['bytes']==1936 and assets['tail']['sha256']=='e04b5c7b07c51b98b8883ba4feb753dbcb5bad9260fc93d009aa7fd577e80749', 'unexpected waveform tail')
# Keep the ICD on one line intentionally: ncnn SIMPLEVK's line scanner then
# opens libvulkan.so.1 from our LD_LIBRARY_PATH. That pinned loader reads this
# VK_DRIVER_FILES manifest; ncnn must not accidentally dlopen the ICD directly.
with icd.open('x') as stream:
    json.dump({'file_format_version':'1.0.0','ICD':{'library_path':remote+'/libvulkan_radish.so','api_version':'1.0.0'}},stream);stream.write('\n')
assets['icd']={'path':str(icd.resolve()),**asset(icd)}
names=['RADISH_NCNN_TILED','RADISH_NCNN_SHARED32','RADISH_NCNN_MIXED_STORAGE','RADISH_NCNN_PIPELINE_CACHE','RADISH_NCNN_TRACE','RADISH_PROFILE','RADISH_BATCH_CHUNKS','RADISH_SHARED_GROUPS','RADISH_NCNN_RESHAPE_ALIAS','RADISH_NCNN_TILED32','RADISH_TILED_BATCH_CHUNKS']
environment=dict(zip(names,sys.argv[15:26]))
environment['VK_DRIVER_FILES']=remote+'/radish_icd.json'
frontend_inputs={'format':'radish-kokoro-device-frontend-inputs-v1','source_sha256':pin,
    'input_directory':str(source.resolve()),'runtime_manifest':asset(source/'runtime.json'),
    'model_directory':str(models.resolve()),'graphs':graphs,'inputs':inputs,
    'binary_path':str(binary.resolve()),'binary':asset(binary)}
record={'format':'radish-kokoro-device-runtime-inputs-v1','frontend_inputs':frontend_inputs,
        'assets':assets,'core_graph':core_graph,'environment':environment,'loader_environment':{'LD_LIBRARY_PATH':remote},
        'backend':backend,'threads':threads,'ort_threads':ort_threads,'max_frames':max_frames,'repeat':repeat,
        'remote_directory':remote,'scope':'Pinned single-process synthesis inputs; generation itself does not establish accuracy.'}
with output.open('x') as stream:json.dump(record,stream,indent=2);stream.write('\n')
print(f'Pinned graphs, raw inputs, {variant} parameters, weights and tail verified; binary/driver/ICD recorded.')
PY
exec 9>"${RADISH_DEVICE_LOCK:-$TOP/.device.lock}"
flock -w 1800 9
if [[ $CORE_GRAPH == snake ]]; then
    python3 - "$WORK/evidence/$TAG-inputs.json" <<'PY'
import hashlib,importlib.util,json,pathlib,sys
r=json.load(open(sys.argv[1]));g=r['core_graph'];v=g['validator'];path=pathlib.Path(v['path'])
data=path.read_bytes()
if len(data)!=v['bytes'] or hashlib.sha256(data).hexdigest()!=v['sha256']:
    raise ValueError('Snake validator changed while waiting for device lock')
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('radish_snake_validation',path)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
if module.validate(pathlib.Path(r['assets']['param']['path']).parent)!=g['host_validation']:
    raise ValueError('Snake validation changed while waiting for device lock')
PY
fi
ADB=(adb -s "$SERIAL")
PULL_STAGE=
CHECKED=false
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
    if ! $CHECKED; then collect_evidence || result=1; fi
    if [[ -n $PULL_STAGE ]]; then rm -rf -- "$PULL_STAGE"; fi
    exit "$result"
}
"${ADB[@]}" shell 'systemctl is-active straylight-listen straylight-voice'
"${ADB[@]}" shell dmesg > "$WORK/evidence/$TAG-dmesg-before.txt"
trap finish EXIT
"${ADB[@]}" shell cat /proc/meminfo > "$WORK/evidence/$TAG-mem-before.txt"
"${ADB[@]}" shell 'df -Pk /var/tmp' > "$WORK/evidence/$TAG-disk-before.txt"
AVAILABLE=$(awk '$1 == "MemAvailable:" {print $2}' "$WORK/evidence/$TAG-mem-before.txt")
[[ $AVAILABLE =~ ^[0-9]+$ && $AVAILABLE -ge $(((640 + 192) * 1024)) ]] || { echo 'insufficient available memory for 640MiB cap plus 192MiB reserve' >&2; exit 1; }
WEIGHT_SHA=0f77cf77ff79a7724662f2e79a4d50284eb498abb2e519469a188e33f6c051ae
DEVICE_WEIGHTS=/var/tmp/radish/kokoro-core-shapes-instancenorm/decoder.ncnn.bin
UPLOAD_WEIGHTS=false
if ! "${ADB[@]}" shell "test -f $DEVICE_WEIGHTS"; then
    DEVICE_WEIGHTS=/var/tmp/radish/kokoro-core-$WEIGHT_SHA.bin
    if ! "${ADB[@]}" shell "test -f $DEVICE_WEIGHTS"; then UPLOAD_WEIGHTS=true; fi
fi
if ! $UPLOAD_WEIGHTS; then
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
DISK_NEEDED=$(python3 - "$WORK/evidence/$TAG-inputs.json" "$UPLOAD_WEIGHTS" "$WORK/evidence/$TAG-graphs.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]));t=r['max_frames'];f=120*t+1
graphs=json.load(open(sys.argv[3]))
payload=sum(a['bytes'] for n,a in r['assets'].items() if n!='weights')+sum(a['bytes'] for a in graphs.values() if a['method']=='upload')+sum(a['bytes'] for a in r['frontend_inputs']['inputs'].values())
if sys.argv[2]=='true':payload+=r['assets']['weights']['bytes']
# Eight boundary files, final core/waveform, metadata and64MiB disk reserve.
output=4*(512*t+4*t+256+1+22*f+512+22*f+600*t)+8*512+1024*1024
print(max(256*1024,(payload+output+64*1024*1024+1023)//1024))
PY
)
DISK=$(awk 'NR == 2 {print $4}' "$WORK/evidence/$TAG-disk-before.txt")
[[ $DISK =~ ^[0-9]+$ && $DISK_NEEDED =~ ^[0-9]+$ && $DISK -ge $DISK_NEEDED ]] || { echo "insufficient device disk; need ${DISK_NEEDED}KiB for payload/output/reserve" >&2; exit 1; }
"${ADB[@]}" shell 'test -r /usr/local/lib/straylight-listen/libonnxruntime.so'
"${ADB[@]}" shell "mkdir -p /var/tmp/radish && mkdir $REMOTE && mkdir $REMOTE/models $REMOTE/input"
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
for file in ids.i64 style.f32 speed.f32; do "${ADB[@]}" push "$SOURCE/$file" "$REMOTE/input/$file"; done
echo "Runtime backend=$BACKEND graph=$CORE_GRAPH repeat=$REPEAT threads=$THREADS ort_threads=$ORT_THREADS; shared weights=$DEVICE_WEIGHTS"
echo "GPU controls: tiled=$TILED tiled32=$TILED32 batch=$BATCH tiled_batch=$TILED_BATCH shared_groups=$GROUPS_PER_CHUNK shared32=$SHARED32 mixed=$MIXED cache=$CACHE trace=$TRACE profile=$PROFILE reshape_alias=$RESHAPE_ALIAS"
"${ADB[@]}" shell "chmod 755 $REMOTE/radish-kokoro-runtime && systemd-run --unit=$UNIT --wait --pipe --collect -p MemoryMax=640M -p MemorySwapMax=128M -p RuntimeMaxSec=300 -p LimitCORE=0 --setenv=RADISH_NCNN_TILED=$TILED --setenv=RADISH_NCNN_TILED32=$TILED32 --setenv=RADISH_NCNN_SHARED32=$SHARED32 --setenv=RADISH_NCNN_MIXED_STORAGE=$MIXED --setenv=RADISH_NCNN_RESHAPE_ALIAS=$RESHAPE_ALIAS --setenv=RADISH_NCNN_PIPELINE_CACHE=$CACHE --setenv=RADISH_NCNN_TRACE=$TRACE --setenv=RADISH_PROFILE=$PROFILE --setenv=RADISH_BATCH_CHUNKS=$BATCH --setenv=RADISH_TILED_BATCH_CHUNKS=$TILED_BATCH --setenv=RADISH_SHARED_GROUPS=$GROUPS_PER_CHUNK --setenv=LD_LIBRARY_PATH=$REMOTE --setenv=VK_DRIVER_FILES=$REMOTE/radish_icd.json $REMOTE/radish-kokoro-runtime --$BACKEND --threads $THREADS --ort-threads $ORT_THREADS --max-frames $MAX_FRAMES --repeat $REPEAT $REMOTE/models $REMOTE/input $REMOTE/decoder.ncnn.param $DEVICE_WEIGHTS $REMOTE/kokoro-tail.bin $REMOTE/output" 2>&1 | tee "$WORK/evidence/$TAG.txt"
if rg -n 'vkCreateComputePipelines failed|new_pipeline failed' "$WORK/evidence/$TAG.txt"; then exit 1; fi
PULL_STAGE=$(mktemp -d "$WORK/.runtime-pull-$KIND-XXXXXX")
"${ADB[@]}" pull "$REMOTE/output" "$PULL_STAGE/output"
collect_evidence
verify_remote_graphs "$WORK/evidence/$TAG-graphs-after.txt"
python3 - "$PULL_STAGE/output" "$OUTPUT" "$WORK/evidence/$TAG-inputs.json" "$DEVICE_WEIGHTS" "$WORK/evidence/$TAG-graphs.json" \
    "$WORK/evidence/$TAG.txt" "$WORK/evidence/$TAG-service.txt" "$WORK/evidence/$TAG-weights.txt" "$WORK/evidence/$TAG-loader.txt" "$WORK/evidence/$TAG-param.txt" \
    "$WORK/evidence/$TAG-graphs.json" "$WORK/evidence/$TAG-graphs-before.txt" "$WORK/evidence/$TAG-graphs-after.txt" \
    "$WORK/evidence/$TAG-dmesg-before.txt" "$WORK/evidence/$TAG-dmesg-after.txt" \
    "$WORK/evidence/$TAG-mem-before.txt" "$WORK/evidence/$TAG-mem-after.txt" \
    "$WORK/evidence/$TAG-disk-before.txt" "$WORK/evidence/$TAG-disk-after.txt" <<'PY'
import array,ctypes,hashlib,importlib.util,json,math,os,pathlib,sys
stage,output,provenance=map(pathlib.Path,sys.argv[1:4]);shared_weights=sys.argv[4]
shared_graphs=json.loads(pathlib.Path(sys.argv[5]).read_text())
def require(ok,message):
    if not ok:raise ValueError(message)
def asset(path):
    require(path.is_file() and not path.is_symlink(),'invalid file: '+str(path))
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1<<20),b''):digest.update(block)
    return {'bytes':path.stat().st_size,'sha256':digest.hexdigest()}
def floats(path,count):
    require(path.stat().st_size==4*count,'wrong float count: '+str(path))
    values=array.array('f');values.frombytes(path.read_bytes())
    if sys.byteorder!='little':values.byteswap()
    require(all(math.isfinite(value) for value in values),'nonfinite values: '+str(path))
    return values
record=json.loads(provenance.read_text());original=record['frontend_inputs'];remote=record['remote_directory']
require(set(shared_graphs)==set(original['graphs']),'unexpected reused graph inventory')
for name,entry in shared_graphs.items():
    require({key:entry[key] for key in ('bytes','sha256')}==original['graphs'][name]
            and entry['remote_path']==remote+'/models/'+name and entry['method'] in ('hardlink','upload'),
            'remote graph provenance differs from pinned inputs: '+name)
require({p.name for p in stage.iterdir()}=={'boundary','core.f32','waveform.f32','runtime.json'},'unexpected runtime output inventory')
boundary=stage/'boundary';front=json.loads((boundary/'frontend.json').read_text());run=json.loads((stage/'runtime.json').read_text())
require(front['format']=='radish-kokoro-frontend-boundary-v1' and front['ort_api']==24 and front['optimization']=='disabled','unexpected frontend metadata')
require(front['ort_library']=='/usr/local/lib/straylight-listen/libonnxruntime.so' and front['threads']==record['ort_threads'],'unexpected frontend runtime configuration')
require(front['frontend_graph']==remote+'/models/frontend-only.onnx' and front['source_graph']==remote+'/models/harmonic-source.onnx' and front['input_directory']==remote+'/input','unexpected frontend path provenance')
t=front['T'];require(type(t) is int and 1<=t<=record['max_frames'],'invalid frontend T')
frames,samples=120*t+1,600*t;tokens=original['inputs']['ids.i64']['bytes']//8
require(front['frames']==frames and front['samples']==samples and front['sample_rate']==24000 and front['tokens_with_pads']==tokens,'inconsistent frontend dimensions')
sizes={'ids.i64':tokens*8,'style.f32':256*4,'speed.f32':4,'asr.f32':512*t*4,'f0.f32':2*t*4,'n.f32':2*t*4,'har.f32':22*frames*4,'durations.f32':tokens*4}
require({p.name for p in boundary.iterdir()}==set(sizes)|{'frontend.json'},'unexpected boundary inventory')
raw={}
for name,size in sizes.items():
    raw[name]=asset(boundary/name);require(raw[name]['bytes']==size,'wrong raw length: '+name)
    if name.endswith('.f32'):
        values=floats(boundary/name,size//4)
        if name=='durations.f32':require(all(v>=1 and v==math.floor(v) for v in values) and sum(values)==t,'invalid durations')
for name,expected in original['inputs'].items():
    require(raw[name]==expected and asset(pathlib.Path(original['input_directory'])/name)==expected,'input changed: '+name)
require(asset(pathlib.Path(original['input_directory'])/'runtime.json')==original['runtime_manifest'],'input provenance changed')
for name,expected in original['graphs'].items():require(asset(pathlib.Path(original['model_directory'])/name)==expected,'graph changed: '+name)
for name,expected in record['assets'].items():require(asset(pathlib.Path(expected['path']))=={k:expected[k] for k in ('bytes','sha256')},'asset changed during run: '+name)
core_graph=record['core_graph']
if core_graph['variant']=='snake':
    validator=core_graph['validator'];path=pathlib.Path(validator['path'])
    require(asset(path)=={k:validator[k] for k in ('bytes','sha256')},'Snake validator changed during run')
    sys.dont_write_bytecode=True
    spec=importlib.util.spec_from_file_location('radish_snake_validation',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    require(module.validate(pathlib.Path(record['assets']['param']['path']).parent)==core_graph['host_validation'],
            'Snake fixture validation changed during run')
require(run['format']=='radish-kokoro-runtime-v1' and run['backend']==record['backend'],'unexpected runtime schema/backend')
require(run['T']==t and run['frames']==frames and run['samples']==samples and run['sample_rate']==24000 and run['shape_blobs']==core_graph['blobs'],'runtime shape mismatch')
require(run['threads']==record['threads'] and run['ort_threads']==record['ort_threads'] and run['repeat']==record['repeat'] and run['ort_version']==front['ort_version'],'runtime configuration mismatch')
require(run['environment']==record['environment'],'runtime environment mismatch')
require(len(run['extraction_seconds'])==record['repeat'],'repeat timing count mismatch')
times=[run[key] for key in ('frontend_wall_seconds','shape_seconds','core_load_seconds','core_scope_seconds','tail_seconds','synthesis_seconds')]+run['extraction_seconds']
require(all(isinstance(value,(int,float)) and math.isfinite(value) and value>=0 for value in times),'invalid runtime timing')
floats(stage/'core.f32',22*frames);floats(stage/'waveform.f32',samples)
evidence={str(pathlib.Path(name).resolve()):asset(pathlib.Path(name)) for name in sys.argv[6:]}
capture={'format':'radish-kokoro-device-frontend-v1','inputs':original,'frontend':front,'files':raw,
         'frontend_manifest':asset(boundary/'frontend.json'),'evidence':evidence,
         'device_checks':'Both services active; no new GPU fault/recovery messages; memory/swap and stage timings recorded in evidence.',
         'scope':'Fresh frontend/source from one private synthesis process. Its generated core/waveform require separate same-boundary accuracy validation.'}
with (boundary/'device-capture.json').open('x') as stream:json.dump(capture,stream,indent=2);stream.write('\n')
result={'format':'radish-kokoro-device-runtime-v1','inputs':record,'runtime':run,'shared_device_weights':shared_weights,'shared_device_graphs':shared_graphs,
        'files':{name:asset(stage/name) for name in ('core.f32','waveform.f32','runtime.json')},
        'boundary_capture_manifest':asset(boundary/'device-capture.json'),'evidence':evidence,
        'accuracy':'Pending separate comparison against CPU references generated from this exact captured boundary. No accuracy gate ran during synthesis.'}
with (stage/'device-runtime.json').open('x') as stream:json.dump(result,stream,indent=2);stream.write('\n')
rename=ctypes.CDLL(None,use_errno=True).renameat2
rename.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint];rename.restype=ctypes.c_int
if rename(-100,os.fsencode(stage),-100,os.fsencode(output),1)!=0:
    error=ctypes.get_errno();raise OSError(error,os.strerror(error),str(output))
print(f'Published unreferenced {record["backend"]} synthesis: T={t}, {samples} samples: {output}')
print('Accuracy validation remains pending; boundary/ is compatible with import-kokoro-device-boundary.py.')
PY
}
main "$@"
