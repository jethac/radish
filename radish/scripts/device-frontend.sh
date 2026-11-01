#!/usr/bin/env bash
main() {
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${RADISH_WORK:-/run/media/jethac/Shared/radish}
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
MODELS=${RADISH_FRONTEND_MODELS:-$WORK/kokoro-frontend-models}
BINARY=${RADISH_FRONTEND_BINARY:-$WORK/kokoro-frontend}
THREADS=${RADISH_ORT_THREADS:-4}
MAX_FRAMES=${RADISH_FRONTEND_MAX_FRAMES:-512}
KIND=${1:-}
RUN=${2:-$(date +%s%N)}
[[ $# -ge 1 && $# -le 2 ]] || { echo 'usage: device-frontend.sh runtime-short|runtime-full [run-label]' >&2; exit 2; }
case $KIND in runtime-short|runtime-full) ;; *) echo 'invalid runtime input kind' >&2; exit 2 ;; esac
[[ $RUN =~ ^[A-Za-z0-9_-]{1,48}$ ]] || { echo 'invalid run label' >&2; exit 2; }
[[ $THREADS =~ ^[1-6]$ ]] || { echo 'RADISH_ORT_THREADS must be 1..6' >&2; exit 2; }
[[ $MAX_FRAMES =~ ^[1-9][0-9]{0,3}$ && $MAX_FRAMES -le 4096 ]] || { echo 'RADISH_FRONTEND_MAX_FRAMES must be 1..4096' >&2; exit 2; }
SOURCE=$WORK/kokoro-$KIND
TAG=frontend-$KIND-$RUN
UNIT=radish-$TAG
REMOTE=/var/tmp/radish/$TAG
OUTPUT=$WORK/kokoro-$KIND-device-frontend-$RUN
[[ ! -e $OUTPUT && ! -L $OUTPUT ]] || { echo 'host output already exists' >&2; exit 2; }
test -f "$BINARY"
mkdir -p "$WORK/evidence"
[[ ! -e $WORK/evidence/$TAG-inputs.json && ! -e $WORK/evidence/$TAG.txt ]] || { echo 'run label already has evidence' >&2; exit 2; }
python3 - "$SOURCE" "$MODELS" "$BINARY" "$WORK/evidence/$TAG-inputs.json" <<'PY'
import hashlib, json, pathlib, sys
source, models, binary, output = map(pathlib.Path, sys.argv[1:])
def asset(path):
    return {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
pin = 'b13da99ce02f03fb6b605720c46d09b746063ddb651bfaf5156a770204ffc4e9'
capture = json.loads((source/'runtime.json').read_text())
graphs = json.loads((models/'frontend-models.json').read_text())
assert capture['format'] == 'radish-kokoro-runtime-fixture-v1' and capture['source_sha256'] == pin, 'unexpected input capture provenance'
assert graphs['format'] == 'radish-kokoro-frontend-models-v1' and graphs['source_sha256'] == pin, 'unexpected graph provenance'
expected = {'frontend-only.onnx': (111933749, '56ba27008352a2191b631ebbf269bf4506d690a1b83d8ff8ff5524b11291ddd3'),
            'harmonic-source.onnx': (13810, '4bdcb9f2f54d6efbfdcefce384300352933cc71fa562930f23eaeb3443528fd2')}
records = {}
for name, (size, digest) in expected.items():
    actual = asset(models/name)
    assert actual == {'bytes': size, 'sha256': digest}, 'changed graph: '+name
    assert all(graphs['graphs'][name][key] == value for key, value in actual.items()), 'graph manifest mismatch'
    records[name] = actual
inputs = {}
for name in ('ids.i64', 'style.f32', 'speed.f32'):
    inputs[name] = asset(source/name)
    assert inputs[name] == capture['files'][name], 'changed raw input: '+name
record = {'format': 'radish-kokoro-device-frontend-inputs-v1', 'source_sha256': pin,
          'input_directory': str(source.resolve()), 'runtime_manifest': asset(source/'runtime.json'),
          'model_directory': str(models.resolve()), 'graphs': records, 'inputs': inputs,
          'binary_path': str(binary.resolve()), 'binary': asset(binary)}
with output.open('x') as stream: json.dump(record, stream, indent=2); stream.write('\n')
print('Pinned frontend/source graphs and runtime raw inputs verified.')
PY
exec 9>"${RADISH_DEVICE_LOCK:-$TOP/.device.lock}"
flock -w 1800 9
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
    python3 "$TOP/radish/scripts/check-gpu-log.py" \
        "$WORK/evidence/$TAG-dmesg-before.txt" "$WORK/evidence/$TAG-dmesg-after.txt" || result=1
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
[[ $AVAILABLE =~ ^[0-9]+$ && $AVAILABLE -ge $(((640 + 192) * 1024)) ]] || {
    echo 'insufficient available memory for 640MiB cap plus 192MiB reserve' >&2; exit 1
}
DISK=$(awk 'NR == 2 {print $4}' "$WORK/evidence/$TAG-disk-before.txt")
[[ $DISK =~ ^[0-9]+$ && $DISK -ge $((256 * 1024)) ]] || {
    echo 'less than 256MiB available on device /var/tmp' >&2; exit 1
}
"${ADB[@]}" shell 'test -r /usr/local/lib/straylight-listen/libonnxruntime.so'
"${ADB[@]}" shell "mkdir -p /var/tmp/radish && mkdir $REMOTE && mkdir $REMOTE/input"
"${ADB[@]}" push "$BINARY" "$REMOTE/kokoro-frontend"
for file in frontend-only.onnx harmonic-source.onnx; do
    "${ADB[@]}" push "$MODELS/$file" "$REMOTE/$file"
done
for file in ids.i64 style.f32 speed.f32; do
    "${ADB[@]}" push "$SOURCE/$file" "$REMOTE/input/$file"
done
"${ADB[@]}" shell "chmod 755 $REMOTE/kokoro-frontend && systemd-run --unit=$UNIT --wait --pipe --collect -p MemoryMax=640M -p MemorySwapMax=128M -p RuntimeMaxSec=120 -p LimitCORE=0 $REMOTE/kokoro-frontend $REMOTE/frontend-only.onnx $REMOTE/harmonic-source.onnx $REMOTE/input $REMOTE/boundary --threads $THREADS --max-frames $MAX_FRAMES" 2>&1 | tee "$WORK/evidence/$TAG.txt"
PULL_STAGE=$(mktemp -d "$WORK/.frontend-pull-$KIND-XXXXXX")
"${ADB[@]}" pull "$REMOTE/boundary" "$PULL_STAGE/boundary"
collect_evidence
python3 - "$PULL_STAGE/boundary" "$OUTPUT" "$WORK/evidence/$TAG-inputs.json" "$REMOTE" "$THREADS" "$MAX_FRAMES" \
    "$WORK/evidence/$TAG.txt" "$WORK/evidence/$TAG-service.txt" \
    "$WORK/evidence/$TAG-dmesg-before.txt" "$WORK/evidence/$TAG-dmesg-after.txt" \
    "$WORK/evidence/$TAG-mem-before.txt" "$WORK/evidence/$TAG-mem-after.txt" \
    "$WORK/evidence/$TAG-disk-before.txt" "$WORK/evidence/$TAG-disk-after.txt" <<'PY'
import array, ctypes, hashlib, json, math, os, pathlib, sys
stage, output, provenance = map(pathlib.Path, sys.argv[1:4])
remote, threads, bound = sys.argv[4], int(sys.argv[5]), int(sys.argv[6])
inputs = json.loads(provenance.read_text())
manifest = json.loads((stage/'frontend.json').read_text())
def asset(path):
    return {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
assert manifest['format'] == 'radish-kokoro-frontend-boundary-v1' and manifest['ort_api'] == 24
assert manifest['ort_library'] == '/usr/local/lib/straylight-listen/libonnxruntime.so'
assert manifest['threads'] == threads and manifest['optimization'] == 'disabled'
assert manifest['frontend_graph'] == remote+'/frontend-only.onnx' and manifest['source_graph'] == remote+'/harmonic-source.onnx'
assert manifest['input_directory'] == remote+'/input'
t = manifest['T']; assert type(t) is int and 1 <= t <= bound
frames, samples = 120*t+1, 600*t
assert manifest['frames'] == frames and manifest['samples'] == samples and manifest['sample_rate'] == 24000
count = inputs['inputs']['ids.i64']['bytes']//8
assert manifest['tokens_with_pads'] == count
sizes = {'ids.i64': count*8, 'style.f32': 256*4, 'speed.f32': 4, 'asr.f32': 512*t*4,
         'f0.f32': 2*t*4, 'n.f32': 2*t*4, 'har.f32': 22*frames*4, 'durations.f32': count*4}
assert {p.name for p in stage.iterdir()} == set(sizes) | {'frontend.json'}, 'unexpected pulled files'
records = {}
for name, size in sizes.items():
    file = stage/name
    assert file.is_file() and not file.is_symlink() and file.stat().st_size == size, 'unexpected raw size/type: '+name
    records[name] = asset(file)
    if name.endswith('.f32'):
        values = array.array('f'); values.frombytes(file.read_bytes())
        if sys.byteorder != 'little': values.byteswap()
        assert all(math.isfinite(value) for value in values), 'nonfinite '+name
        if name == 'durations.f32': assert all(value >= 1 and value == math.floor(value) for value in values) and sum(values) == t
for name, expected in inputs['inputs'].items():
    assert records[name] == expected and asset(pathlib.Path(inputs['input_directory'])/name) == expected, 'raw input changed: '+name
assert asset(pathlib.Path(inputs['input_directory'])/'runtime.json') == inputs['runtime_manifest']
assert asset(pathlib.Path(inputs['binary_path'])) == inputs['binary'], 'helper changed during run'
for name, expected in inputs['graphs'].items():
    assert asset(pathlib.Path(inputs['model_directory'])/name) == expected, 'graph changed during run'
for phase in ('frontend', 'source'):
    assert all(math.isfinite(manifest[phase][key]) and manifest[phase][key] >= 0 for key in ('load_seconds', 'run_seconds'))
record = {'format': 'radish-kokoro-device-frontend-v1', 'inputs': inputs, 'frontend': manifest,
          'files': records, 'frontend_manifest': asset(stage/'frontend.json'),
          'evidence': {str(pathlib.Path(name).resolve()): asset(pathlib.Path(name)) for name in sys.argv[7:]},
          'device_checks': 'Both services active; no new GPU fault/recovery messages. See evidence files for memory/swap and timing.',
          'scope': 'Fresh on-device CPU frontend/source only; this is not an ORT core reference, ncnn fixture, or complete synthesis validation.'}
with (stage/'device-capture.json').open('x') as stream: json.dump(record, stream, indent=2); stream.write('\n')
libc = ctypes.CDLL(None, use_errno=True)
rename = libc.renameat2
rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
rename.restype = ctypes.c_int
if rename(-100, os.fsencode(stage), -100, os.fsencode(output), 1) != 0:
    error = ctypes.get_errno(); raise OSError(error, os.strerror(error), str(output))
print(f'Published fresh tablet frontend boundary: T={t}, frames={frames}, samples={samples}: {output}')
PY
}
main "$@"
