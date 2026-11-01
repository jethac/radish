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
| Fixed full-size core, warm extraction | **43.124 s** | **32.079 s** |
| Fresh full-length synthesis, including startup | **59.185 s** | **39.790 s** |

The GPU figures use the opt-in FP32 AffineSnake graph, planar convolution input,
output reshape aliases and eight sequential chunks per submission for eligible
short shaders and tiled convolution. Select `--affine-snake`,
`RADISH_NCNN_PLANAR=1` and `RADISH_BATCH_CHUNKS=8`; keep norm fusion off and
one X workgroup. The fixture requires recorded full CPU validation before
GPU use. Defaults remain unchanged.

Warm core timing is one extraction, versus two for the CPU mean; the CPU uses
the preceding InstanceNorm graph. Complete synthesis figures are single startup
runs with byte-identical frontend boundary inputs. The GPU core peaks at
**416.0 MiB with 7.4 MiB swap**; full synthesis peaks at **415.6 MiB with
74.2 MiB swap**. Memory pressure varies between runs. No new GPU faults occurred,
and both appliance services remained active. Complete core and waveform bytes
match the preceding GPU implementation. GPU inference remains slower than CPU;
resident warm synthesis and sustained thermal behavior remain unmeasured.

The latest changes simplify convolution address calculations and combine the
three style operations with each Snake activation. The indexing change saves
0.374 s in the affected math commands; AffineSnake replaces 192 commands with
48 and saves 1.822 s in the affected work. Warm extraction moves from 45.340 s
to 44.956 s to 43.124 s across these single-pair measurements. The final graph
has 714 layers and retains every model weight and the original FP32 arithmetic
order. Source and evidence are in ncnn patch 0030, cumulative Mesa patch 0031,
and the AffineSnake preparation/validation scripts.

Earlier submission batching removes 10,894 submissions without changing shader
work, and planar input avoids repeated convolution address reconstruction.
Together with normalization, reshape and activation changes, these bring warm
GPU time down from the initial 101.275 s. Detailed measurements and attribution
are in [NOTES.md](radish/NOTES.md).

Fresh short synthesis takes **32.105 s**, including startup, and passes every
core/audio comparison at **330.3 MiB, zero swap**. Its core takes **16.578 s**;
startup still makes the total slightly slower than the earlier 31.891 s sample.
InstanceNorm remains the default graph; Snake and AffineSnake are explicit
project-local ncnn variants. Preparing and selecting them is documented in
[RUNTIME_INTEGRATION.md](radish/RUNTIME_INTEGRATION.md).

Optional X workgroup pairing and norm-sum4 fusion have correctness evidence but
no reliable overall performance win; both remain off in the figures above.
Mixed FP16 storage still fails full-model and waveform accuracy gates, so FP32
remains the validated path.

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
   `radish/patches/0031-radish-planar-index-batching-cumulative.patch` to that base.
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
