# Kokoro runtime integration audit

2026-11-01: the standalone CPU frontend and harmonic source now run on the
tablet from token IDs/style/speed, producing a fresh T=78 boundary. Its exact
bytes have matching host ORT decoder/tail references, and the tablet GPU core
and portable waveform tail both pass on that boundary. This completes the
staged T=78 chain with host reference generation and shape capture between
device stages. A single executable and live application integration remain
unfinished. Fixed-core and separate waveform evidence is recorded in
[NOTES.md](NOTES.md).

## Proposed route

Keep harmonic generation in the existing CPU ONNX Runtime frontend, return its
`har` tensor with `asr`, F0, N and durations, feed those tensors plus style into
ncnn, then run the portable CPU waveform tail. This reuses an existing runtime
and the original source graph; no new GPU feature is needed.

Start with a private benchmark and the FP32 frontend from the original model.
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

## Remaining integration work

The next runtime implementation should remove the host calibration step with a
small metadata-only shape pass over the pinned layout graph. Its 17 operator
kinds have integer shape rules; seed the five inputs from T, propagate each
operation's actual padding/stride/dilation/reshape/broadcast rules, and append
standard `-23330` hints before `Net::load_param_mem`. Require exact agreement
with all 1739 existing T78/T213 captured shapes. Do not infer formulas by fitting
those two observations. Upstream `ModelWriter::shape_inference()` executes an
Extractor for every blob, so it does not remove calibration costs.

Carry the validated InstanceNorm graph and tiled-kernel options into that
executable as well. After matching the original graph's hints, validate the
1307-blob fused graph, including its rank-three normalization reshapes. Removing
host calibration must preserve the measured graph optimizations.

Then factor the existing frontend/source, ncnn extraction and portable tail into
one private executable, keeping tensors in memory and releasing ORT sessions
before GPU allocation. An optional sequential CPU comparison must reuse the
same boundary. Start with one Net for the actual T and rebuild on a T change;
measure resident reuse, combined memory, cold/warm latency and cancellation
before treating it as an application backend.

- Replace per-input host shape capture with validated propagation from graph
  metadata and T, then run frontend, ncnn and tail in one private executable.
  Keep the existing staged references as the baseline. Full T213 tablet
  frontend/source execution remains unmeasured.
- For every new T, keep shape hints and compiled specializations matched to
  actual dimensions. T78 device success does not justify reusing its hints at
  another length or claiming arbitrary-length compatibility.
- Add the ncnn handoff and portable tail to a private executable before wiring
  an application backend. Reuse the existing frontend/token/style/duration
  interfaces; keep generated weights and model graphs out of the repository.
- Measure combined memory, cold/warm latency and cancellation. The resident
  frontend adds memory beyond current core-only measurements. Initially,
  generating boundaries and releasing the ORT session before GPU inference can
  isolate memory costs; it does not establish resident production performance.

Keep device locking, bounded sessions, both live services and fault inspection
requirements from [GOAL.md](GOAL.md). This audit does not complete M5 or M6.
