# Kokoro runtime integration audit

2026-11-01: one private tablet executable now runs CPU frontend/source, metadata
shape propagation, the FP32 Vulkan core and the portable waveform tail from
token IDs/style/speed. With the opt-in Snake graph, complete T=78 and T=213 runs
take **33.764051 s** and **63.528396 s** synthesis time, with **383.3 MiB** and
**458 MiB** peak memory and zero swap. T78 uses the earlier batch-four setting;
the latest T213 result uses batch eight. No new
GPU faults occurred, and both appliance services remained active.
Every core value and waveform sample passes
the unchanged gates against host ORT references computed afterward from each
run's exact eight boundary files. No host work occurs between inference stages.
The six-thread CPU baseline also passes T213 in **39.789900 s**, using identical
boundary tensors; Vulkan remains slower. InstanceNorm remains the default;
select Snake explicitly with `--snake`. Resident reuse, cancellation and live
application integration remain unfinished.
Earlier staged and fixed-core evidence is retained below and in
[NOTES.md](NOTES.md).

## Implemented route

Keep harmonic generation in the CPU ONNX Runtime frontend, return its
`har` tensor with `asr`, F0, N and durations, feed those tensors plus style into
ncnn, then run the portable CPU waveform tail. This reuses an existing runtime
and the original source graph; no new GPU feature is needed.

The private benchmark uses the FP32 frontend from the original model.
The existing production frontend is uint8 and would introduce a separate model
quality comparison. Do not change the live listener or its services for bring-up.

## Pins and source locations

Paths below are relative to `/home/jethac/Projects/lenovo-8` unless absolute.
Models, fixtures and generated artifacts remain private.

| Source | Pin / role |
| --- | --- |
| `streams/tts/work/kokoro/kokoro-v1.0-model-dur.onnx` | SHA256 `b13da99ce02f03fb6b605720c46d09b746063ddb651bfaf5156a770204ffc4e9`; original FP32 model with duration output |
| `streams/kokoro-gpu/work/b-en.npz` | SHA256 `944f95f2202263f6a79f39f107438a444f6a3949ed3e0403f3772e14d8627c77`; saved boundary/source/waveform fixture |
| `streams/kokoro-gpu/scripts/kokoro-ref.py` | Original boundary capture; disables ORT graph optimization; documents zero phases/noise |
| `streams/kokoro-gpu/scripts/kokoro-front-onnx.py` | Existing CPU frontend extraction, including uint8 F0/N dequantization |
| `/home/jethac/Projects/straylight-voice` | Inspected HEAD `e71bfea2a617cc9b2fae2d5e9b6525e9426e70e5` |
| `listen/src/tts/kokoro_gpu.rs` in that checkout | Existing ORT frontend, tensor handoff, duration return and cancellation hooks |
| `gpu/src/kokoro.c:449` in that checkout | Existing double-precision harmonic generator; deliberately differs from this export |

Host checks used `streams/tts/downloads/kokoro-venv/bin/python`, ORT **1.31.1**.
The standalone tablet helper reports installed ORT **1.28.2**, from
sherpa-onnx v2.0.1. It opens the installed library read-only through C API24;
`build-frontend.sh` pins official v1.24.4 header downloads outside the repository.

## Harmonic boundary and verified extraction

The dependency closure from `/encoder/Reshape_output_0` (F0) to
`/decoder/decoder/generator/Concat_3_output_0` (`har`) contains **57 nodes and
24 initializers**, with no other runtime input. A graph returning both `har` and
`/decoder/decoder/generator/m_source/l_tanh/Tanh_output_0` (`src`) serializes to
**13,800 bytes** in the initial in-memory audit, using the original opset20.
The implemented exporter now writes the same node/initializer closure as a
private **13,810-byte** artifact; graph/shape naming accounts for the metadata
difference. Models and captures remain on the shared scratch disk.

For decoder length T, F0 is `[1,2T]`, `src` is `[1,600T,1]`, and `har` is
`[1,22,120T+1]`. Audio is 600T samples at 24 kHz. For the original T=213 fixture,
feeding saved F0 reproduced all **127800 source samples and 562342 harmonic
values bit-for-bit**. Both disabled and full ORT optimization passed. One-thread
host source timings were 0.127 s first / 0.121 s warm; these are not tablet
measurements. Synthetic F0 lengths 2/4/8 produced the expected dimensions and
finite outputs, demonstrating that the extracted source itself is dynamic.

There are no random operators anywhere in the original model. Source initial
phases and additive noise are zero. The exported sequence is:

1. Nearest/asymmetric/floor F0 upsampling by 300; nine harmonic multiples,
   division by 24000, and subtraction of floor.
