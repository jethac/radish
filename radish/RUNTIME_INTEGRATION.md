# Kokoro runtime integration audit

## 2026-11-02: first Japanese runtime request passes full validation

The intended Japanese greeting now runs through the tablet frontend/source, all 589 Vulkan core layers, and the CPU waveform tail. The voice blend is 0.7 `jf_alpha` / 0.3 `af_heart`; the actual boundary produces T=168 and 100,800 samples (4.2 seconds at 24 kHz). All 443,542 core values and all waveform samples pass the unchanged full comparisons. Core maximum absolute error is 8.774e-5; waveform SNR is 96.41 dB against the required 50 dB.

This first request takes 47.314285 seconds synthesis: 8.038820 seconds frontend scope, 11.831491 seconds core loading, 26.928692 seconds first extraction, and 0.082031 seconds tail. Peak memory is 413.7 MiB with zero swap. The retained profiles contain 56,204 chunks and 8,627 submissions, including 2,604 packed-math submissions; no new fault occurs and both services remain active.

References use the original decoder and tail on all eight exact tablet boundary files. This does not independently recompute the frontend/source or compare against an unsplit original-model waveform. Resident synthesis and an exact matching four-thread ncnn CPU baseline are separate pending scopes; unrelated English timings are not a baseline for this utterance.

