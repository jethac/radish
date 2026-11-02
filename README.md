# Radish

## TL;DR

Radish is an experimental, compute-first Vulkan driver for Adreno 5xx, built
inside Mesa's Freedreno project. It currently runs the Kokoro neural decoder
through ncnn on the Adreno 506 in a Lenovo ThinkSmart View. FP32 output checks
pass; GPU inference is still slower than the best measured CPU path. The goal
is fast speech inference that makes full use of attainable GPU performance.

## Purpose

Make the GPU in older Snapdragon devices usable by existing Vulkan inference
software. Radish supplies the a5xx hardware backend and the buffer-based compute
features these workloads need, then improves the kernels and submission path
against real model measurements.

The first target is the Adreno 506 / APQ8053 on Linux with the `msm` DRM driver.
Radish shares Mesa's Vulkan infrastructure and Freedreno's ir3 compiler. It is
a separate driver, with its own a5xx resource bindings and command emission.

## Current status

**Current priority:** performance experiments are paused while the driver fills
its Vulkan1.0/1.1 compute execution and resource gaps. The latest
[0044 checkpoint](radish/CORE_API_0044.md) has representative valid-use evidence
for the original40 command paths, alongside focused BASIC, private/constant,
normal/noopt and historical32/64 barrier-reduction checks. A descriptor-order
mistake in earlier resource/RBA probes is documented; corrected update-before-record
runs now pass unchanged resource, depth, array-fetch, robustness and allocation
guard checks. The bounded private array path also passes filtered sampling, gradient LOD,
wrap/border and gather checks. Four-sample color clear/copy/fetch and
depth/stencil size/count consumers also pass with explicit coverage limits;
corrected cube and subword consumers now pass their full outputs and guards after rebuilding the child native texture inventory. Earlier faulting candidates are preserved. The separate sixteen-sampled-texture/four-storage-resource consumer now passes through descriptor-size constants, with existing cube/subword/public robustness checks requalified. Earlier native-query faults remain preserved; high-index S2EN support is not claimed. Focused irregular-workgroup barrier checks and a real128MiB two-layer image now pass. Full-model regression remains pending.
A locally corrected CTS initialization harness runs five information cases:
four pass and the full property-limit test still fails; six corrected fields
reduce its failure messages from37 to31. The40-path count is not complete
semantic coverage or conformance. Graphics is outside scope; remaining compute
limits, sampled-image behavior and legal workgroups are active work. Performance
experiments remain paused; the inference results below are the prior baseline.

Radish runs every core layer of Kokoro's neural decoder on the GPU through ncnn.
The latest opt-in graph fuses 42 normalization/AffineSnake chains and executes
**589 core layers through Vulkan**, with no CPU fallback. Frontend/source and
waveform generation still run on the CPU in one private tablet executable.

Full core and waveform references pass at both input lengths. All boundary and
output bytes match the previous accepted GPU result; full waveform SNR is
**97.91 dB**. No GPU faults occurred, and both live services remained active.

| Measurement | Latest GPU path | Packed six-thread CPU reference |
| --- | ---: | ---: |
| Neural core, full resident request | **35.322 s** | **30.703 s** |
| Complete short synthesis, resident core | **17.297 s** | **15.411 s** |
| Complete full synthesis, resident core | **41.226 s** | **36.803 s** |

The [packed-weight experiment](radish/GPU_PACKED_A.md) improves the same-binary
fixed warm core **35.806 → 34.304 s (4.20%)**, with all 562,342 outputs byte-identical.
All 58 affected convolutions retain the same batching. A balanced representative
convolution measures **7.171 → 6.518 ms GPU elapsed (9.10%)**. Fixed cold load rises
**9.030 →11.427 s**, making load plus first inference **0.712 s slower** in that pair.