2. Half-pixel linear downsampling by the stored FP32 1/300, inclusive FP32
   cumulative sum, then separate multiplications by 2, stored pi and 300.
3. Half-pixel linear upsampling by 300, sine, amplitude 0.1, voiced mask F0>10,
   learned 9-to-1 projection plus bias, and tanh.
4. Reflect padding of ten samples per side; one-sided STFT with the stored
   periodic Hann window, FFT20 and hop5. Concatenate eleven magnitudes and
   eleven phases. Phase uses division/atan and explicit quadrant branches.

The existing GLES generator uses double phase accumulation and a conventional
DFT/atan2 path. Its source can sound plausible while differing substantially
from the export; copying it unchanged would invalidate the current waveform
comparison. A standalone math port would need to preserve FP32 operation order,
interpolation, STFT numerics and zero/sign phase behavior. Keeping ORT avoids
making that port a prerequisite.

## Frontend evidence and validation boundary

An in-memory FP32 frontend plus source has 1258 nodes and approximately 111.6 MB
of serialized initializers. Inputs are token IDs `[1,L]`, style `[1,256]` and
speed `[1]`; outputs are asr, F0, N, har and `/encoder/Clip_output_0` durations.
The original fixture's 82 token IDs/style/speed1 produced T=213 and duration sum
213 on the host, with all finite outputs.

Fresh asr was bit-identical to the fixture, but F0 differed by up to
3.0517578125e-5 and N by 1.9073486328125e-6, despite disabled optimization.
Tiny F0 changes affect the export's large FP32 phase arguments; harmonic phase
differences also include wrapping at 2pi. Matching the old frontend's numerical
behavior was not pursued further.

For runtime comparison, compute one fresh frontend/source boundary and feed
**the same tensors to CPU and GPU cores**, then compare both complete waveform
paths. Preserve the original fixed-fixture checks separately. Do not relax the
existing core gate (`0.002 + 0.002*abs(reference)`) or waveform gate
(`2e-4 + 2e-3*abs(reference)`, SNR at least 50 dB) to hide changed inputs.

## Implemented workflow and evidence

`scripts/export-kokoro-runtime.py export MODEL NEW_MODELS_DIR` verifies the
pinned original model and writes four reusable dynamic graphs:

| Graph | Nodes | Bytes | Boundary |
| --- | ---: | ---: | --- |
| `frontend.onnx` | 1258 | 111946804 | IDs/style/speed to asr/F0/N/har/durations |
| `harmonic-source.onnx` | 57 | 13810 | F0 to source and har |
| `decoder.onnx` | 1161 | 213353855 | Fresh asr/F0/N/style/har to core output |
| `waveform-tail.onnx` | 45 | 13109 | Core output to waveform |

Its `capture MODELS_DIR INPUT_NPZ NEW_CAPTURE_DIR` command reads only IDs and
style from the archive, runs the frontend, independently checks its har against
the source-only graph, and computes original CPU core/tail references using
those same fresh tensors. `--token-limit 12` selects twelve nonpad prefix tokens
while preserving both padding tokens. `runtime.json` records dimensions,
runtime/options, graph and raw-file hashes, and unchanged accuracy gates.
Existing destinations and an unexpected model hash are rejected.

Private artifacts are under `/run/media/jethac/Shared/radish`:

| Capture | T / core frames | Core values / waveform samples | ncnn CPU max error |
| --- | --- | --- | ---: |
| `kokoro-runtime-full` | 213 / 25561 | 562342 / 127800 | 4.95910645e-5 |
| `kokoro-runtime-short` | 78 / 9361 | 205942 / 46800 | 3.0040741e-5 |
| `kokoro-runtime-device-short` | 78 / 9361 | 205942 / 46800 | 4.93526459e-5 |

`scripts/prepare-kokoro-runtime-ncnn.py RUNNER CAPTURE TEMPLATE NEW_DIR` validates
the complete capture and graph manifests, the pinned unhinted ncnn parameters
and weights, then captures all **1739 logical shapes** and annotates **1368
layers**. Both captures pass the original ORT gate before/after annotation,
with byte-identical ncnn CPU outputs. Immutable raw tensors/weights are linked;
parameters and mutable outputs are separate. Success requires
`runtime-ncnn.json`; a failed assembly retains diagnostic logs without that
success marker. The validated directories are `kokoro-runtime-{full,short}-ncnn`.
`reference-waveform.f32` always denotes the fresh original ORT tail reference in
these directories, not the older b-en waveform.

The model helper now infers T from raw sizes, verifies dependent dimensions and
rejects stale boundary shape hints. The waveform helper supports explicit
`--runtime-frames` using the existing pinned inverse-basis parameters; the
original fixed-fixture invocation remains unchanged. T78 and T213 host tail
checks pass the existing waveform gates. This demonstrates these two lengths,
not arbitrary model/shape support.

