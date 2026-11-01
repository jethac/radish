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
- ncnn runs the Kokoro neural core with GPU operations and three CPU
  deconvolution fallbacks. A complete FP32 output comparison passes.
- On the fixed full-size fixture, fused normalization and tiled convolutions
  reduce warm GPU core extraction from **101.275 s to 74.318 s**. The same
  graph takes **32.079 s** on the optimized six-thread ncnn CPU path. These
  are neural-core timings, not complete text-to-speech latency; the new GPU
  result contains one warm extraction, the CPU mean contains two.
- Fresh tablet-generated inputs at a different sequence length pass the GPU
  core and waveform comparisons: **38.845 s** for 205,942 core outputs and
  **97.96 dB SNR** for 46,800 audio samples. This is a staged chain with host
  reference preparation between stages, not a measured end-to-end runtime.
- Explicit mixed-storage conversion and FP32 normalization remove the NaNs
  from packed FP16 inference, but full-model and waveform accuracy still fail.
  FP32 remains the validated path.
- The opt-in tiled kernel now passes the full model. It accelerates 37
  convolutions; extending it to the remaining dilated convolutions is next.
  The driver rejects shaders beyond A506's measured register budget.

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
   `radish/patches/0016-radish-register-budget-cumulative.patch` to that base.
   Earlier cumulative Mesa patches are historical alternatives, not a series.
2. Set `RADISH_WORK` to the scratch/build directory, `RADISH_MESA` to the Mesa
   checkout, `RADISH_SYSROOT` to the aarch64 sysroot and `RADISH_VENV` to the
   host build environment. Run `radish/scripts/build.sh`.
3. For ncnn, use revision `f18f3ffc94dbc76a277e94556c588ab2c2d0e1d9` with
   glslang `780b6a209132dc74e99458f62ebe5fa7e57c41cf`. Set `RADISH_NCNN` to
   that checkout. `build-loader.sh` prepares the loader/toolchain and
   `build-ncnn.sh` applies or verifies the supported patch stack.
4. Read [NOTES.md](radish/NOTES.md) and the chosen device runner before running
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
