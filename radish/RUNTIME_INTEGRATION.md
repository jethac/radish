# Kokoro runtime integration audit

2026-11-01: dynamic frontend/source export and fresh T=78/T=213 captures pass
on the host; the fresh T=78 FP32 neural core also passes on the tablet. Live
frontend/core/waveform integration and tablet harmonic-source execution remain
incomplete. The fixed core and separate on-device waveform results are recorded
in [NOTES.md](NOTES.md).

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
The installed tablet runtime is documented in `streams/tts/NOTES.md` as ORT
**1.28.2**, from sherpa-onnx v2.0.1. This audit did not recheck or execute it.

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

The independent packed-FP16 failure and source-only 0014 tiled-convolution and
0015 mixed-storage candidates are tracked in [NOTES.md](NOTES.md). None supplies
a live integration, conformance or performance-win claim.

## Remaining integration work

- Run the exported frontend/source on the actual tablet ORT runtime before
  claiming device support or frontend latency. Host ORT 1.31.1 is not installed
  tablet ORT 1.28.2 evidence.
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