`runtime-short-ncnn-device.log` records the first fresh T78 FP32 GPU core run:
**205942 outputs**, max error **9.29832458e-5**, relative RMS **8.99170127e-7**,
zero mismatches, **38.332986 s** extraction/transfers, **321.1 MiB** peak and
zero swap. Both live services remain active and no new GPU fault is recorded.
Frontend/source generation was on the host; this timing covers the core only.

The packed-FP16 investigation and convolution/normalization candidates have
separate evidence and limitations in [NOTES.md](NOTES.md).

## Tablet frontend and exact-boundary import

`scripts/export-kokoro-frontend.py MODEL NEW_MODELS_DIR` recreates a separate
**1201-node / 111933749-byte** `frontend-only.onnx`, SHA256
`56ba27008352a2191b631ebbf269bf4506d690a1b83d8ff8ff5524b11291ddd3`,
plus the pinned 13810-byte harmonic-source graph. The existing four-graph
export remains unchanged. The pure frontend returns asr/F0/N/durations and
excludes harmonic work; the helper releases its session before running the
small source graph. No source formula is reimplemented.

`tests/kokoro_frontend.cpp` reads `ids.i64`, `style.f32`, and `speed.f32`, checks
tokens, types, shapes, finite values and duration sums, and writes eight raw
files plus `frontend.json` into a new directory. Publication uses atomic
`renameat2(NOREPLACE)`. Host ORT1.31.1 runs reproduce all eight original host
capture files byte-for-byte for both T78 and T213.

`scripts/device-frontend.sh runtime-short first` runs the helper in a fresh
private tablet directory with MemoryMax640M, SwapMax128M and RuntimeMax120s.
It checks 832MiB available memory and 256MiB free disk before pushing, preserves
the shared lock override, checks both services and new kernel faults, and pulls
to an exclusive host directory with raw/model/input/evidence hashes.
`runtime-short-frontend-device-first.log` records:

| Stage | Session load | Inference |
| --- | ---: | ---: |
| CPU frontend | 3.554571 s | 0.468618 s |
| Harmonic source | 0.011151 s | 0.248083 s |

The complete boundary stage took **4.665090 s**, with **181.3MiB peak memory**,
zero swap, both services active and no new GPU fault. This is standalone T78
frontend/source evidence; model loading is included in the total.

The pulled directory is `kokoro-runtime-short-device-frontend-first`.
`scripts/import-kokoro-device-boundary.py CAPTURE RUNTIME_MODELS NEW_FIXTURE`
validates its eight raw hashes, source/model pins, dimensions and evidence,
copies the device arrays exactly, and runs only the host ORT decoder and tail
to obtain references conditioned on those arrays. It never reruns the frontend
or harmonic source. Its `runtime.json` records device ORT1.28.2 and host reference
ORT1.31.1 separately, with `source_crosscheck: device-boundary-preserved`.
The assembler accepts that explicit provenance only after matching the original
device manifest and all eight raw files; the old host source crosscheck remains.

`kokoro-runtime-device-short` and `kokoro-runtime-device-short-ncnn` are ready
under the private work directory. Both scalar CPU runs pass the unchanged gate
for **205942 values**, max error **4.93526459e-5**, relative RMS
**4.17532224e-7**, zero mismatches; all **1739 shapes** match. CPU outputs before
and after annotation are byte-identical, SHA256
`d9f8abf51d708632352846e3798417c14dcf095ea34b1f2396a362ec53b61082`.
Logs are `runtime-device-short-import.log` and
`runtime-device-short-ncnn-prepare.log`. These are CPU decoder/tail references
on the tablet boundary, without an unsplit original-ONNX waveform identity claim.

The same fixture then passed both tablet stages:

| Stage / log | Output and accuracy | Measured stage time | Peak / swap |
| --- | --- | ---: | --- |
| GPU core, `runtime-device-short-ncnn-device.log` | 205942 values; max error 1.02519989e-4, relative RMS 9.15445936e-7; zero mismatches | 38.844565 s extraction/transfers | 318.7MiB / zero |
| CPU tail, `runtime-device-short-ncnn-waveform-device.log` | 46800 samples; max error 8.36180584e-6, SNR97.957328dB; zero mismatches | 0.040910 s | 2.4MiB / zero |

Both services stayed active and no new GPU fault was recorded. The actual
tablet frontend/source output reaches the GPU and then the waveform tail with
the existing accuracy gates. Host CPU reference generation and ncnn shape
capture were interleaved between device stages; these stage timings therefore
do not establish single-executable synthesis latency or combined memory use.

