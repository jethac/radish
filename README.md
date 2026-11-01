# Radish

## TL;DR

Radish is an experimental, compute-first Vulkan driver for Adreno 5xx, built
inside Mesa's Freedreno project. It currently runs the Kokoro neural decoder
through ncnn on the Adreno 506 in a Lenovo ThinkSmart View. FP32 output checks
pass; GPU inference is still slower than the best measured CPU path. The goal
is useful, faster GPU speech inference.

## Purpose

Make the GPU in older Snapdragon devices usable by existing Vulkan inference
software. Radish supplies the a5xx hardware backend and the buffer-based compute
features these workloads need, then improves the kernels and submission path
against real model measurements.

The first target is the Adreno 506 / APQ8053 on Linux with the `msm` DRM driver.
Radish shares Mesa's Vulkan infrastructure and Freedreno's ir3 compiler. It is
a separate driver, with its own a5xx resource bindings and command emission.

## Current status

Radish runs every layer of Kokoro's neural decoder on the GPU through ncnn.
The latest graph eliminates the three CPU deconvolution fallbacks: all **715
executed core layers use Vulkan**. Frontend/source generation and the waveform
tail still run on the CPU in one private tablet executable.

Full core and waveform comparisons pass at two input lengths. The full output
contains 127,800 samples and passes at **97.91 dB SNR**. No GPU faults occurred,
and both live appliance services remained active.

| Measurement | Latest GPU path | Packed six-thread CPU |
| --- | ---: | ---: |
| Neural core, full resident request | **37.089 s** | **30.703 s** |
| Complete short synthesis, resident core | **18.590 s** | **15.411 s** |
| Complete full synthesis, resident core | **43.578 s** | **36.803 s** |

The latest opt-in change removes 49 normalization input copies while preserving
shared residuals. In a same-binary fixed-core comparison, warm extraction falls
**38.467 → 37.059 s (3.66%)**. Profiling attributes **1.566 s** to the affected
copy/normalization work; all 1513 remaining command signatures match. All 562,342
output values are byte-identical to the previous GPU result and pass the unchanged
reference. Fixed-core peak is **402.8 MiB plus 6.7 MiB swap**, versus 405.6 MiB
with zero swap for the disabled path. This is one warm observation per mode.

A validated short/short/full/full loop takes **28.003 / 18.590 / 46.939 / 43.578 s**
for complete synthesis. Each request regenerates frontend/source and waveform;
only the core is retained for matching shapes. All four core/waveform references
pass, and all eight boundary inputs plus both outputs match validated 0033 bytes.
Session peak is **504.9 MiB plus 84.2 MiB swap**, with no faults and both services
active. This is a whole-session peak, not a per-request measurement.

Optional `--reuse-pipeline-cache` also preserves compiled pipelines across shape
changes while replacing the network and reloading weights. This run loads the
core in **9.374 / 0 / 3.316 / 0 s**. External cache entries are 177/177/271/271,
stable on same-shape reuse. The earlier short/full/short/short run demonstrated
cached pipelines surviving a shape revisit; details are in NOTES.

The previous GPU resident results were **45.501 s full / 18.498 s short**.
Full improves in the latest observation; short is essentially flat. Different
sequence, cache history and swap limit that comparison. CPU remains faster on
the same graph and exact boundaries at **36.803 / 15.411 s full/short**,
404.8 MiB peak and zero swap. Sustained behavior and live application integration
remain unmeasured.

A tablet cancellation probe preserves the completed request and discards the
second request after its active GPU core finishes. Exit follows the cancel marker
by about 15.8 s in this observation; it does not interrupt GPU work promptly.
The probe reports 485.7 MiB/zero swap, no faults and both services active.
Restart after cancellation and broader cancellation coverage remain untested.

Select the measured graph with `--affine-snake-deconv2d`,
`RADISH_NCNN_PLANAR=1` and `RADISH_BATCH_CHUNKS=8`, with one X workgroup.
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
GPU time down from 101.275 s. The latest reduction-loop unrolling candidate is
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

## Building and running

This is a development checkpoint. The scripts require external source trees,
an aarch64 sysroot and host build tools; model weights and device captures are
kept outside the repository.

1. Check out Mesa at `ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230` in a
   separate worktree. Apply
   `radish/patches/0032-radish-indirect-dispatch-cumulative.patch` to that base.
   Earlier cumulative Mesa patches are historical alternatives, not a series.
2. Set `RADISH_WORK` to the scratch/build directory, `RADISH_MESA` to the Mesa
   checkout, `RADISH_SYSROOT` to the aarch64 sysroot and `RADISH_VENV` to the
   host build environment. Run `radish/scripts/build.sh`.
3. For ncnn, use revision `f18f3ffc94dbc76a277e94556c588ab2c2d0e1d9` with
   glslang `780b6a209132dc74e99458f62ebe5fa7e57c41cf`. Set `RADISH_NCNN` to
   that checkout. `build-loader.sh` prepares the loader/toolchain and
   `build-ncnn.sh` applies or verifies the supported patch stack.
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
full/full/short/short sequence, choose a new run label and use:

```sh
RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock \
RADISH_NCNN_PLANAR=1 RADISH_NCNN_NORM_SUM4=1 RADISH_BATCH_CHUNKS=8 \
RADISH_NCNN_PAD_CHANNELS=1 \
  radish/scripts/device-runtime.sh runtime-full padded-channels-NEW \
  --affine-snake-deconv2d --vulkan --request-loop \
  --next-input runtime-full --next-input runtime-short --next-input runtime-short \
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
