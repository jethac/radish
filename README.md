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
contains 127,800 samples and passes at **98.38 dB SNR**. No GPU faults occurred,
and both live appliance services remained active.

| Measurement | GPU path | Packed six-thread CPU |
| --- | ---: | ---: |
| Fixed full-size core, warm extraction | **40.724 s** | **32.079 s** |
| Fresh full-length synthesis, including startup | **57.293 s** | **39.790 s** |

GPU inference remains slower than CPU. Enabling the existing normalization
optimization reduces warm core time from 42.470 s to 40.724 s, with identical
output bytes. Fresh full synthesis improves from 63.130 s to 57.293 s, with a
41.411 s GPU core. Frontend startup also falls from 10.327 s to 5.889 s, so the
whole improvement cannot be attributed to normalization. All eight boundary
inputs, core output and waveform match the preceding validated run exactly.

The latest fixed core peaks at **385.1 MiB with 71.7 MiB swap**; fresh full
synthesis uses **418.9 MiB with 12.6 MiB swap**. The earlier norm-disabled short
run takes **30.157 s**, with a **15.874 s** GPU core, 385.0 MiB and zero swap.
These are single warm/startup observations with varying memory pressure; the
CPU warm mean has two samples and uses the earlier InstanceNorm graph. Resident
warm synthesis now passes at both lengths; sustained thermal behavior remains
unmeasured.

A bounded resident worker now regenerates frontend/source and waveform for every
request while retaining one loaded core. Short synthesis falls from **29.765 s
to 20.056 s** on reuse; full synthesis falls from **57.301 s to 46.984 s**.
Shape changes reload the core, and all five tablet requests pass complete
reference checks with identical prior output bytes. The short/full/full session
peaks at **517 MiB with 19.1 MiB swap**, with no faults and both live services
active. These are single resident observations; a matching resident CPU
comparison, sustained behavior and live application integration remain pending.

Select the measured graph with `--affine-snake-deconv2d`,
`RADISH_NCNN_PLANAR=1` and `RADISH_BATCH_CHUNKS=8`, with one X workgroup.
The latest fixed core and fresh full synthesis additionally enable
`RADISH_NCNN_NORM_SUM4=1`; the earlier short result uses it disabled. The exact
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

## License

Original Radish code and tools are [MIT licensed](LICENSE). Mesa and ncnn
notices and applicable upstream terms are preserved; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