## Integrated runtime and metadata shapes

`tests/kokoro_shapes.{h,cpp}` implements `radish::infer_kokoro_shapes(param,T)`
without weights, tensor execution or ncnn dependencies. It seeds the five
inputs from T and applies the audited operators' padding, stride, dilation,
reshape, slice, concat and broadcast rules with checked integer arithmetic.
Unsupported or ambiguous parameters, malformed graphs and overflow fail.
Existing `-23330` hints are replaced, retaining every other parameter token,
layer, blob and weight order, including the 58 style projection annotations.
The caller separately verifies graph and weight hashes. The interface accepts
T=1..4096; that bound does not imply measured model accuracy at every length.

Independent comparisons against actual CPU captures match **all 1739 blobs**
of the original 1368-layer layout at both T78 and T213, and **all 1307 blobs**
of the fused 1098-layer InstanceNorm graph at both lengths. The rank-three
normalization reshapes are included. The isolated fused T78 CPU run also passes
the original full-output gate: max error 4.50611115e-5, relative RMS
4.16033117e-7, zero mismatches. Shape formulas come from operation metadata,
not interpolation between captured lengths. The old host shape-capture
assembler remains useful as an independent check; it is no longer part of
integrated synthesis. Upstream `ModelWriter::shape_inference()` would execute
an Extractor for each blob and is not used here.

The callable interfaces are `radish_frontend::generate_frontend()` and
`export_boundary()` in `tests/kokoro_frontend.{h,cpp}`, and
`radish_waveform::load_tail()` / `synthesize()` in
`tests/kokoro_waveform.{h,cpp}`. They share the validated implementations with
the standalone commands. The frontend returns owned vectors and releases ORT
values, sessions and the runtime before GPU allocation. The tail retains the
pinned inverse basis, window and double-precision arithmetic.

`tests/ncnn/kokoro_runtime.cpp` joins these modules with the existing ncnn
helpers. It generates one boundary, rebuilds all logical shape hints for the
actual T, loads the selected graph, checks its 1307 InstanceNorm or 1067 Snake
loaded hints, and runs FP32
core inference. It destroys the Net, GPU allocators and device before the CPU
tail. Thus a new invocation with a different T gets new hints and pipelines;
there is no cross-input resident Net cache. `--repeat N` reuses one boundary
and loaded Net for N extractions, then applies the tail once to the last result.
It measures loaded-core reuse, not N complete requests or changing shapes.
`--cpu-packed` selects a separate CPU invocation; a numerical comparison must
still establish identical boundaries or use the preserved-boundary references.

The output is an atomically published new directory containing `boundary/`
(eight raw files and `frontend.json`), `core.f32`, `waveform.f32`, and
`runtime.json`. `synthesis_seconds` includes frontend creation/destruction,
shape propagation, core loading/all extractions/destruction and one tail,
excluding output publication. Load, extraction, scope and tail times are also
recorded separately. Generating an output alone does not establish accuracy.

### Artifact pins and device wrapper

`scripts/build-runtime.sh` builds the standalone frontend and the
`build-ncnn-simpleomp/radish-kokoro-runtime` target using the pinned ORT headers.
The device wrapper verifies these immutable artifacts before staging:

| Artifact | SHA256 |
| --- | --- |
| Pure frontend, 111933749 bytes | `56ba27008352a2191b631ebbf269bf4506d690a1b83d8ff8ff5524b11291ddd3` |
| Harmonic source, 13810 bytes | `4bdcb9f2f54d6efbfdcefce384300352933cc71fa562930f23eaeb3443528fd2` |
| Fused T213 parameter template | `6499e4996cea79d28e9c72caab1e2f0ca7d9fab4420a6f198c7e197fb06d347f` |
| Opt-in Snake T213 parameter template | `e01c56a4f23d6d3c8f92dbecb0c3fc37f6f74e9f305f58b82d5196278a450d38` |
| Core weights, 213042160 bytes | `0f77cf77ff79a7724662f2e79a4d50284eb498abb2e519469a188e33f6c051ae` |
| Portable tail, 1936 bytes | `e04b5c7b07c51b98b8883ba4feb753dbcb5bad9260fc93d009aa7fd577e80749` |
| Private Vulkan loader, 703696 bytes | `75699a38b1f26c1e8bfa2062e6593177bb07d4e3e346486d117fab8a0de0104a` |

`scripts/device-runtime.sh` also records the current executable, driver, ICD,
input and environment hashes in private evidence. It stages the pinned loader
as `libvulkan.so.1` beside the private driver. The deliberately compact ICD
causes ncnn SIMPLEVK to load that Vulkan loader, which then reads
`VK_DRIVER_FILES`; its library search path does not depend on a system loader.
The installed ORT library remains read-only. Existing device core weights are
hash-checked and reused, or uploaded once to a new shared hash-named asset.
Immutable frontend/source graphs may also be reused through no-overwrite
hardlinks from a prior private runtime directory after matching their exact
hashes. The wrapper records upload/hardlink provenance and rechecks both remote
graph hashes before and after execution.