The private runtime uses the qualified shifted ncnn archive, an explicitly verified loader, and the reviewed workload-scoped robust=false device. Application defaults and driver robustness support remain unchanged. [Exact inputs, source pairing, complete numerical results and independent reviews](results/japanese-first-runtime-0045.json) and [reproducible runtime/wrapper patch](patches/0045-runtime-japanese-explicit-device.patch) preserve this qualification. The patch requires the paired [ncnn campaign branch](https://github.com/jethac/ncnn/tree/radish/0045-packed-a-shift) and matching runtime rebuild; it does not deploy or replace the live services.

2026-11-02: one private tablet executable runs the CPU frontend/source,
metadata propagation, FP32 Vulkan neural core and CPU waveform tail from token
IDs/style/speed. The new opt-in `--norm-affine-snake-deconv2d` graph fuses 42
normalization/AffineSnake chains and executes all 589 core layers on Vulkan.

Packed-A0042 is an additional default-off weight-layout option. The same-binary
fixed warm core falls 35.805708→34.303633 s; actual short/short/full/full synthesis is
**28.952951 /17.297296 /46.216283 /41.225818s**. All four original same-boundary
core/waveform references pass and every byte matches 0035. Resident hits improve
but cold/shape misses regress because core loading increases. The mixed sequence
saves only 0.394555 s summed synthesis; its larger whole-session change is not an
isolated GPU gain. Peak is 512.8 MiB plus 74.3 MiB swap; no new fault, services active.
CPU resident short/full 15.411271/36.802922 s remains faster. The earlier0035 baseline
and all prior results remain documented below. See [GPU_PACKED_A.md](GPU_PACKED_A.md)
for the tradeoff; sustained behavior, cancellation/restart on this graph, live
application integration and the GPU-efficiency goal remain open.

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
helpers. Its one-shot mode generates one boundary, rebuilds all logical shape
hints for the actual T, loads the selected graph, checks its 1307 InstanceNorm or 1067 Snake
loaded hints, and runs FP32
core inference. It destroys the Net, GPU allocators and device before the CPU
tail. Thus a new invocation with a different T gets new hints and pipelines;
one-shot mode retains no cross-input Net cache. The bounded request-loop mode
described below now retains one core across complete requests. `--repeat N`
reuses one boundary and loaded Net for N extractions, then applies the tail once
to the last result.
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
- Repeat CPU/GPU resident measurements under matched session and thermal
  conditions, and measure sustained throughput. The initial same-graph CPU
  baseline passes and remains faster. Frontend/source are regenerated per request;
  no concurrent frontend/GPU execution is implemented.
- Extend tablet cancellation coverage and run a new request after cancellation
  before application wiring. Cancellation after an active core now passes;
  prompt interruption during extraction and other phase/recovery cases remain open.
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

### Planar input and padding: checkpoints 0024–0026

Select `RADISH_NCNN_PLANAR=1` with the Snake graph, ordered ncnn patches
0024/0026 and cumulative Mesa patch 0025. The new path replaces padding with
an initialized channel-major workspace and uses contiguous vector loads in the
53 tiled convolutions. Patch 0026 changes only the padding workgroup from
8×4 to 8×8; math and storage dimensions are unchanged. PLANAR defaults to zero.

The fixed core passes first/warm extraction at 48.370236/46.932926 s, with
418.4 MiB and zero swap. The preceding 8×4 candidate ran at 48.863090 s warm;
8×8 removes 1848 padding submissions and saves 0.839163454 s in those commands.
Other retained commands vary by 0.794779154 s, so the whole 1.930164 s reduction
cannot be attributed entirely to padding. Output bytes are unchanged.

Fresh T78 synthesis passes in 31.890577631 s, including 18.148241024 s core
extraction, at 329.1 MiB and zero swap. Fresh T213 passes in 63.320729646 s,
including 47.887014795 s extraction, at 429.7 MiB and zero swap. Every core and
waveform value passes the original exact-boundary ORT comparison; waveform SNR
is 96.917732 dB and 98.355163 dB respectively. Both generated core/waveform files
are byte-identical to the preceding GPU outputs. The eight T213 boundary files
also match the CPU baseline. Both services remain active and no new GPU faults
occur. The full startup result is essentially unchanged from checkpoint 0023.

Evidence under WORK: `core-snake-planar-pad64-0026-repeat2.log`,
`warm-planar-pad64-result-0026-ooc8sgac/REPORT.md`,
`runtime-single-{short,full}-planar-pad64-0026-validation.json`,
`runtime-full-planar-pad64-0026-boundary-identity.json` and
`kokoro-runtime-{short,full}-single-vulkan-planar-pad64-0026-first-snake-planar/`.
The exact binaries are frozen in `planar-pad64-0026/bin/`, with hashes in
`planar-pad64-0026/compiled-checkpoint.json`.

### Two X workgroups: checkpoint 0027

Separate cumulative Mesa patch 0027 permits `RADISH_TILED_GROUPS_X=2` only
for the inspected PLANAR shader resource class. It pairs X workgroups and caps
sequential chunks at four, retaining at most eight workgroups per submission.
Default one-group execution is unchanged. The wrappers record this option;
the runtime manifest helper has been rebuilt, but fresh synthesis with X2 has
not been run.

Focused dilation 1/3/5 and full fixed-core comparisons pass with identical
output bytes. First/warm core time is 45.621586/47.975705 s, at 417.4 MiB plus
25.3 MiB swap. The 53 changed math commands remain near 24.1 s: warm time
24.073201017→24.095025469 s, despite 25,944→13,024 hardware launches. Unchanged
work explains most of the first/warm latency variation. No performance gain is
established; retain the 0026/groups-one recommendation. No new GPU fault occurs.
Evidence: `core-snake-planar-x2-0027-repeat2.log` and
`warm-planar-x2-result-0027-nf2vcmi7/REPORT.md` under WORK.

### Fused normalization variance reduction: checkpoint 0028

Independent ncnn patch `0028-ncnn-instancenorm-sum4.patch` is disabled by
default. Set `RADISH_NCNN_NORM_SUM4=1` alongside `RADISH_NCNN_PLANAR=1` and
`--snake` to select its guarded FP32 pack4 path. It combines the full-width
variance square with the first sum4 reduction; mean, remaining reductions,
epsilon and final normalization retain their previous arithmetic.

The fixed full core passes all 562,342 values with identical output bytes.
First/warm extraction takes 47.302535/46.983200 s at 418.5 MiB and zero swap.
Removing 49 square passes saves 1.632487385 s in the affected commands, while
other commands vary by +1.547037747 s. Whole warm time remains effectively
flat against 46.932926 s; this is no established overall speedup.

Fresh synthesis also passes the unchanged full core and waveform gates:

| Input | Core extraction | Complete synthesis | Peak memory | Swap | Waveform SNR |
| --- | ---: | ---: | ---: | ---: | ---: |
| Full, T=213 | 46.628144 s | 65.988211 s | 436.9 MiB | 46.9 MiB | 98.355 dB |
| Short, T=78 | 17.777928 s | 31.783290 s | 328.6 MiB | 0 | 96.918 dB |

Both runs keep the appliance services active and report no new GPU faults.
All eight boundary files and complete core/waveform bytes match checkpoint
0026 at the same input length. The full run's frontend time and swap differ;
its slower total cannot be attributed solely to the normalization kernel.
Keep checkpoint 0026 as the performance recommendation pending paired evidence.

Evidence under WORK: `runtime-single-{full,short}-norm-sum4-0028-first.log`,
`runtime-single-{full,short}-norm-sum4-0028-validation.json`, and
`runtime-{full,short}-norm-sum4-0028-boundary-identity.json`.
Captures are `kokoro-runtime-{full,short}-single-vulkan-norm-sum4-0028-first-snake-planar-norm-sum4/`.
`norm0028-result-audit-zm_1noi9/REPORT.md` independently verifies fixed-core
command attribution and corrects the initial compiler-length accounting:
95 encoded instructions give `instrlen=6`; 198 allocated dwords include
trailing padding. Both focused execution and full-model profiles report 6.

### Short-shader batching: checkpoint 0029

Cumulative Mesa patch `0029-radish-short-batch8-cumulative.patch` adds optional
`RADISH_BATCH_CHUNKS=8` for the existing loop-free, barrier-free shader class
with instruction length at most 32. Defaults and dispatch group limits stay
unchanged. Every chunk retains its state, waits and cache synchronization;
the larger batch groups sequential chunks into fewer DRM submissions.

The measured path uses Snake, planar input, reshape aliases, generic batch 8,
tiled batch 8, shared groups 8, X groups 1 and norm fusion 0. The paired fixed
model uses the same frozen 0026 model helper and 0029 driver for both settings.
First/warm times are 47.715981/47.413913 s at batch 4 and
45.766135/45.339510 s at batch 8. All 562,342 output values remain byte-identical.
Peak memory is 419.7/417.5 MiB, with 7.7/0 MiB swap respectively.

Every one of the 1,798 command signatures, 114,393 chunks and 901 layer paths
matches. Submissions fall from 26,292 to 15,398; the affected shader class saves
1.992217851 s of the 2.074403 s warm improvement. This is a single paired
observation with different swap usage. New `submit_ns` and `submit_max_ns`
profile fields measure CPU wall time around submission, fence wait and fault
query. They include host scheduling and are neither GPU timestamps nor a
future bound on job duration. The largest eligible observed intervals are
34.063/33.911 ms in first runs and 5.011/6.194 ms in warm runs.

Fresh synthesis passes every unchanged core and waveform comparison:

| Input | Core extraction | Complete synthesis | Peak memory | Swap | Waveform SNR |
| --- | ---: | ---: | ---: | ---: | ---: |
| Full, T=213 | 45.667104 s | 61.429138 s | 431.3 MiB | 16.7 MiB | 98.355 dB |
| Short, T=78 | 17.289251 s | 32.622528 s | 330.7 MiB | 0 | 96.918 dB |

All eight boundary files and complete GPU core/waveform bytes match 0026.
Full synthesis improves from 63.320730 s. Short synthesis is slower than the
preceding 31.890578 s, despite its faster core, because startup varies:
core loading takes 10.769532 s instead of 9.191366 s. Both services remain
active and no GPU faults occur. CPU remains faster; resident synthesis and
sustained behavior remain unfinished. Select batch eight explicitly for this
measured workload; norm fusion and X pairing remain off.

Evidence under WORK: `core-snake-planar-short-batch{4,8}-0029-repeat2.log`,
`short-batch8-result-0029-p9a9QvGK/REPORT.md`,
`runtime-single-{full,short}-short-batch8-0029-first.log`,
`runtime-single-{full,short}-short-batch8-0029-validation.json`, and
`runtime-{full,short}-short-batch8-0029-boundary-identity.json`.
Build/source reconstruction and frozen driver are in
`short-batch8-0029-integration-bot0hl5l/`.

### Positive convolution indices and AffineSnake: checkpoints 0030/0031

Ordered ncnn patch 0030 replaces six signed index divisions with equivalent
operations on their proved nonnegative domains. The floating-point recurrence,
shared storage, tensor layout and barriers stay unchanged. All 20 compiled
specializations retain 16 full registers, zero half registers and no private
storage. Their native lengths are 27 or 28; cumulative Mesa patch 0031 admits
those lengths to the existing bounded tiled batching class. X pairing still
requires length 31 and remains disabled in these measurements.

With the original Snake graph, warm extraction moves from 45.339510 s to
44.956105 s. The 53 affected math commands save 0.373507849 s; every other
command signature, chunk count and submission count matches. Output bytes
remain identical. This single pair has differing swap usage, including
30.6 MiB in the index run; it establishes a modest observed improvement.

The separate `RadishAffineSnake` layer computes style scale, style bias and
Snake activation together, preserving the original eight FP32 operation
boundaries and the stored reciprocal. Its five inputs are normalized data,
gamma, beta, alpha and reciprocal. The exact graph has 714 layers/875 blobs,
with all weights retained. Focused CPU/GPU comparisons are bit-identical to
the original operators; full CPU and GPU reference gates also pass.

Prepare the fixture from the validated Snake fixture, actual original/affine
CPU validation directory and its runner:

```bash
python3 radish/scripts/prepare-kokoro-affine-snake-fixture.py \
  "$RADISH_WORK/kokoro-core-shapes-snake" CPU_VALIDATION_DIRECTORY \
  VALIDATED_CPU_RUNNER "$RADISH_WORK/kokoro-core-shapes-affine-snake"
```

The preparer imports and checks completed evidence; it does not fabricate a
pass marker or run inference. Its required files and commands are described in
`radish/NOTES.md`. The validator rechecks every fixed input, the graph/weights,
full CPU output and original/fused byte identity before allowing device use.
The model runner accepts `core-shapes-affine-snake`; the runtime runner selects
it only with `--affine-snake`, preserving the existing InstanceNorm/Snake paths.

With identical model helper, driver and controls, AffineSnake changes warm
extraction from 44.956105 s to 43.124072 s. Targeted work changes from 192 to
48 commands and saves 1.822263171 s; all 1,606 retained per-layer command
signatures match. Global traversal order changes because normalized input is
visited first, so this is not a claim of identical global scheduling.
Fixed output bytes remain identical. Peak memory is 416.0 MiB with 7.4 MiB
swap. First extraction takes 43.525012 s and cold load 8.880 s.

Fresh runs use batch 8, tiled batch 8, shared groups 8, planar input and reshape
aliases, with norm fusion 0 and X groups 1:

| Input | Core extraction | Complete synthesis | Peak memory | Swap | Waveform SNR |
| --- | ---: | ---: | ---: | ---: | ---: |
| Full, T=213 | 43.655035 s | 59.184846 s | 415.6 MiB | 74.2 MiB | 98.355 dB |
| Short, T=78 | 16.577948 s | 32.105121 s | 330.3 MiB | 0 | 96.918 dB |

Both complete core/audio comparisons pass unchanged gates; all eight boundary
files and GPU output bytes match checkpoint 0029. Both services remain active
and there are no new GPU faults. These are single startup runs with varying
memory pressure. CPU remains faster, and three core operations still fall back
to CPU. Resident synthesis and sustained behavior remain unfinished.

Evidence under WORK: `planar-index-result-0031-cp_rznjt/REPORT.md`,
`affine-snake-result-0031-nb319sa9/REPORT.md`,
`runtime-single-{full,short}-affine-index-0031-validation.json`,
`runtime-{full,short}-affine-index-0031-boundary-identity.json`, and
`affine-index-0031-checkpoint/binary-checkpoint.json`.
The captures are
`kokoro-runtime-{full,short}-single-vulkan-affine-index-0031-first-affine-snake-planar/`.

### Height-one deconvolution variants

`--snake-deconv2d` and `--affine-snake-deconv2d` select graphs with all three
former CPU deconvolutions expressed as existing Vulkan 2D operators at height
one. Each replacement adds an input/output reshape and preserves the original
weights, horizontal stride, cropping and output padding. Vertical parameters
are explicit, including zero bottom output padding. The depthwise C1090 path
uses real aligned reshapes; the two dense output reshapes can use the existing
validated alias path. No driver or ncnn math shader change is required.

The exact graphs are 912 layers/1073 blobs for Snake and 720/881 for AffineSnake.
Prepare either from its corresponding original fixture and completed full CPU
evidence:

```bash
python3 radish/scripts/prepare-kokoro-deconv2d-fixture.py \
  --variant affine-snake-deconv2d \
  "$RADISH_WORK/kokoro-core-shapes-affine-snake" CPU_VALIDATION_DIRECTORY \
  VALIDATED_CPU_RUNNER "$RADISH_WORK/kokoro-core-shapes-affine-snake-deconv2d"

RADISH_NCNN_PLANAR=1 RADISH_BATCH_CHUNKS=8 \
  bash radish/scripts/device-runtime.sh runtime-full NEW_LABEL \
  --affine-snake-deconv2d
```

For the Snake route, use variant `snake-deconv2d` and the original Snake fixture.
The importer independently checks both original and candidate CPU outputs
against the unchanged full reference. Output identity to the old graph is not
required: different deconvolution accumulation produces small numerical changes.
The selected graph, all inputs, weights and CPU evidence are rechecked before
and after device execution. Existing graph defaults remain unchanged.

The combined fixed core passes first/warm extractions in 43.051203/42.469886 s,
with 402.1 MiB peak and zero swap. All 715 non-Input layers execute through
Vulkan in both extractions. All 562,342 values pass; the output is byte-identical
to the independently validated Snake/deconvolution result, whose saved-output
host waveform passes all 127,800 samples at 98.081 dB SNR. Cold loading is
9.346 s. The preceding AffineSnake graph takes 43.124072 s warm; these are single
warm observations, and the CPU baseline remains faster at 32.079044 s.
Fresh runtime validation is recorded separately after each captured boundary.

Evidence: `core-affine-deconv2d-0031-repeat2.log`,
`affine-deconv2d-fixed-validation.json`, `deconv2d-first-host-waveform.log`, and
`deconv2d-integrated-0031-checkpoint/binaries.json` under WORK.

The integrated all-GPU core also passes fresh tablet synthesis at both lengths.
T213 takes 42.943278 s core / 63.130064 s total, with 459.3 MiB peak and 38.1 MiB
swap; T78 takes 15.873927 s core / 30.156883 s total, with 385.0 MiB peak and zero
swap. All 715 executed core layers use Vulkan in each run. Every core value and
waveform sample passes the unchanged reference gates, with waveform SNR
98.377722/97.073984 dB for full/short input. All eight frontend boundary files
match the preceding AffineSnake captures exactly; output values change slightly
because of GPU deconvolution accumulation. No faults occur and services stay
active. Full startup regresses from 59.184846 s: frontend time rises from
5.935016 to 10.326545 s despite a faster GPU core. Short synthesis improves from
32.105121 to 30.156883 s, including a lower 9.564430 s core load. These remain
single startup observations with varying memory pressure, not sustained results.

Evidence under WORK: `runtime-single-{full,short}-affine-deconv2d-0031-first.log`,
matching `runtime-single-{full,short}-affine-deconv2d-0031-validation.json`, and
`runtime-affine-deconv2d-0031-summary.json`. Captures are
`kokoro-runtime-{full,short}-single-vulkan-affine-deconv2d-0031-first-affine-snake-deconv2d-planar/`.


### Combined graph with norm sum4 enabled

With frozen driver 0031 and the same integrated binary, enabling
`RADISH_NCNN_NORM_SUM4=1` on `--affine-snake-deconv2d` passes fresh T213 core and
waveform checks: 57.293048 s total, 41.411426 s core, 9.424234 s core load,
5.889140 s frontend, 418.9 MiB peak / 12.6 MiB swap. All 715 neural-core layers
execute on GPU. The eight boundary files and complete outputs match the prior
norm-disabled full run exactly; waveform SNR remains 98.377722 dB. No fault
occurred and both services stayed active. The preceding full total was 63.130064 s
with a 10.326545 s frontend, so the total gain is not solely a GPU improvement.
CPU full synthesis remains 39.789900 s. These are single startup observations;
bounded resident results follow below; sustained performance remains pending.
The earlier short 30.156883 s result uses norm sum4 disabled. Exact logs and pins
are in NOTES.


### Bounded complete-request reuse

The runtime binary accepts an explicit request loop:

```sh
radish-kokoro-runtime --affine-snake --deconv2d --vulkan \
  --request-loop --next-input INPUT_SHORT --next-input INPUT_FULL \
  --threads 4 --ort-threads 4 --max-frames 512 --repeat 1 \
  --cancel-file /private/run/cancel \
  MODELS INPUT_SHORT PARAM WEIGHTS TAIL NEW_SESSION
```

The positional INPUT_DIR is request zero; one to three `--next-input` directories
append complete requests, for two to four total. Loop mode requires repeat 1.
Existing one-shot `--repeat N` still computes one boundary, N core extractions
and one tail. Each loop request regenerates frontend/source, infers actual-T
shapes, extracts a new result and generates its waveform. A matching inferred
graph reuses one Net; a shape change evicts it before loading the replacement.
There is no multi-model cache or concurrent frontend/GPU execution.

Completed requests atomically publish request-000/, request-001/, etc, each with
the existing boundary/, core.f32, waveform.f32 and runtime.json v1 layout. Extra
request metadata records index, core-cache hit and workspace trim time. A hit has
zero core load. Top-level session.json records status, counts, ordered requests,
phase memory samples and final teardown. Require terminal complete and exact
requested/completed counts before accepting a successful session. The optional
regular cancel marker suppresses uncommitted output, preserves completed requests
and exits2 after draining the active phase. It is not prompt mid-GPU cancellation;
host checks and the later tablet probe below demonstrate this boundary behavior.

The same binary passes five full tablet reference comparisons across T78/T213:
short cold/resident 29.764615/20.055962 s; full after shape reload/resident
57.301229/46.984077 s. All 715 core layers use GPU; complete boundaries and outputs
match prior validated captures. The shape session peaks at 517 MiB / 19.1 MiB
swap, with no faults and both services active. Request timing includes phase
memory sampling/checkpoint I/O and workspace trim, excludes capture publication
and final session teardown. Released allocators do not guarantee immediate
physical-page return. The initial resident CPU comparison follows below;
sustained behavior remains unmeasured.

Public guarded loop staging is implemented and validated with driver 0032. After
preparing the exact combined-graph fixture and frontend assets above, the measured
two-short command is:

```sh
RADISH_NCNN_PLANAR=1 RADISH_NCNN_NORM_SUM4=1 RADISH_BATCH_CHUNKS=8 \
  radish/scripts/device-runtime.sh runtime-short public-resident-0032-first \
  --affine-snake-deconv2d --vulkan --request-loop --next-input runtime-short \
  --threads 4 --ort-threads 4 --max-frames 512 --repeat 1
```

Choose a new run label for another execution. The positional `runtime-short` or
`runtime-full` selects request zero; one to three `--next-input` fixture selectors
append requests, in order, for two to four total. Public loop staging requires
`--affine-snake-deconv2d` and repeat 1. One-shot behavior/defaults are unchanged;
the wrapper does not expose the binary's cancellation marker option.

The wrapper preserves the shared lock, 640 MiB / 128 MiB swap / 300 s runtime,
15 s stop timeout and existing reserves. A failed or timed-out run drains only
its own unit while retaining the lock. `kokoro-runtime-capture.py` freezes actual
binary/driver/loader/graph/input hashes, records observed sources separately from
binary identity, verifies host and remote assets and installed ORT before/after,
and verifies the pulled output hashes. Publication requires a complete session,
exact request order/counts and expected same-T cache/reload decisions. Each
`request-NNN/` keeps v1 `device-runtime.json` and
`boundary/device-capture.json`; `device-session.json` records the complete ordered
session with accuracy pending. After guarded capture, use the unchanged importer
and validator separately for every request, with unique references/reports:

```sh
"$PY" radish/scripts/import-kokoro-device-boundary.py \
  "$CAPTURE/request-000/boundary" "$RADISH_WORK/kokoro-runtime-models" "$REFERENCE_000"
"$PY" radish/scripts/validate-kokoro-runtime.py \
  "$CAPTURE/request-000" "$REFERENCE_000" "$REPORT_000"
```

The public wrapper's driver 0032 T78/T78 run passes both full core/waveform references with
all eight boundary files and outputs byte-identical to earlier validated captures.
Fresh/resident synthesis is 29.521695246/20.089174476 s, core
15.717958607/15.750590681 s and load 9.475816580/0 s. Each request executes 715 GPU /
zero CPU layers; all 1430 ordered trace rows match the earlier private session.
Peak memory 432.1 MiB, zero swap, no faults, both services active. The session
manifest records 49.928546931 s including final cleanup 0.254041777 s; service wall
time 50.038 s. The 9.432521 s saving primarily avoids core loading; it is not a
compute-speedup or sustained-performance claim. The later CPU comparison follows
below; the later tablet cancellation probe also follows.

The canonical build matches the measured runtime SHA
`0541e68cbb9e2f04862ff6cf36c73a70036118ec2f6d415716bf658b0270bda9`.
The original five private-harness requests used driver 0031; the two public-wrapper
requests use driver 0032 SHA
`c8fee96723942fca768a9026987a5d609071d2c60079058064377635155cb44c`.
Both use FP32, AffineSnake/deconv, PLANAR1, NORM_SUM4=1, generic/tiled batch8 and X1.
New evidence under WORK: `resident-public-0032-validation-mcnh4qu7/{REPORT.md,summary.json}`,
`public-resident-0032-request-{000,001}-validation.json`, and capture
`kokoro-runtime-short-single-vulkan-public-resident-0032-first-affine-snake-deconv2d-planar-norm-sum4-loop/`.
See NOTES for complete pins, logs and limits.

### Resident packed CPU comparison

The same public wrapper and combined graph pass four CPU requests with six
inference threads and four ORT threads. The measured command was:

```sh
RADISH_NCNN_PLANAR=1 RADISH_NCNN_NORM_SUM4=1 RADISH_BATCH_CHUNKS=8 \
  radish/scripts/device-runtime.sh runtime-short resident-cpu-0032-first \
  --affine-snake-deconv2d --cpu-packed --request-loop \
  --next-input runtime-short --next-input runtime-full --next-input runtime-full \
  --threads 6 --ort-threads 4 --max-frames 512 --repeat 1
```

Use a new run label for reproduction. All four requests pass the unchanged
per-request importer/validator above. Each has 715 CPU / zero GPU core layers.
All eight input boundary files match the corresponding validated GPU captures;
same-T CPU core/waveform bytes match each other. Short/full core maximum errors
are 0.000358581543/0.000347137451; waveform SNR 92.417480/88.221183 dB with zero
mismatches across all 205942/562342 core values and 46800/127800 samples.

| Complete resident synthesis | Packed CPU, six threads | GPU |
| --- | ---: | ---: |
| T78 | 15.411270994 s | 20.089174476 s |
| T213 | 36.802922440 s | 46.984076563 s |

CPU core times across short/short/full/full are 11.325414/11.072058/30.849668/
30.703001 s; complete synthesis 16.998107/15.411271/38.496353/36.802922 s.
Cache pattern is miss/hit/miss/hit with zero load on hits. Session 108.114303525 s,
service 112.503 s, 404.8 MiB peak / zero swap, no faults and both services active.
CPU remains faster in these observations. Graph, binary, ORT4 and exact boundaries
match; CPU uses six inference threads and GPU four host threads. Short GPU uses current
driver 0032; full GPU is the earlier driver 0031 short/full/full session. There is
one resident sample per length/backend, with differing sequence and memory
pressure; sustained throughput remains open. The bounded tablet cancellation
result below does not establish prompt interruption or restart after cancellation.

Evidence under WORK: `resident-cpu-0032-validation-ch3gouod/{REPORT.md,summary.json}`,
`resident-cpu-0032-request-{000,001,002,003}-validation.json`, and capture
`kokoro-runtime-short-single-cpu-packed-resident-cpu-0032-first-affine-snake-deconv2d-planar-norm-sum4-loop/`.

### Tablet cancellation at a completed phase boundary

A private probe using frozen runtime 0541e68c...70bda9 and driver 0032 observes
actual GPU progress in the second T78 core, then creates the binary's regular
`--cancel-file` marker. The core drains; the runtime observes cancellation at
`after_core` and exits 2. The terminal session is cancelled with one completed of
two requests. Request 0 remains byte-identical across all eight boundary files,
core and waveform to its validated public capture; request 1 and its temporary
output are absent. Reused assets/ORT and pulled output hashes pass before/after
checks. This diagnostic preserves existing accuracy evidence by identity; it
does not fabricate a complete-session or per-request device-capture marker.

Marker-call return to process exit takes 15.793738505 s as observed by the host;
final core cleanup takes 0.263654331 s. Polling and transport contribute to the
observed interval. Peak 485.7 MiB /zero swap, service 51.789 s, no new faults and
both live services active. This validates phase-boundary cancellation only;
active GPU work is not promptly interrupted. A new tablet request after
cancellation, broader phase coverage and response-time bounds remain untested.
The public wrapper still requires complete sessions and does not expose the
cancel marker option; these measurements come from the private guarded probe.

Evidence: `WORK/resident-cancel-0032-private/validation.json`, marker/session,
launch and hash records alongside it; independent audit
`WORK/resident-cancel-independent-kw7685ve/audit.json`. No application/service
wiring changed.

### Padded-channel runtime 0033

`RADISH_NCNN_PAD_CHANNELS=1` enables five additional tiled convolutions for the
three measured K3 channel tuples. It requires `RADISH_NCNN_PLANAR=1` and the
existing FP32 tiled path; default0 and all other routes remain unchanged.
Graph, boundary contract and stored weights are unchanged. The ncnn patch adds
checked channel-padded GPU weights and a pack1 padding prepass. Rebuild both
model/runtime helpers before enabling it; their manifests now record the toggle.
Enabled output names add `-pad-channels`, preserving earlier captures.

The actual four-request sequence, using a new label when repeating it, is:

```sh
RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock \
RADISH_NCNN_PLANAR=1 RADISH_NCNN_NORM_SUM4=1 RADISH_BATCH_CHUNKS=8 \
RADISH_NCNN_PAD_CHANNELS=1 \
  radish/scripts/device-runtime.sh runtime-full padded-channels-0033-first \
  --affine-snake-deconv2d --vulkan --request-loop \
  --next-input runtime-full --next-input runtime-short --next-input runtime-short \
  --threads 4 --ort-threads 4 --max-frames 512 --repeat 1
```

| Request | T / core reuse | Core load s | Core extraction s | Complete synthesis s |
| --- | --- | ---: | ---: | ---: |
| 000 | 213 / fresh process | 9.071568673 | 39.689168303 | 54.928111002 |
| 001 | 213 / retained core | 0 | 39.181154535 | 45.500714193 |
| 002 | 78 / shape reload | 9.328510884 | 14.378352951 | 28.964790620 |
| 003 | 78 / retained core | 0 | 14.370964223 | 18.767524719 |

The new core is still keyed by exact inferred metadata, with fresh frontend/source
and one waveform tail for every request. Each request has 715 ordered GPU/zero CPU
paths. Original-ORT references were generated only after device completion from
each exact captured boundary: device frontend ORT 1.28.2, host references 1.31.1.
Full requests pass all 562342 core values (max 0.000225782394, relative RMS 8.11419901e-7)
and 127800 waveform samples (max 2.09733844e-5, SNR 97.912821255 dB). Short requests
pass all 205942 core values (max 0.000186681747, relative RMS 1.09851758e-6) and 46800
waveform samples (max 1.13752903e-5, SNR97.411696248 dB). Thresholds are unchanged.
All eight boundary files match the earlier validated CPU/GPU captures exactly.
New same-T outputs are byte-identical within this session; they differ from the
previous GPU outputs and pass their own references.

Whole-session peak is 525.8 MiB plus 80.1 MiB swap, no faults/services active.
This is not an isolated fresh-request memory measurement. Session JSON records
149.175802788 s including final cleanup 0.270881183 s; outer printed elapsed is
149.188578 s and service wall 153.529 s. Request synthesis excludes publication
and final teardown, while extraction includes allocator trim. The short miss
includes eviction/reload after full synthesis, not fresh-process startup.
CPU remains faster at 36.802922440/15.411270994 s full/short. Previous GPU resident
46.984076563/20.089174476 s came from different session sequences and, for full,
driver 0031. One resident sample per length, binary changes and memory pressure
preclude sustained or isolated whole-runtime performance claims.

Capture: `WORK/kokoro-runtime-full-single-vulkan-padded-channels-0033-first-affine-snake-deconv2d-planar-norm-sum4-pad-channels-loop/`.
For each `request-000` through `request-003`, use a fresh reference/report path:

```sh
PY=streams/tts/downloads/kokoro-venv/bin/python
"$PY" radish/scripts/import-kokoro-device-boundary.py "$CAPTURE/request-000/boundary" \
  "$WORK/kokoro-runtime-models" "$WORK/NEW-reference-000" --threads 4 --max-frames 512
"$PY" radish/scripts/validate-kokoro-runtime.py "$CAPTURE/request-000" \
  "$WORK/NEW-reference-000" "$WORK/NEW-validation-000.json"
```

Evidence: `WORK/padded0033-runtime-validation-i_6pkny1/{REPORT.md,summary.json}`,
`padded0033-runtime-request-{000,001,002,003}-validation.json`, and
`runtime-padded-channels-0033-first.log`. Patch SHA
`ee30ca55756e7a67283353803621a78a8cb6e78ba11a37606ab0c5981dba8878`;
runtime `5057a939f068bfcd96590067bf2874427df29ab346996b5057f5696bf9949c60`;
driver remains 0032 `c8fee96723942fca768a9026987a5d609071d2c60079058064377635155cb44c`.
The checkpoint and unchanged graph/weight hashes are in
`WORK/padded-channel-integration-0033-checkpoint/after.json` and per-request device
provenance. No live application/service integration or sustained/restart claim.

### Optional pipeline reuse across shape reloads

`--reuse-pipeline-cache` is default off and requires a Vulkan request loop with
`RADISH_NCNN_PIPELINE_CACHE=1`. It retains the device and an external ncnn pipeline
cache across exact-T Net evictions. A shape miss still destroys the old Net and
reloads/packs/uploads weights for the new one; only one weight set is retained.
Positive shape hints and exact-T matching remain unchanged. Both the session and
each request record the option, and enabled output names add `-pipeline-cache`.

The validated short/full/short/short command was:

```sh
RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock \
RADISH_NCNN_PLANAR=1 RADISH_NCNN_NORM_SUM4=1 RADISH_BATCH_CHUNKS=8 \
RADISH_NCNN_PAD_CHANNELS=1 RADISH_NCNN_PIPELINE_CACHE=1 \
  radish/scripts/device-runtime.sh runtime-short shared-pipelines-first \
  --affine-snake-deconv2d --vulkan --request-loop --reuse-pipeline-cache \
  --next-input runtime-full --next-input runtime-short --next-input runtime-short \
  --threads 4 --ort-threads 4 --max-frames 512 --repeat 1
```

Use a new run label when repeating it. The wrapper retains its two-to-four-request
bound, resource limits, asset pins and complete-session publication checks.

| Request | T / exact-Net reuse | Core load s | Core extraction s | Complete synthesis s |
| --- | --- | ---: | ---: | ---: |
| 000 | 78 / fresh process | 9.710721877 | 14.733876446 | 28.937691947 |
| 001 | 213 / shape reload | 3.988262344 | 39.090509623 | 49.434000380 |
| 002 | 78 / shape reload, cached pipelines | 1.162544576 | 14.568416143 | 20.455279049 |
| 003 | 78 / retained core | 0 | 14.191422084 | 18.498216593 |

The external cache records 173,263,263,263 entries; the device cache records four
at each completed request. These are artifact counts, not hits or allocated bytes. Revisiting
T78 adds no entries but still reloads weights. Its 140 executing pipeline handles
and all 1425 ordered dispatch signatures match across requests 000, 002 and 003.
The earlier 0033 short reload was
9.328510884 s versus 1.162544576 s here; these observations have different cache
histories, session order and memory pressure, so they are not a matched timing
pair or a compute-kernel speedup measurement.

All four requests pass unchanged original-ORT core/waveform gates generated after
device completion from their exact boundaries. All eight boundary files and both
outputs are byte-identical to the corresponding validated 0033 captures. Short
waveform SNR is 97.411696248 dB and full SNR 97.912821255 dB. The three T78 requests
also match each other exactly. The driver remains 0032, advertised Vulkan remains
experimental 1.0, and model, shaders and precision are unchanged.

Session time is 120.478595258 s, including final Net → external cache → device
cleanup of 0.279266953 s; service wall is 120.596 s. Whole-session peak is
525.7 MiB plus 41.6 MiB swap, with no faults and both services active. Request
synthesis excludes publication and final teardown; extraction includes allocator
trim. CPU remains faster: the earlier short resident result is 15.411270994 s
versus 18.498216593 s here. This sequence contains no full resident reuse sample;
the earlier CPU/full comparison remains separately scoped. Sustained service,
cache-memory bounds beyond four requests and a live application latency win remain
unestablished.
The tablet cancellation probe above predates this flag. Cleanup uses the reviewed
success/cancellation/failure call sites, but cancellation with a populated shared
cache has not been device-retested.

Capture: `WORK/kokoro-runtime-short-single-vulkan-shared-pipelines-first-affine-snake-deconv2d-planar-norm-sum4-pad-channels-loop-pipeline-cache/`.
Evidence: `WORK/shared-pipelines-runtime-validation-3906w_1n/{REPORT.md,summary.json,manifest.json}`,
`resident-pipeline-cache-result-o2y_6g4d/{REPORT.md,audit.json}`,
`shared-pipelines-request-{000,001,002,003}-validation.json` and
`runtime-shared-pipelines-first.log`. Apply the same per-request import/validation
commands above with new reference/report destinations.

Measured canonical runtime SHA
`dfc2f7a65ffe153ebffc18597e16aa771dfb9cd69e417a723ffdcc7e6de05d66`;
source SHA `27f81a603923ce135920bd0c858ee74c5e734ed39d6e905aac7e1568b29e3737`.
Project source patches are frozen in
`WORK/resident-pipeline-cache-private-logygus0/runtime-pipeline-cache.patch`
(SHA `872d474e33c20707d311dfcb3ca82174541d30bfa8044d076672742bae5fa58b`)
and `WORK/resident-cache-controls-yz7yt3g4/resident-cache-controls.patch`
(SHA `4b15a6b8a9a30d0a01c9d48496647f65c5a61834c036bb4dd791dd7268282eaa`).


### Optional normalization copy removal 0034

`RADISH_NCNN_NORM_OUTPLACE=1` combines a named rank3 input view with a separate
normalization output. It is default off and requires Vulkan FP32,
`RADISH_NCNN_NORM_SUM4=1` and `RADISH_NCNN_RESHAPE_ALIAS=1`. Graph/weight pins,
shape inference and accuracy thresholds are unchanged. New captures record the
control; enabled run/output names add `-norm-outplace`. The focused operator
mode is `device-operators.sh --norm-outplace-only [--compile-only]` with SUM4
and RESHAPE_ALIAS enabled; OUTPLACE0/1 selects the before/after route.

The validated complete-runtime command was:

```sh
RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock \
RADISH_NCNN_PLANAR=1 RADISH_NCNN_PAD_CHANNELS=1 RADISH_BATCH_CHUNKS=8 \
RADISH_NCNN_NORM_SUM4=1 RADISH_NCNN_RESHAPE_ALIAS=1 RADISH_NCNN_NORM_OUTPLACE=1 \
RADISH_NCNN_PIPELINE_CACHE=1 \
  radish/scripts/device-runtime.sh runtime-short norm-outplace-0034-first \
  --affine-snake-deconv2d --request-loop --reuse-pipeline-cache \
  --next-input runtime-short --next-input runtime-full --next-input runtime-full
```

Use a new run label when repeating it. This keeps four CPU/ORT threads, one
extraction per request, and the existing four-request and resource bounds.

| Request | T / exact-Net reuse | Core load s | Core extraction s | Complete synthesis s |
| --- | --- | ---: | ---: | ---: |
| 000 | 78 / fresh process | 9.374135751 | 14.223748612 | 28.003366292 |
| 001 | 78 / retained core | 0 | 14.054969412 | 18.589849111 |
| 002 | 213 / shape reload | 3.315996690 | 37.448352060 | 46.939050940 |
| 003 | 213 / retained core | 0 | 37.088662047 | 43.578287423 |

All four requests pass original-ORT core and waveform references generated after
device completion from their exact boundaries. All eight boundary files and both
outputs are byte-identical to the matching validated 0033 captures; short and
full waveform SNR remain 97.411696248 and 97.912821255 dB. Every request traces
715 GPU/zero CPU core layers. The separate fixed full-core comparison verifies
exactly 49 removed input copies, 49 final output-binding changes and no additional
transfer clones; all 1513 retained command signatures match. See NOTES for the
same-binary 3.66% warm-core improvement and its attribution.

External cache entries are 177/177/271/271, with four device cache entries;
counts remain stable on same-shape reuse. Session time is 137.809958348 s,
including 0.456133282 s final cleanup; service wall is 137.911 s. Whole-session
peak is 504.9 MiB plus 84.2 MiB swap, with no new faults and both services active.
This is not an isolated per-request memory peak. The previous GPU full resident
result was 45.500714 s, while short was 18.498217 s: this run improves full and
leaves short essentially flat. Different order/cache history/swap limit that
comparison. CPU still wins at 36.802922/15.411271 s full/short. Sustained service,
live integration and cancellation with this new path remain unmeasured.

Capture: `WORK/kokoro-runtime-short-single-vulkan-norm-outplace-0034-first-affine-snake-deconv2d-planar-norm-sum4-norm-outplace-pad-channels-loop-pipeline-cache/`.
Evidence: `WORK/norm-outplace-implementation/runtime-first.log`,
`norm-outplace-runtime-validation-drx8p6l9/{REPORT.md,summary.json}`, and
`norm-outplace0034-request-{000,001,002,003}-validation.json`.
The independent profile/provenance audit is in
`WORK/norm-outplace-runtime-audit-0034-gxlmdgfl/`.

Patch SHA `b2112c3ae6e0bc580f0d606b8ac8f5cfc8c23a8ea6426511d249673bab7179ed`;
measured runtime SHA `d1f09b2dcc91b0d25bac576ac14228288f908b0b4e0aaf5a5206c32de5f12ecb`.
Driver 0032 and the experimental Vulkan 1.0 advertisement are unchanged.

### Optional normalization/AffineSnake fusion 0035

`--norm-affine-snake-deconv2d` selects the pinned 594-layer/755-blob graph
`ccab4ff8…aa43`; the executable receives `--norm-affine-snake --deconv2d`.
The runtime and fixed-model wrappers require its complete CPU proof marker.
The fixed fixture kind is `core-shapes-norm-affine-snake-deconv2d`.
`fuse-kokoro-norm-affine-snake.py SOURCE NEW_OUTPUT` creates only a provisional
fixture. Import completed same-runner original/candidate CPU logs and outputs
with `prepare-kokoro-deconv2d-fixture.py --variant norm-affine-snake-deconv2d
SOURCE EVIDENCE RUNNER NEW_OUTPUT`; this variant requires full output byte
identity in addition to the original reference. Weights are unchanged.

The new GPU layer requires `RADISH_NCNN_NORM_SUM4=1` and full FP32. OUTPLACE and
RESHAPE_ALIAS remain separate controls for retained legacy normalizations.
The measured four-request command, using a new run label when repeating, is:

```sh
RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock \
RADISH_NCNN_PLANAR=1 RADISH_NCNN_PAD_CHANNELS=1 RADISH_BATCH_CHUNKS=8 \
RADISH_NCNN_NORM_SUM4=1 RADISH_NCNN_RESHAPE_ALIAS=1 RADISH_NCNN_NORM_OUTPLACE=1 \
RADISH_NCNN_PIPELINE_CACHE=1 \
  radish/scripts/device-runtime.sh runtime-short norm-affine-0035-first \
  --norm-affine-snake-deconv2d --request-loop --reuse-pipeline-cache \
  --next-input runtime-short --next-input runtime-full --next-input runtime-full
```

| Request | T / Net reuse | Core load s | Core extraction s | Complete synthesis s |
| --- | --- | ---: | ---: | ---: |
| 0 | 78 / miss | 9.172902 | 13.788183 | 27.427916 |
| 1 | 78 / hit | 0 | 13.654213 | 18.163352 |
| 2 | 213 / miss | 3.516383 | 35.978882 | 45.676598 |
| 3 | 213 / hit | 0 | 36.437867 | 42.819037 |

All four original-ORT core/waveform references pass, all eight boundary files and
both outputs match validated 0034 bytes, and every request has 589 GPU/zero CPU
core paths. Short/full waveform SNR is 97.411696248/97.912821255 dB. Short uses
1430 commands/4455 submits/25634 chunks; full uses 1520/10235/67163. There are
96 transfers per request. External cache entries remain 177/177/271/271; device
entries are five throughout, one more than 0034 for the new raw-SPIR-V final.
Counts are entries, not cache bytes. Same-shape reuse remains stable.

Session 137.660401781 s includes final cleanup 0.296271739 s; service 137.771 s,
whole-session 531.4 MiB plus 57.2 MiB swap. No new GPU fault; both services active.
Previous 0034 resident full/short is 43.578287/18.589849 s; current 42.819037/18.163352 s
improves in this observation, while CPU 36.802922/15.411271 s remains faster.
These are not sustained or isolated whole-session speedup claims. New graph
cancellation and application integration have not been validated.

Capture: `WORK/kokoro-runtime-short-single-vulkan-norm-affine-0035-first-norm-affine-snake-deconv2d-planar-norm-sum4-norm-outplace-pad-channels-loop-pipeline-cache/`.
Independent runtime profile audit: `norm-affine-runtime-audit-2erfkd8e/REPORT.txt`.
Reference evidence: `norm-affine-runtime-validation-zat6_thf/summary.json` and
`norm-affine0035-request-{000,001,002,003}-validation.json`.
Runtime SHA256 `56c8969c6b32316b19007955155c9462137fc56d01f482af06e2294da11b74c2`;
unchanged driver `c8fee96723942fca768a9026987a5d609071d2c60079058064377635155cb44c`.

### Optional packed-A runtime 0042

The standard build-ncnn.sh now applies/verifies 0042 after 0033 in the ordered
Conv1D stack; fresh consistent generated shader tables are required. Do not
manually apply0042 twice. device-model.sh, device-operators.sh and device-runtime.sh
validate/forward RADISH_NCNN_PLANAR_PACKED_A=0|1 (default 0). The runtime serializer
and capture verifier record that same key. Existing opt-in graph controls remain.

With private models already prepared and a fresh run label:

```sh
RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock \
RADISH_NCNN_PLANAR=1 RADISH_NCNN_PLANAR_PACKED_A=1 \
RADISH_NCNN_PAD_CHANNELS=1 RADISH_NCNN_NORM_SUM4=1 \
RADISH_NCNN_NORM_OUTPLACE=1 RADISH_NCNN_RESHAPE_ALIAS=1 \
RADISH_NCNN_PIPELINE_CACHE=1 RADISH_BATCH_CHUNKS=8 \
RADISH_PROFILE=1 RADISH_NCNN_TRACE=1 \
  radish/scripts/device-runtime.sh runtime-short packed-a-0042-local \
  --norm-affine-snake-deconv2d --request-loop --reuse-pipeline-cache \
  --next-input runtime-short --next-input runtime-full --next-input runtime-full
```

| Request | T / Net | Core load s | Core extraction s | Synthesis s | 0035 synthesis s |
| --- | --- | ---: | ---: | ---: | ---: |
| 0 | 78 / miss | 11.629309210 | 12.961794393 | 28.952951240 | 27.427915819 |
| 1 | 78 / hit | 0 | 12.863021557 | 17.297295788 | 18.163352088 |
| 2 | 213 / miss | 5.929417864 | 34.306032611 | 46.216282882 | 45.676597988 |
| 3 | 213 / hit | 0 | 35.321536017 | 41.225817843 | 42.819037213 |

All four original-ORT validations pass unchanged gates. All eight boundary files,
core and waveform bytes match 0035; short/full waveform SNR is 97.411696/97.912821 dB.
Every request executes 589 GPU/zero CPU layers, 58 packed math owners at native 28,
with unchanged ordered command signatures except those native lengths. Math
chunks/submits remain 9760/1238 (short) and 26256/3294 (full); 96 transfers per request.
Same-shape Net hits remain false/true/false/true, external cache entries 177/177/271/271
and device entries 5 each. These are artifact counts, not occupancy or cache bytes.

Resident synthesis improves 0.866056/1.593219 s, but misses regress 1.525035/0.539685 s.
Load adds 2.456407 s initially and 2.413035 s after shape change; load includes packing,
pipelines and uploads. Summed requests 134.086903108→133.692347753 s save 0.394555355 s.
Session 137.660401781→134.609214313 s includes a much larger change outside request
synthesis, so its 3.051187468 s reduction must not be credited wholly to GPU.
Whole-session peak 512.8 MiB/74.3 MiB swap versus 531.4/57.2 differs; observations are
not sustained controls. No fault/recovery, services active, global ICD 0032 unchanged.

[Compact runtime evidence](results/gpu-packed-a-0042-runtime.json) retains request
pins, output hashes and aggregate scopes. Private measured runtime 0971994a…31c28b
was reproduced exactly by the canonical build, as were helper 61285af2…70678c and
model 0b2f39fa…3c664. Canonical ICD 6ae15421…7813f has the identical reviewed queue
source but differs from measured private driver a9be6b5c…12a4e; no separate device
run of that linked canonical ICD is claimed. Full hashes are in the result JSON.

Capture under WORK: kokoro-runtime-short-single-vulkan-packed-a-0042-first-norm-affine-snake-deconv2d-planar-packed-a1-norm-sum4-norm-outplace-pad-channels-loop-pipeline-cache/.
The historical private suffix packed-a1 differs from the current wrapper's
packed-a suffix. Audit/validation evidence is in packed-a-implementation-0042-a6naaxsx/
runtime-result-audit/ and runtime-integration/packed-a-0042-first-request-{000,001,002,003}-validation.json.
