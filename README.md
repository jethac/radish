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

- Private ICD loading, buffer compute, descriptors, transfers, shared memory
  and synchronization have targeted device checks.
- A private executable now runs fresh Kokoro frontend/source, ncnn core and
  waveform synthesis entirely on the tablet. Shape hints come from graph
  metadata; inference no longer needs host calibration between stages.
- The neural core uses GPU operations and three CPU deconvolution fallbacks.
  Full core and waveform comparisons pass at two input lengths, including
  all 127,800 samples of the full-length output at **98.36 dB SNR**.

| Measurement | GPU path | Packed six-thread CPU |
| --- | ---: | ---: |
| Fixed full-size core, warm extraction | **46.933 s** | **32.079 s** |
| Fresh full-length synthesis, including startup | **63.321 s** | **39.790 s** |

The GPU figures use the opt-in FP32 Snake graph, planar convolution input,
output reshape aliases and eight sequential tiled chunks per submission.
Warm core timing is one extraction,
versus two for the CPU mean; the CPU uses the preceding InstanceNorm graph.
Complete synthesis figures are single startup runs with byte-identical
frontend boundary inputs. The GPU core peaks at **418.4 MiB, zero swap**;
full GPU synthesis peaks at **429.7 MiB with zero swap**. No new GPU faults
occurred, and both appliance services remained active. Core and waveform bytes
match the preceding GPU implementation. GPU inference remains slower than CPU.
Resident warm synthesis and sustained thermal behavior remain unmeasured.

Channel-major convolution input and its 64-thread padding pass reduce warm
time from 49.429 s to 46.933 s. The affected convolution/padding commands save
1.378 s; variation in other work also contributes to the 2.496 s overall change.
Fresh full-length synthesis is essentially unchanged from 63.528 s. The layout
remains disabled by default: select `RADISH_NCNN_PLANAR=1`, ordered ncnn patches
0024/0026 and cumulative Mesa patch 0025. Full core and waveform bytes remain
identical to the preceding GPU implementation at both measured input lengths.

Cumulative Mesa patch 0027 also validates optional pairs of convolution
workgroups with `RADISH_TILED_GROUPS_X=2`. Full output remains identical, but
the affected math time stays near 24.1 s and establishes no performance gain.
The recommended setting remains one workgroup; the measurements above use it.

- Fused FP32 normalization and tiled convolution, including dilation 3/5,
  guarded output reshape aliases, sequential submission batching and Snake
  activation fusion reduce fixed-core warm GPU time from 101.275 s to 49.429 s.
  Snake replaces 240 arithmetic dispatches with 48 and removes 7,680 submissions;
  its measured improvement is 8.6% over the preceding 56.014 s checkpoint.
  Raising sequential tiled batching from four to eight then removes another
  3,231 submissions and reduces warm time from 51.188 s by a further 3.4%.
  The tiled path executes 53 matrix-product commands; compiled register-budget
  checks remain in place.
- Fresh short-utterance synthesis with Snake and planar input takes **31.891 s**,
  including startup, and passes every core/audio comparison at
  **329.1 MiB, zero swap**.
  Select it with `device-runtime.sh ... --snake`; InstanceNorm remains the
  default graph. Snake is a project-local ncnn custom layer requiring no new
  Mesa feature; cumulative patch 0025 includes the planar batching case.
- Explicit mixed-storage conversion and FP32 normalization remove the NaNs
  from packed FP16 inference, but full-model and waveform accuracy still fail.
  FP32 remains the validated path.

This is an experimental compute-only ICD. It does not provide graphics or
presentation, and it has no Vulkan 1.0 or 1.1 conformance claim. Supported,
unsupported and untested behavior is recorded in the capability documents.

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
   `radish/patches/0025-radish-planar-batching-cumulative.patch` to that base.
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

## License

Original Radish code and tools are [MIT licensed](LICENSE). Mesa and ncnn
notices and applicable upstream terms are preserved; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