Each run has exclusive device and host output directories. The shared lock
honors `RADISH_DEVICE_LOCK`. The wrapper requires 832MiB available memory and
enough disk for payload, bounded outputs and reserve (at least 256MiB), then
uses MemoryMax640M, SwapMax128M and RuntimeMax300s. It preserves both services,
checks new GPU faults, and records service, memory and disk evidence. Defaults
are four ncnn/ORT threads, `RADISH_NCNN_TILED=1`, batch4, shared-groups8,
shared32/mixed-storage0, pipeline-cache1, trace/profile1. These values and all
overrides are recorded. No production service files are replaced.

### Reproduction and same-boundary validation

From the repository root, with the pinned private inputs, exported graphs,
fused graph/weights and tail prepared under WORK:

```bash
export RADISH_WORK=/run/media/jethac/Shared/radish
PY="$PWD/streams/tts/downloads/kokoro-venv/bin/python"
bash radish/scripts/build-runtime.sh
bash radish/scripts/device-runtime.sh runtime-short review-short --vulkan
bash radish/scripts/device-runtime.sh runtime-full review-full --vulkan
```

Labels must be new; prior outputs are never overwritten. `--repeat 2` requests
two loaded-core extractions within the same process, and `--cpu-packed` is an
alternative backend. The default maximum decoder T is 512 (`--max-frames`),
independent of the derived core frame count `120*T+1`.
The InstanceNorm graph remains the default. Explicit `--snake` selects the
separately pinned 906-layer candidate and adds `-snake` to the capture directory
name. Its shared fixture validator freezes the host CPU validation marker,
reference/output hashes and graph assets, then rechecks them after acquiring
the device lock and after execution. This host gate does not replace validation
of each actual tablet output.

After a device run finishes, import its preserved boundary and validate the
complete outputs. For the short command above:

```bash
CAPTURE="$RADISH_WORK/kokoro-runtime-short-single-vulkan-review-short"
REFERENCE="$RADISH_WORK/kokoro-runtime-single-review-short-reference"
"$PY" radish/scripts/import-kokoro-device-boundary.py \
  "$CAPTURE/boundary" "$RADISH_WORK/kokoro-runtime-models" "$REFERENCE"
"$PY" radish/scripts/validate-kokoro-runtime.py \
  "$CAPTURE" "$REFERENCE" "$RADISH_WORK/runtime-single-review-short-validation.json"
```

The wrapper supplies importer-compatible `boundary/device-capture.json` plus
`device-runtime.json`. The importer copies all eight boundary arrays exactly
and generates only decoder/tail references; it never recomputes frontend/source.
Validation checks the frozen output and evidence hashes, reference provenance,
all eight raw-file identities, exact output lengths and unchanged core/waveform
gates. This work happens after device synthesis and is excluded from its time.
It compares the original decoder/tail on the tablet boundary, without claiming
identity to a separate unsplit original-model execution.

### Complete-run evidence

`runtime-integrated-host-short-validation.json` records a full host T78 pass:
205942 core values, max error **3.81469727e-5**, relative RMS **4.69385446e-7**;
46800 waveform samples, max error **1.07027590e-5**, SNR **102.704341dB**;
zero mismatches. All eight frontend files match the existing host reference
inputs exactly. This is host integration correctness, not tablet performance.

The complete tablet runs are `kokoro-runtime-short-single-vulkan-first` and
`kokoro-runtime-full-single-vulkan-first`, with logs
`runtime-single-{short,full}-first.log` and reports
`runtime-single-{short,full}-first-validation.json`. ORT1.28.2 generates each
boundary on the tablet; host ORT1.31.1 supplies references afterward. All eight
raw arrays are byte-identical between each device capture and its reference
inputs. Both runs use four ncnn and four ORT threads.

| Stage | Vulkan T78 | Vulkan T213 |
| --- | ---: | ---: |
| Frontend/source, including ORT cleanup | 8.364384 s | 6.130650 s |
| Metadata propagation, 1307 blobs | 0.038182 s | 0.031212 s |
| Core cold load | 9.727086 s | 9.669170 s |
| Core extraction, including transfers | 25.190135 s | 67.889310 s |
| Entire core scope, including load/destruction | 35.187278 s | 77.893781 s |
| Waveform tail | 0.038283 s | 0.103361 s |
| Synthesis total | **43.652206 s** | **84.223461 s** |
| Through output publication | 43.656357 s | 84.231595 s |
| Service wall time | 43.711 s | 84.317 s |
| Peak memory / swap | 343.6MiB / zero | 457.2MiB / zero |

