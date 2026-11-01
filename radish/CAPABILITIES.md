# Capability evidence

Experimental API negotiation is 1.0, with no optional device features/extensions;
the instance exposes `VK_KHR_get_physical_device_properties2`.
This is not a conformant Vulkan implementation. Source observations
are not hardware validation. Paths refer to the pinned revisions in `NOTES.md`.

Verified on A506: loader discovery, compute-only queue creation, BO allocation
and mapping, fixed sparse SSBO bindings, push constants, SPIR-V compilation,
direct dispatch and fenced host readback. See `NOTES.md` for the precise test
scope; untested limits remain unproven.

| Requirement | Evidence | Planned behavior / unresolved check |
| --- | --- | --- |
| Vulkan 1.0 instance path | ncnn `src/gpu.cpp:3108–3167` | Initial experimental negotiation ceiling 1.0; no conformance claim |
| Compute-only queue | ncnn `src/gpu.cpp:586–627` | One compute/transfer queue; no graphics |
| Fixed storage-buffer descriptors | ncnn Vulkan shaders and ordinary descriptor fallback in `src/command.cpp` | Flatten set/binding into a5xx slots; verify slot limits and array indexing |
| Push descriptors/templates | ncnn `src/command.cpp`, guarded by extension queries | Do not expose initially; ordinary descriptor sets first |
| Native FP16 arithmetic/storage | ncnn queried features and options | Features remain disabled; uint-packed half values with FP32 arithmetic require neither feature; native paths need a separate audit and validation |
| Subgroups/cooperative matrices | ncnn feature-gated shader paths | Disable initially; keep legal workgroup/shared-memory fallback |
| Shader specialization constants | ncnn `src/pipeline.cpp` | Required for operator pipelines and workgroup selection |
| Push constants | ncnn `src/command.cpp` | Required; audit constant-register budget and ir3 ABI |
| Shared workgroup memory/barriers | `shared-cap-lastword-green.log`, `shared-reject-green.log` | 16 KiB driver cap; tree barriers/last word tested at 128 invocations; >16 KiB addressing fault unresolved; no general memory-model claim |
| Host-visible buffers | Default Freedreno WC BO mappings; `coherent-green.log` | Host coherent; 32 dispatches verified without flush/invalidate; broader memory-model coverage pending |
| Direct and indirect dispatch | `fd5_compute.c` | Direct first; indirect before backends that require it |
| Dispatch subdivision / builtins | `grid-base{,-shared}-green.log`, `dispatch-chunk-green.log` | Logical IDs/counts preserved across bounded hardware chunks; internal base ABI only, no DispatchBase advertisement |
| Bounded submission batching | Patch 0009, `core-shapes-batch4.log` | Opt-in 1..4 short, loop-free, non-barrier chunks per submit; grid/shared/dependency/full-core checks pass; default 1 |
| Shared32 group budget | Patch 0012, `shared32-groups8.log`, `ncnn-reduce-groups8.log`, `core-groups8-repeat2.log` | Opt-in 1..8 groups per hardware chunk for barrier kernels with at most 32 physical invocations; default 1; shared outputs/guards, 18 means and full core pass |
| Compiled register footprint | Current cumulative patch 0016, `operators-tiled-register-reject.log` | Combined full/half footprint bounded by measured 2048 full vec4 budget; entire barrier group rounds to waves, non-barrier requires one wave; old local128/21-full shader rejects with -8 before upload/execution, revised 16-full passes; not a general occupancy proof |
| Recovery detection | `ncnn-reduce-bounded.log`, matching kernel `msm_gpu.c` | Recovered fault returns DEVICE_LOST via per-context counter; does not cover all IOMMU fault paths |
| Transfer / dispatch dependencies | `transfer-bytes-green.log`, `chain-first.log` | Aligned CP copies and unaligned CPU fallback pass selected ordering tests; 48 dependent dispatches pass; serial execution needs optimization |
| ncnn Reduction | `ncnn-reduce-default32.log` | Backend patch 0005 uses 32 threads; all 18 mean cases pass; identical physical64 shader still hangs |
| ncnn Conv1D / InnerProduct | `operators-full.log`, `operators-induction-execute.log` | Five original FP32 cases pass; K3/K7/K11 and full core also pass after padding hints/FP32 induction patches 0010/0011 |
| ncnn shared32 convolution | Patch 0013, `operators-shared32-specialization-compile.log`, `operators-shared32{,-groups8}-execute.log` | Optional exact Radish FP32 Conv1D GEMM local8x4 with 8 KiB shared arrays; initial GLSL declaration issue corrected with specialization ID15; K3/K7/K11 and full model pass, no faults; full model is bit-identical but slower at 198.453 s, so the path remains opt-in and is rejected for performance |
| ncnn tiled FP32 convolution | Revised 0014/0017, `operators-tiled-{scalar,transpose}-{compile,execute}.log`, `core-instancenorm-tiled-transpose-first.log` | Both builds pass; local128, 16 full/0 half registers, 16 KiB shared; 0017 operator 950 instructions/1152 dwords/4 loops; all 25984 outputs pass at10.868408 ms/8 submits; full fused model also passes 562342 values with 37 tiled math commands, no faults |
| ncnn packed-FP16 conversion/ReLU | `packed-compile.log`, `packed-execute.log` | Four local32 pipelines pass changing inputs, guards, adjacent-half preservation and subnormal unpack; uint SSBO packing with FP32 arithmetic; native features remain false |
| ncnn packed-FP16 Conv1D / InnerProduct | `operators-fp16-packed-{compile,execute}.log` | All five existing fixtures pass original-FP32 references with declared storage bounds, no faults; K11 local16x4 and large InnerProduct local64 are barrier-free; these passes do not establish packed model accuracy |
| ncnn packed-FP16 means | `ncnn-reduce-fp16-packed-{compile,execute}.log` | Local32, 16 full/13 half registers; all 18 cases and 84 outputs pass declared storage bounds with shared group budget 8 and no faults; physical64 barrier issue unresolved |
| Kokoro packed-FP16 core | `core-fp16-packed-first.log`, preserved packed candidate | FAILED: all 562342 outputs NaN; six zero variance-plus-epsilon channels in blob 437 match 153366 nonfinite blob 439 values; 107.515 s is not an accepted benchmark |
| GPU mixed storage / FP32 norms | Patch 0015, `core-norm32-mixed-first.log`, `packed-norm32-waveform-diagnostic.log` | Boundary conversion and 54 masked chains make blob 437 positive and blob 439 finite; full core is finite but FAILS 168167 values, max error 0.368797302, rms 0.00359251526; waveform FAILS SNR 40.421776 dB/43435 values; 100.270060 s is not an accepted inference benchmark |
| Kokoro neural core | `core-groups8-repeat2.log` | All 562342 FP32 outputs remain bit-identical; 1360 GPU layer calls and 3 CPU deconvolutions; 103.192 s first / 101.275 s warm (one post-warmup iteration), 37358 submits each, peak 416.1 MiB, zero swap |
| Kokoro waveform fixture | `waveform-device.log` | Portable CPU tail passes all 127800 original samples on device, SNR 97.91 dB, wall 0.105977 s; saved harmonic source, separate from core timing |
| FP32 InstanceNorm core variant | `core-instancenorm-first.log`, `core-instancenorm-waveform-device.log` | Guarded 54-chain fusion preserves weights/aliases; all 562342 core values pass, max error 0.000129699707; first extraction 88.314593 s, peak 424 MiB/swap 72.2 MiB; all 127800 waveform samples pass, SNR 98.017571 dB; no faults; saved-source fixture, not a controlled warm benchmark |
| FP32 InstanceNorm+tiled core | `core-instancenorm-tiled-transpose-repeat2.log` | Both 562342-value extractions pass, max error 0.000106811523, rms 7.80397212e-7; 76.534312 s first/74.317956 s warm (one warm), cold 9.379 s, 417.8 MiB/zero swap, no faults; first/warm output SHA identical; actual trace 1090 GPU calls/3 CPU deconvolutions, 37 tiled math commands |
| InstanceNorm+tiled waveform | `core-instancenorm-tiled-transpose-waveform-device.log` | All 127800 samples pass, max error 1.12576771e-5, SNR 97.967904 dB, zero mismatches; 0.104488 s, 4.7 MiB/zero swap, no faults; saved-source fixture |
| Dynamic T=78 core/tail | `runtime-short-ncnn-device.log`, `runtime-short-ncnn-waveform-device.log` | Fresh host boundary: 205942 GPU core outputs and 46800 device waveform samples pass; tail 0.043091 s, SNR 96.176020 dB, 2.9 MiB/zero swap, no faults |
| Tablet frontend/source boundary | `runtime-short-frontend-device-first.log` | Actual T=78 CPU frontend/source generation passes in 4.66509 s, peak 181.3 MiB/zero swap; validated separately from GPU core/tail, no integrated end-to-end timing |
| Preserved tablet boundary through GPU/tail | `runtime-device-short-ncnn-device.log`, `runtime-device-short-ncnn-waveform-device.log` | Same actual tablet inputs: 205942 core outputs pass in 38.844565 s, max error 1.02519989e-4, 318.7 MiB/zero swap; 46800 waveform samples pass in 0.040910 s,SNR 97.957328 dB, 2.4 MiB/zero swap, no faults; separately timed stages, not live end-to-end latency |
| CPU performance baseline | `core-instancenorm-cpu-repeat3.log`, earlier `core-cpu-packed-omp6.log` | Same fused graph, packed FP32/six SIMPLEOMP threads: 32.326136 s first, 32.025450/32.132638 s warm, mean 32.079044 s (two warm), 444.4 MiB/zero swap; all 562342 outputs pass, max error 0.000507354736; GPU 74.317956 s (one warm) is 2.317x slower; prior nonfused CPU mean 32.052271 s; no end-to-end win |
| ncnn pipeline reuse | Patch 0008, `core-cache-layouts.log` | Opt-in exact Radish A506 exception to old Qualcomm heuristic; cache keys/ownership/API unchanged; does not prove persistent Vulkan cache semantics |
| CTS | `build-cts/{BINARY,CASELIST_REVIEW}.json` | Pinned headless binary and bounded case lists prepared; no device CTS execution |
| ggml compatibility | Local `ggml-vulkan.cpp:5194,:4642` | Later target: Vulkan 1.2 instance floor and 16-bit storage requirement need resolution |
| Bindless descriptors | Public 2025 maintainer discussion; a5xx uses bindful resources | Separate mapping; no descriptor-indexing advertisement |

The ncnn Vulkan-only ReLU test passes three sizes through upload, compute and
download (`ncnn-relu-default32.log`). The fixed real-model core and saved-source
waveform now pass; general API compatibility, packed-FP16 model accuracy, native
FP16 features, live waveform integration and an end-to-end performance win remain
unproven.
