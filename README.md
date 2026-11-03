# Radish

Status (2026-11-03): Radish reports Vulkan 1.1 and passes the `dEQP-VK.compute`
group (93/93). The full applicable 1.1 compute CTS has not been run yet. Kokoro
TTS runs all 589 core layers on the A506, matching the CPU reference. On 4.2 s
of Japanese audio it takes 36.35 s warm, against 30.92 s on four CPU threads
(17.6% slower). Qwen3-ASR transcribes correctly on the GPU; latency is not yet
measured. Both are far from real time. Profiling is next; see
[GOAL.md](radish/GOAL.md).

## TL;DR

Radish is an experimental, compute-first Vulkan driver for Adreno 5xx, built
inside Mesa's Freedreno project. It runs Kokoro TTS (through ncnn) and
Qwen3-ASR (through transcribe.cpp/ggml) on the Adreno 506 in a Lenovo
ThinkSmart View. The goal is fast speech inference that makes full use of
attainable GPU performance.

## Purpose

Make the GPU in older Snapdragon devices usable by existing Vulkan inference
software. Radish supplies the a5xx hardware backend and the buffer-based compute
features these workloads need, then improves the kernels and submission path
against real model measurements.

The first target is the Adreno 506 / APQ8053 on Linux with the `msm` DRM driver.
Radish shares Mesa's Vulkan infrastructure and Freedreno's ir3 compiler. It is
a separate driver, with its own a5xx resource bindings and command emission.

## Source

| Component | Repository and branch |
| --- | --- |
| Driver | [`jethac/turnip-rt` `radish/main`](https://github.com/jethac/turnip-rt/tree/radish/main) (`src/freedreno/radish/`) |
| ncnn | [`jethac/ncnn` `radish/main`](https://github.com/jethac/ncnn/tree/radish/main) |
| transcribe.cpp / ggml | [`jethac/transcribe.cpp` `radish/main`](https://github.com/jethac/transcribe.cpp/tree/radish/main) |
| ASR app glue | `jethac/straylight-voice` `radish/asr` |
| TTS app and tests | this repository, `radish/tests/` |

The numbered files in `radish/patches/` are earlier checkpoints, kept for
history. Build from the branches above.

## Repository contents

The `radish/` layout is preserved from the device-development workspace so the
build and test scripts retain their relative paths.

| Path | Contents |
| --- | --- |
| [radish/GOAL.md](radish/GOAL.md) | Purpose, scope, milestones and completion criteria |
| [radish/NOTES.md](radish/NOTES.md) | Measurements, failures, current work and reproduction details |
| [radish/patches/](radish/patches/README.md) | Historical checkpoints; superseded by the branches above |
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

1. Check out `jethac/turnip-rt` branch `radish/main` and configure a cross
   build with `-Dvulkan-drivers=radish -Dgallium-drivers= -Dfreedreno-kmds=msm
   -Dbuildtype=debugoptimized` and an aarch64 cross file, then
   `ninja src/freedreno/radish/libvulkan_radish.so`.
2. Check out `jethac/ncnn` branch `radish/main` (glslang
   `780b6a209132dc74e99458f62ebe5fa7e57c41cf`) and set `RADISH_NCNN` to it.
   `build-loader.sh` prepares the loader/toolchain.
3. For ASR, check out `jethac/transcribe.cpp` `radish/main` and
   `jethac/straylight-voice` `radish/asr`, then run
   `listen/asr/build.sh aarch64 vulkan -DGGML_VULKAN_KHR_PROFILE=ON`.
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