Core load/extraction are components of core scope and must not be added to it
again. Both services remain active and no new GPU fault is recorded for either
run. Full-output accuracy is:

| Output | T78 | T213 |
| --- | --- | --- |
| Core values | 205942 | 562342 |
| Core max error / relative RMS | 1.05142593e-4 / 9.27539488e-7 | 1.02996826e-4 / 7.81135939e-7 |
| Waveform samples | 46800 | 127800 |
| Waveform max error / SNR | 1.47223473e-5 / 96.917732dB | 9.19587910e-6 / 98.355163dB |
| Core / waveform mismatches | zero / zero | zero / zero |

There is no host calibration or reference generation between inference stages.

The equivalent full T213 `--cpu-packed --threads 6` run uses four ORT threads
and the same fused graph in
`kokoro-runtime-full-single-cpu-packed-cpu-first`. Its log is
`runtime-single-full-cpu-first.log`; strict validation is recorded in
`runtime-single-full-cpu-first-validation.json`. The independent
`runtime-single-full-cpu-gpu-boundary-identity.json` records identical SHA256s
for all eight frontend input/output files across the CPU and Vulkan runs.

| CPU-packed T213 stage | Time |
| --- | ---: |
| Frontend/source, including ORT cleanup | 5.736043 s |
| Metadata propagation | 0.031603 s |
| Core cold load | 1.144992 s |
| Core extraction | 32.626909 s |
| Entire core scope, including load/destruction | 33.855854 s |
| Waveform tail | 0.102624 s |
| Synthesis total | **39.789900 s** |
| Through publication / service wall time | 39.807908 s / 40.049 s |

CPU peak memory is **461.2MiB**, with zero swap, both services active and no new
GPU fault. All **562342 core values** pass, max error **3.62396240e-4**, relative
RMS **2.51363812e-6**; all **127800 waveform samples** pass, max error
**5.05000353e-5**, SNR **88.225028dB**, with zero mismatches in either output.
These are separate first-process runs with verified identical boundaries, not a
sustained or resident benchmark. The Vulkan path is functionally complete for
these inputs but has not beaten the six-thread CPU baseline.

## Output reshape optimization

The accepted revised0019 adds `RADISH_NCNN_RESHAPE_ALIAS=1` to the private
runtime wrapper defaults. It aliases only compatible FP32 pack4 rank3(h=1)
outputs back to rank2, retaining original input copies and all scalar fallbacks.
The fixed full core passes bit-identically in67.138187/66.919494 s first/warm;
49 copy dispatches disappear without adding transfer clones. The earlier
bidirectional alias is rejected for its109.107981 s warm regression and remains
reproducible in commit `bf4d98f`.

`runtime-single-short-to2d-first.log` validates this control through fresh T78
synthesis. Frontend4.168688 s, metadata0.032164 s, core load9.847798 s, core
extraction25.451257 s, synthesis39.846221 s, publication39.850121 s, service
wall41.872 s; peak330.8 MiB and zero swap. Both services stay active with no
new faults. Matching validation JSON passes all205942 core values and46800 audio
samples, both byte-identical to the earlier T78 run; waveform SNR96.917732 dB.
This does not isolate a synthesis speedup from aliasing: frontend startup varies
and this first core extraction is slightly slower than the earlier25.190135 s.
The full T213 startup table above predates the output-alias optimization.

## Remaining integration work

- Retain full-output checks across additional supported inputs and lengths.
  Complete T78/T213 success, T bounds and metadata formulas alone do not
  establish arbitrary-input accuracy.
- Measure loaded-core repeats, cold/warm behavior and sustained throughput.
  Resident frontend/core reuse across requests and changing T is not implemented;
  a future cache must keep dimensions, hints and compiled pipelines consistent.
- Add and measure cancellation and cleanup behavior before application wiring.
  Current bounded one-shot processes do not establish production cancellation
  or memory use with simultaneously resident frontend and core sessions.
- Integrate the existing token/style/duration interfaces into the live application
  only after these private-runtime checks. Models, weights and generated captures
  remain outside the public repository.

Keep device locking, bounded sessions, both live services and fault inspection
requirements from [GOAL.md](GOAL.md). This evidence does not complete M5 or M6.

### Sequential tiled submission checkpoint 0022

At checkpoint 0022, the private runtime selected `RADISH_TILED_BATCH_CHUNKS=4` for the pinned
FP32 graph. The driver still defaults 1 and accepts this option only for the
measured local128 tiled resource class. `RADISH_NCNN_TILED32` remains0.
All wrappers validate 1/2/4, and the executable and frozen device manifest record
the new control. Rebuild the executable before using the updated wrapper.