The actual 0042 short/short/full/full synthesis loop is **28.953 /17.297 /46.216 /
41.226 s**, versus 0035 **27.428 /18.163 /45.677 /42.819 s**. Resident hits improve;
initial/shape-load misses regress. Loads are **11.629 /0 /5.929 /0 s**. All four
original-ORT core/waveform references pass; every boundary/output byte matches 0035.
Session peak is **512.8 MiB plus 74.3 MiB swap** (prior 531.4/57.2). Summed synthesis
improves only **0.394555 s**; the larger session reduction includes unrelated
pre-request/publication/cleanup differences and cannot be assigned wholly to GPU.

Packed A remains **default off**, exposed through the standard build and device
wrappers. Select it explicitly for this resident-hit/cold-miss tradeoff. The
current table records the new opt-in GPU path; CPU references use the earlier
equivalent graph and still win. Single observations, different driver builds,
cache/clock histories and memory pressure limit attribution. Sustained behavior
and live application integration remain unmeasured. The prior 0035 fixed warm
result was 35.731 s after normalization fusion; its runtime values remain in
[RUNTIME_INTEGRATION.md](radish/RUNTIME_INTEGRATION.md#optional-normalizationaffinesnake-fusion-0035).

GPU saturation remains unproven. At 0035, the dominant 58 tiled convolutions delivered
**10.9 useful GFLOP/s on command wall time**, with only 1.2% padding overhead.
The qualified timer measures one unchanged convolution at **7.161 ms GPU queued
elapsed**. The [0040 paired calibration](radish/GPU_MAD_UNROLL.md) raises the
achieved arithmetic lower bound to **22.53 GFLOP/s**: fourfold loop unrolling
reduces mean GPU time **12.597682 → 11.916471 ms (5.41%)**, with exact outputs.
Its 17.86% reduction in expanded instruction slots does not imply an equivalent
timing gain or a hardware ceiling. The earlier copy calibration achieves
**4.30 GB/s logical read+write**, not measured physical DRAM bandwidth. Workload
and packet geometry differ from convolution; model performance is unchanged.
See [GPU_TIMING.md](radish/GPU_TIMING.md) and
[GPU_CALIBRATION.md](radish/GPU_CALIBRATION.md) for the earlier measurements.

The per-group register guard does not establish total physical capacity, and
compiler wave estimates do not establish occupancy. The
[state-reuse experiment](radish/GPU_STATE_REUSE.md) preserved every output but
reduced GPU time only **0.42%**, below its planned 5% criterion. A separate
[compiler experiment](radish/GPU_REPEAT_COALESCING.md) formed repeat MAD
instructions but added moves inside the reduction loop and was rejected before
tensor execution. Both remain excluded. The current default-off diagnostic
driver is **0041**; 0042 adds an incremental timer for the packed-weight
experiment. The 0042 measurements use private driver a9be6b5c…12a4e. The rebuilt canonical
ICD 6ae15421…7813f is separately linked; the global device ICD remains**0032**.

Counter programming is deferred: the inspected A506 sources do not establish
exclusive counter ownership while other services remain active. No new counter
code is implemented. The [shared-hot diagnostic](radish/GPU_SHARED_HOT.md)
now measures **7.213085 → 5.417721 ms (24.89% less GPU time)** at equal issued
arithmetic work, with all exact references passing. Both source variants failed
the original no-hot-MOV gate; before timing, the scope was revised to compare
the original complete implementation including its extra register moves. The
result does not isolate memory or barrier cost, establish a conservative bound,
or improve a production model. It guides the next kernel investigation. A CPU
win alone will not close the goal.

A tablet cancellation probe preserves the completed request and discards the
second request after its active GPU core finishes. Exit follows the cancel marker
by about 15.8 s in this observation; it does not interrupt GPU work promptly.
The probe reports 485.7 MiB/zero swap, no faults and both services active.
Restart after cancellation, the new fused graph and broader cancellation
coverage remain untested.

Select the measured graph with `--norm-affine-snake-deconv2d`,
`RADISH_NCNN_PLANAR=1`, `RADISH_NCNN_PLANAR_PACKED_A=1` and
`RADISH_BATCH_CHUNKS=8`, with one X workgroup.
The latest measurements additionally enable `RADISH_NCNN_NORM_SUM4=1` and
`RADISH_NCNN_PAD_CHANNELS=1`, plus `RADISH_NCNN_NORM_OUTPLACE=1` with
`RADISH_NCNN_RESHAPE_ALIAS=1`; these optimizations remain default-off except the
runtime wrapper's existing reshape-alias default. The exact
graph preserves every model weight and expresses
three 1D deconvolutions as stock height-one 2D Vulkan operators with reshapes.
The preparation/validation tools require completed full CPU reference evidence.
Existing graph defaults remain unchanged. See
[RUNTIME_INTEGRATION.md](radish/RUNTIME_INTEGRATION.md) for preparation and use.

Earlier normalization, activation, layout and submission changes brought warm
GPU time down from 101.275 s. The earlier K2 convolution-loop unrolling candidate was
correct but 7–9% slower in paired operator measurements and remains excluded.
Optional X pairing has no established overall performance win. The normalization
optimization has passing fixed-core and fresh full results; sustained behavior
with the combined graph remains unmeasured.
Mixed FP16 storage still fails full-model accuracy gates, so FP32 remains the
validated path. Detailed results and failed experiments are in
[NOTES.md](radish/NOTES.md).

This is an experimental compute-only ICD. It provides no graphics or presentation
and has no Vulkan 1.0 or 1.1 conformance claim. Core indirect dispatch now reads
arguments at execution time and passes 36 device checks, including host changes
after recording and arguments written by a GPU transfer. Additional resource
types, public dispatch bases, secondary command buffers, events and subgroup
support remain among the API gaps. CTS is built but has not run on the device. See
[VULKAN_AUDIT.md](radish/VULKAN_AUDIT.md).

## Repository contents

The `radish/` layout is preserved from the device-development workspace so the
build and test scripts retain their relative paths.

| Path | Contents |
| --- | --- |
| [radish/GOAL.md](radish/GOAL.md) | Purpose, scope, milestones and completion criteria |
| [radish/NOTES.md](radish/NOTES.md) | Measurements, failures, current work and reproduction details |
| [radish/patches/](radish/patches/README.md) | Complete driver source as cumulative Mesa patches, plus ordered ncnn adaptations |
| [radish/scripts/](radish/scripts/) | Cross-build, model preparation and isolated device runners |
| [radish/tests/](radish/tests/) | Compute checks, ncnn operator/model runners and waveform validation |
| [radish/ABI.md](radish/ABI.md) | Shader ABI and a5xx dispatch mapping |
| [radish/CAPABILITIES.md](radish/CAPABILITIES.md) | Implemented features and their evidence |
| [radish/VULKAN_AUDIT.md](radish/VULKAN_AUDIT.md) | Vulkan 1.0/1.1 gaps |
| [radish/RUNTIME_INTEGRATION.md](radish/RUNTIME_INTEGRATION.md) | Fresh-input Kokoro integration and remaining work |
| [radish/GPU_TIMING.md](radish/GPU_TIMING.md) | Qualified GPU elapsed diagnostic, results and reproduction |
| [radish/GPU_CALIBRATION.md](radish/GPU_CALIBRATION.md) | Measured arithmetic/copy rates, raw correctness and reproduction |
| [radish/GPU_STATE_REUSE.md](radish/GPU_STATE_REUSE.md) | Rejected state-reuse experiment and paired GPU measurements |
| [radish/GPU_REPEAT_COALESCING.md](radish/GPU_REPEAT_COALESCING.md) | Compiler repeat/copy tradeoff and compile-only rejection |
| [radish/GPU_MAD_UNROLL.md](radish/GPU_MAD_UNROLL.md) | Paired loop-overhead calibration, exact outputs and achieved arithmetic rate |
| [radish/GPU_PACKED_A.md](radish/GPU_PACKED_A.md) | Packed-A focused/model/runtime results and cold-load tradeoff |
| [radish/GPU_SHARED_HOT.md](radish/GPU_SHARED_HOT.md) | Combined shared-hot diagnostic, strict admission failures and paired results |

## Building and running

This is a development checkpoint. The scripts require external source trees,
an aarch64 sysroot and host build tools; model weights and device captures are
kept outside the repository.

1. Check out Mesa at `ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230` in a
   separate worktree. Apply
   `radish/patches/0041-radish-shared-hot-calibration-cumulative.patch` to that base.
   Earlier cumulative Mesa patches are historical alternatives, not a series.
2. Set `RADISH_WORK` to the scratch/build directory, `RADISH_MESA` to the Mesa
   checkout, `RADISH_SYSROOT` to the aarch64 sysroot and `RADISH_VENV` to the
   host build environment. Run `radish/scripts/build.sh`.
3. For ncnn, use revision `f18f3ffc94dbc76a277e94556c588ab2c2d0e1d9` with
   glslang `780b6a209132dc74e99458f62ebe5fa7e57c41cf`. Set `RADISH_NCNN` to
   that checkout. `build-loader.sh` prepares the loader/toolchain and
   `build-ncnn.sh` applies or verifies the supported patch stack, including0042.
   Do not manually apply0042 again; its runtime selector defaults to0.
4. For the private Kokoro runtime, `radish/scripts/build-runtime.sh` builds the
   combined executable. The required private model exports, device runner and
   exact-boundary validation commands are in
   [RUNTIME_INTEGRATION.md](radish/RUNTIME_INTEGRATION.md).
5. Read [NOTES.md](radish/NOTES.md) and the chosen device runner before running
   it. Select the device with `RADISH_SERIAL` and use an explicit private ICD.
   Set `RADISH_DEVICE_LOCK` to the same absolute lock-file path used by every
   other workspace accessing that tablet. Its default is this checkout's
   `.device.lock`.

The runners keep the appliance services active, bound memory and runtime, and
check for new GPU faults. A fresh checkout does not contain the original
workspace's sysroot, tool environment, model fixtures or historical logs.
For the existing Lenovo workspace, use its shared
`/home/jethac/Projects/lenovo-8/.device.lock` when running from either checkout.

With the private fixtures prepared, the public wrapper accepts two to four
ordered `runtime-short`/`runtime-full` requests. To reproduce the latest
short/short/full/full sequence, choose a new run label and use:

```sh
RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock \
RADISH_NCNN_PLANAR=1 RADISH_NCNN_NORM_SUM4=1 RADISH_BATCH_CHUNKS=8 \
RADISH_NCNN_PAD_CHANNELS=1 RADISH_NCNN_NORM_OUTPLACE=1 \
RADISH_NCNN_RESHAPE_ALIAS=1 RADISH_NCNN_PIPELINE_CACHE=1 \
  radish/scripts/device-runtime.sh runtime-short norm-affine-NEW \
  --norm-affine-snake-deconv2d --vulkan --request-loop --reuse-pipeline-cache \
  --next-input runtime-short --next-input runtime-full --next-input runtime-full \
  --threads 4 --ort-threads 4 --max-frames 512 --repeat 1
```

Loop mode requires the explicit combined graph and repeat 1; one to three
`--next-input` selectors follow the first positional fixture. Each request keeps
the existing boundary/core/waveform capture and device provenance; a session
manifest records ordering, reuse and terminal status. Run the unchanged
same-boundary reference importer and full validator separately for each request
after capture. Generation alone does not establish accuracy. One-shot defaults
and `--repeat N` semantics remain unchanged.

## License

Original Radish code and tools are [MIT licensed](LICENSE). Mesa and ncnn
notices and applicable upstream terms are preserved; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