Fresh T78 validation passes all 205942 core values and 46800 waveform samples:
core 20.403266 s, total synthesis 34.863887 s, service 38.856 s,341.8 MiB/zero swap.
Core and waveform are byte-identical to the earlier T78 run; SNR 96.917732 dB.
Evidence: `runtime-single-short-tiled-submit-0022-validation.json`. The fixed
T213 core is 56.013717 s warm, but full-length fresh InstanceNorm-only synthesis
has not been rerun with 0022. Keep the older full-length startup result separate
from the later Snake runtime using this driver checkpoint.

### Opt-in Snake graph

The model and integrated runtime now register the project-local
`radish_ncnn::SnakeContext` before loading graph parameters. The context outlives
its Net/layers and is destroyed before the device; it compiles explicit-length
GLSL once when a supported GPU Snake layer is instantiated. Original graphs
instantiate no Snake shader. CPU execution uses the original five
BinaryOp/UnaryOp layers. No weights or tensor execution are added to metadata
propagation; the exact 906-layer/1067-blob inventory has a checked rank2 Snake
rule requiring both original per-channel coefficient shapes.

The strict rewrite retains all 96 original coefficient tensors, weighted-row
order and bytes, and the 48 final output identities. `snake-host-full-cpu-first.log`
passes all 562342 values at unchanged gates, max error 6.65187836e-5; its
2.504136 s is host CPU evidence. Focused tablet CPU pack1/pack4 and GPU probes
also pass with retained inputs and bit identity to the original five operations.
Actual shader compilation passes after correcting the push struct spelling to
`parameter`: local32, 8 full/1 half registers, no shared/private memory. The
live actual SPIR-V gate checks NoContraction, and the outer final-NIR `fmad`
preserves intermediate rounding because it is explicitly unfused. Full audit
scope and original failed-compile evidence are retained in [NOTES.md](NOTES.md).

`scripts/fuse-kokoro-snake.py SOURCE NEW_DIRECTORY` creates only a **provisional**
graph and `snake-provisional.json`; it executes no tensors and is insufficient
for `--snake`. The private fixture must first pass a Snake-enabled host
`radish-model --cpu-packed --threads 6 FIXTURE` against the pinned fixed
`reference.f32`. Preserve `snake-cpu-validation.log` and the runner's complete
`ncnn-cpu-packed-output.f32`. A completed `snake-validation.json` must record
format `radish-kokoro-snake-validation-v1`, status `cpu-reference-passed`, the
parameter/weight/reference hashes and CPU backend, 562342 outputs, zero
mismatches, 0.002 absolute/relative gates, runner hash, log hash and output hash.
This checkpoint's host runner is `build-ncnn-host/radish-model`, SHA256
`f4b06e2edfe3740d27fc78fa63d6b982e78b1dd03f99ca4d1f070c69b6b9fe48`.

`scripts/validate-kokoro-snake-fixture.py FIXTURE` is read-only: it requires that
proof marker, verifies all eleven evidence/input/graph files and independently
recompares every CPU output. Neither script creates CPU proof automatically;
do not copy a success marker onto an unvalidated candidate. The existing
validated private fixture is `WORK/kokoro-core-shapes-snake`. Device executable,
probe, graph and ICD pins for the measured runs are preserved separately in
`WORK/snake-binary-checkpoint.json`; model weights and proof outputs remain
private. Each fresh runtime still requires its own same-boundary validation.

The fixed T213 GPU core now passes first/warm extractions in
**51.936026/51.188057 s**, with 9.365 s cold load, 416.8 MiB peak and zero swap,
no faults and both services active. All 562342 outputs pass and are byte-identical
to accepted 0022, so its existing full-waveform evidence applies to these exact
core bytes without claiming a new tail run. Warm latency is 8.615% below
56.013717 s, with one warm observation for each graph. Evidence:
`core-snake-first-repeat2.log`. This is a saved-boundary core comparison;
the complete warm trace maps 240 original Snake commands to 48, reducing their
wall time from 6.654548391 to 2.459730388 s and submits from 11328 to 3648.
All 1750 retained command signatures match, with no new helpers or transfers.
These command timers include host work, submission/waits and GPU execution.
The map is `warm-snake-result-mrqc_tau/comparison-0022-snake.json`.

Fresh T78 and T213 `--snake` synthesis both pass independent validation in
`runtime-single-{short,full}-snake-first-validation.json`, using the captures
`kokoro-runtime-{short,full}-single-vulkan-snake-first-snake`. Both use four
ncnn/ORT threads and have no new faults, with both services active. The wrapper
successfully reuses immutable frontend/source graphs through verified hardlinks.

| Snake runtime stage | T78 | T213 |
| --- | ---: | ---: |
| Frontend/source, including cleanup | 4.424653 s | 5.772051 s |
| Metadata propagation, 1067 blobs | 0.026029 s | 0.026095 s |
| Core cold load | 9.398850 s | 9.314241 s |
| Core extraction, including transfers | 19.507355 s | 52.428375 s |
| Entire core scope | 29.189242 s | 62.193123 s |
| Waveform tail | 0.037999 s | 0.103515 s |
| Synthesis total | **33.764051 s** | **68.158274 s** |
| Through publication / service wall | 33.767987 / 33.852 s | 68.178252 / 68.279 s |
| Peak memory / swap | 383.3 MiB / zero | 445.9 MiB / **22.9 MiB** |

Load/extraction are included in core scope. The full runtime's swap use is a
distinct condition from the zero-swap fixed-core repeat and T78 runtime.

All **205942 core values** and **46800 waveform samples** pass unchanged gates
against original decoder/tail references on the exact tablet boundary; all
eight boundary files are identical to the reference inputs. Core max error is
1.05142593e-4, relative RMS 9.27539488e-7; waveform max error 1.47223473e-5,
SNR 96.917732 dB, zero mismatches. Core/waveform hashes are respectively
`7b91c412aa771e91c1976233fddde9d138610b8f56dd8157829110f0adba670c` and
`1baae4fc1a45bb276bff219d3b9b9c7af8352e5d889798482e6fcfd4114c7353`,
matching the earlier GPU T78 results exactly. No host work occurs between
inference phases; reference creation/validation happens afterward.

T213 likewise passes all **562342 core values** and **127800 waveform samples**:
core max error 1.02996826e-4/relative RMS 7.81135939e-7, waveform max error
9.19587910e-6/SNR 98.355163 dB, zero mismatches. Core/waveform SHA256s are
`7a5dbb9410a91e30bdfb9db12426b653d21b09786310c8c0cc623618efa174bb` and
`4d2ff1622bb83ad0ff02a603c6f2974fbd19685a099e45b5f0cd13a2928b7e8d`, identical
to the earlier full GPU results. The separate
`runtime-single-full-snake-cpu-gpu-boundary-identity.json` verifies all eight
boundary files match both prior CPU and GPU captures. The earlier six-thread
CPU synthesis remains faster at 39.789900 s; it is a historical same-input
baseline, not a fresh Snake CPU run or a sustained comparison.

Snake stays an explicit `--snake` opt-in and InstanceNorm remains the default
for this checkpoint. Resident cross-request reuse, cancellation and sustained
performance remain open.

### Sequential batch eight: checkpoint 0023

The runtime now selects `RADISH_TILED_BATCH_CHUNKS=8` by default, while Snake
still requires `--snake`. The driver adds eight to the existing guarded tiled
batch choices; its default remains one. Each hardware dispatch still executes
one workgroup with the same waits and cache operations. Batch sixteen is not
accepted by this checkpoint.

The fixed Snake core passes first/warm extractions in **49.266772/49.429406 s**,
with 419 MiB peak memory, zero swap and identical output bytes. Fresh full-length
synthesis then passes in **63.528396 s**, including 5.705182 s frontend/source,
0.026177 s metadata, 9.426758 s core load, 47.854719 s extraction and 0.103455 s
tail. Core scope is 57.630407 s; through publication is 63.535909 s and service
wall is 63.611 s. Peak memory is **458 MiB with zero swap**, no new GPU faults,
and both appliance services remain active.

All 562342 core values and 127800 audio samples pass the unchanged gates, with
98.355163 dB waveform SNR. Core/waveform bytes match the earlier full GPU output
exactly. All eight captured boundary files also match the prior CPU and batch-four
GPU runs. Evidence is `runtime-single-full-snake-batch8-0023-validation.json`,
`runtime-single-full-batch8-cpu-gpu-boundary-identity.json` and the capture
`kokoro-runtime-full-single-vulkan-snake-batch8-0023-retry-snake` under WORK.

The first attempt stopped at disk preflight before inference. Hash-verified
deduplication of identical private model graphs preserved all paths and contents,
recovered 218664 KiB, and allowed the retry with the same disk reserve. Its record
is `device-runtime-immutable-graph-dedup-0023.log`.

The warm core comparison isolates unchanged command work with 3231 fewer
submissions and a 3.4357% wall-time reduction. The separate 68.158274 to
63.528396 s fresh-runtime comparison has only one observation per setting and
different swap use; do not attribute its entire improvement to batching.
Resident requests and sustained behavior remain unmeasured.
