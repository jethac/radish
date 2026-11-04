# Radish: goal

Radish is a compute-first Vulkan driver for the Adreno 506 in the Lenovo
ThinkSmart View. It is built inside Mesa's Freedreno tree and shares Mesa's
Vulkan runtime and the ir3 shader compiler. The goal has two halves:

1. **Conformance.** Full Vulkan 1.1 conformance: the unmodified Khronos CTS
   mustpass (vulkan-cts-1.4.7.1) passed without waivers, and a
   submission-ready result. Graphics is implemented only as far as
   conformance needs; at run time the GPU budget is still compute. Nothing
   is advertised that does not pass.
2. **Performance.** Tuned to get as close as the hardware allows to the
   APQ8053's real limits, on the workloads the appliance actually runs: speech
   synthesis and recognition for Straylight.

Neither half is done until both are. A conformant but slow driver is a
milestone. So is a fast one that cuts corners on the spec. Beating the CPU is
also a milestone, not the finish line: the finish line is a measured account of
where the remaining gap to the hardware goes, with every material opportunity
taken or ruled out by measurement.

## Why

Mesa's Vulkan driver for Adreno, Turnip, starts at a6xx, and upstream has no
plans for a5xx. Freedreno covers a5xx for OpenGL ES only. Without Vulkan,
existing inference engines (ncnn, ggml/llama.cpp-family, sherpa-ncnn) cannot
use this GPU. Earlier GLES compute work topped out around 14–17 GFLOP/s on
GEMM, slower than the CPU for real models. Vulkan gives explicit control over
memory, synchronization and submission, and lets existing engines run
unmodified. It is a way in, not a promise of more FLOPs.

## The hardware

| | |
| --- | --- |
| SoC | Qualcomm APQ8053-Lite, marketed as Snapdragon 624 (MSM8953 family, no modem). Same CPU and GPU as the Pro part; the Lite codec is limited to 1080p. |
| CPU | 8 × Cortex-A53 at up to 1.8048 GHz, in-order; ~57.8 GFLOP/s FP32 nominal |
| GPU | Adreno 506, a5xx generation, up to 650 MHz; mainline `msm` DRM, no KGSL |
| GPU ALU | Measured: about 15 lane-instructions per clock (16 FP32 lanes at ~93%), so **19.4 GFLOP/s FP32 MAD** at 650 MHz, ~20.8 theoretical. FP16 (`mad.f16`) issues at the same rate: no 2× FP16. Mesa lists the A505–A509 with one SP (`num_sp_cores = 1`). The once-quoted 124.8 GFLOP/s (96 lanes) is not supported by measurement, nor is 83 GFLOP/s (64 lanes per SP as on the A530). |
| Memory | 2 GB LPDDR3, single channel, shared by CPU and GPU |
| Thermal | GPU trip at 80 °C; CPU capped at 1420.8 MHz from 75 °C |
| Kernel | mainline Linux, Arch Linux ARM |

Measured so far, on Radish:

- ALU ceiling (`radish/tests/ubench`, 2026-11-03, GPU at 650 MHz, services
  running): **19.4 GFLOP/s FP32 MAD**, register operands, vec4 × 12
  independent chains, 128 threads. Loop body ISA: 384 `mad.f32` (128 with
  `(rpt1)`), 6 nop cycles, 11 other instructions, no `(ss)`. The rate is
  the same for 1 to 256 workgroups, 4 to 16 full registers (8 to 16 max
  waves), single or double thread size, scalar or vec4, and FP16. A
  2-source `mul.f` loop issues at the same instruction rate as `mad.f32`
  (9.3 vs 9.7 G lane-ops/s), so the limit is issue width, not register
  reads. MAD with two const-file operands: 12–13 GFLOP/s. The earlier
  21–22 GFLOP/s "mul + add" calibration compiled to `mad.f32` too
  (`precise` did not stop fusion).
- Bandwidth: SSBO vec4 read 2.9 GB/s (cache-resident and 4 MiB), 1.9 GB/s
  at 32 MiB; texel buffer RGBA32F 4.9 GB/s, RGBA16F 6.3 GB/s; UBO dynamic
  index 2.9 GB/s; SSBO copy 3.0 GB/s read+write.
- vec4 copy: **4.3 GB/s** logical read+write.
- Earlier GLES work: GEMM 14.4 GFLOP/s in compute, **16.7 GFLOP/s in FP16
  fragment shaders over RGBA16F textures**. That suggests the texture path
  (TPL1 cache) feeds the ALUs better than SSBO loads. Worth exploiting from
  compute through images and samplers.
- Kokoro's convolutions run at 4–7 GFLOP/s.
- A GPU GEMM slows concurrent CPU decoding by 18–20%: the memory bus is
  shared, and that interaction counts in end-to-end numbers.

Known hardware and compiler constraints (keep validating; don't assume):

- **Register file.** Full and half registers share one file, and occupancy
  falls as a shader's footprint grows. ir3 under-counted half registers on a5xx
  (fixed locally); Radish rejects pipelines over the measured budget.
- **Shared memory.** 16 KiB validated. 32 KiB faults, so the limit is 16 KiB
  until the larger-address fault is understood.
- **Workgroups.** 128 invocations max today. One ncnn reduction hangs at 64
  threads and passes at 32 with an identical binary. That is an unresolved
  multi-wave bug, not a closed question.
- **Descriptors.** a5xx is bindful: no bindless descriptor heap. Descriptor
  indexing and Vulkan 1.2 are a large architectural step, which is part of why
  1.1 is the target.
- **Hangcheck.** Long submissions are killed. Dispatches must be bounded and
  split; raising the timeout is not a fix.
- **FP16.** Native half ALU exists, but no throughput win yet: conversions,
  scheduling and register pressure have eaten it.
- **Submission.** Every submit is a kernel ioctl plus a fence wait. Batching
  already cut Kokoro's submits from 26,292 to 15,398 per run and helped.

## Conformance target

**Full Vulkan 1.1, graphics included only as far as conformance needs.**
The compute-only route is closed (see the [ledger](radish/VULKAN11_CTS_LEDGER.md)):
`VK_VERSION_1_1` depends on the graphics layer in `vk.xml`, `multiview` is
mandatory, the required format tables include colour-attachment, blend and
blit features, and CTS 1.4.7.1 has no compute-only mode
(`--deqp-compute-only` survives only in `external/vulkancts/README.md`;
`framework/common/tcuCommandLine.cpp` has no such option). So Radish gains a
graphics path:

- One queue family with `GRAPHICS | COMPUTE | TRANSFER`.
- Render passes and framebuffers (GMEM or bypass, multiview), graphics
  pipelines with ir3 vertex and fragment shaders (geometry and tessellation
  stay off: optional in 1.1), vertex input, rasterisation, depth/stencil,
  blending, clears, blits and resolves, and the required colour-attachment
  and blit format features.
- The hardware reference is Freedreno's a5xx Gallium driver
  (`src/gallium/drivers/freedreno/a5xx/`) and the a5xx register XML;
  Turnip's structure where it fits.
- Graphics is for conformance, not the screen: the UI keeps its own
  renderer, and graphics must not regress compute performance (Kokoro and
  the students' GPU RTF are measured before and after).

The compute bugs found by the full run are part of the same target:
workgroups over 128 invocations (the multi-wave barrier bug), border colours
in compute, small mipmapped 3D images, UBO/SSBO layouts and timeline
semaphores.

Where conformance stands (2026-11-04, [ledger](radish/VULKAN11_CTS_LEDGER.md)):

- Reports API version 1.1. The full CTS 1.4.7.1 mustpass has run on the
  tablet: 79,647 Pass, 127,014 Fail, 3,008,449 NotSupported, 7 Crash.
- Of the failures, 121,120 need a graphics queue or pipeline, 233 come from
  CTS queue-family assumptions, and 5,672 are compute-path driver work
  (mostly workgroups over 128 invocations and border colours in compute).
- The compute-only question is answered: as the 1.1 spec and the CTS
  stand, a device without graphics cannot pass (multiview and the graphics
  format features are required; the CTS compute-only mode was removed).
  Whether to add a minimal graphics path, or to ask the working group
  through the X.Org Foundation, is the user's decision.
- Advertised optional features: `storageBuffer8BitAccess`,
  `storageBuffer16BitAccess`, `shaderInt8`, `timelineSemaphore`, pipeline
  statistics queries, `VK_KHR_sampler_mirror_clamp_to_edge`.

Conformance work order:

1. Get the full CTS running on the tablet in bounded batches, in a way that
   leaves the appliance usable. Record per-group pass, fail and unsupported
   counts in one ledger.
2. Settle the compute-only question (done: it cannot qualify).
3. Add the graphics path and fix failures by group, largest first. Re-run
   whole groups, not single cases.
4. Formal submission once the mustpass is clean.

## Performance target

The workloads are whatever Straylight ships on the device. Today:

| Workload | Engine | Hot paths | Best CPU today |
| --- | --- | --- | --- |
| Japanese TTS: Kokoro-82M decoder | ncnn | Conv1D (dilated, packed), ConvTranspose, InstanceNorm, Snake/AffineSnake, iSTFT tail on CPU | ONNX Runtime on the A53s: about 3× slower than real time |
| ASR: Qwen3-ASR | transcribe.cpp / ggml | quantized Q4_K/Q6_K/Q8_0 matmul and matvec, GQA attention, RMSNorm, softmax, KV cache | CPU: 4.1–6.1 s after speech |
| ASR, fast path: SenseVoice-Small + streaming zipformer | sherpa-onnx on CPU | CTC encoder, streaming transducer | 1.1–1.75 s after speech |

The baseline Radish must beat is **the best CPU implementation the appliance
would otherwise ship**. That is not the same engine's CPU backend. The current
Radish comparison is ncnn four-thread CPU: 30.92 s for 4.2 s of Japanese audio,
against 36.35 s on the GPU warm. The production CPU Kokoro path is about three
times faster than that ncnn CPU number. Report both, and judge against the
stronger one.

The product targets behind the numbers:

- Japanese TTS continuous in real time (RTF ≤ 1) on the device.
- Recognition under about 1 s after speech.
- Both models fitting beside the system: the listener peaks around 1.36 GiB,
  with about 550 MiB free and 768 MiB of zram swap.

If the GPU can't hit those alone, it can still pay by taking work off the CPU
so the two run concurrently. sherpa-ncnn's Vulkan path for zipformer is a
candidate way to bring the fast ASR models onto the GPU.

How performance work proceeds:

1. **Find the ceiling.** Microbenchmarks for FP32 and FP16 ALU throughput:
   independent MAD chains, vec4, enough waves to hide latency, a register
   footprint sweep. Bandwidth via SSBO, UBO/constant and image/texture paths.
   Per-submit and per-dispatch overhead. Do this under the appliance's real
   clocks and thermal state, with the live services running.
2. **Profile the real runs.** Per layer and per phase: GPU execution,
   submission and waits, host-side encode, CPU frontend and tail, transfers.
   GPU timestamps for kernels, wall-clock end to end.
3. **Efficiency account.** For the dominant kernels, compare useful FLOPs and
   bytes against the measured ceilings. The gap tells you whether a kernel is
   ALU-bound, bandwidth-bound, occupancy-bound or overhead-bound.
4. **Work the biggest measured loss first.** Re-profile after every change.

Levers specific to this SoC, roughly by expected value:

- Kernel shapes that fit the register budget at high occupancy: tiling, vector
  width, accumulator count. Check the ISA and register counts for every
  candidate.
- Weight and activation reads through the texture path (images, TPL1) and
  constants and UBOs, instead of plain SSBO loads. The GLES fragment result
  says this matters.
- FP16 arithmetic and storage where accuracy allows, with conversions taken
  out of hot loops. Validate per operator, then at model level.
- Fusion (norm, activation, bias) to cut memory round trips on a shared,
  narrow bus.
- Fewer, larger submissions: batching, command reuse, fewer fences. Keep each
  submission under the hangcheck limit.
- Compiler work in ir3 when the ISA shows waste: scheduling, moves, half/full
  conversions, loop overhead.
- Overlap of CPU work (frontend, tail, decode) with GPU work, and attention to
  memory contention between the two.
- Robustness costs: an application may turn `robustBufferAccess` off. Make the
  on path cheap too, e.g. one bounds check per vector, not per scalar.

Rules for performance claims:

- Same input, same accuracy checks, same thread counts, warm and cold reported
  separately, peak memory and swap reported, thermal state noted.
- Correctness first: poisoned outputs, full checks on small shapes, an
  independent CPU reference. A fast wrong kernel is a bug.
- Microbenchmark wins count only when they reach end-to-end latency.
- Rejected experiments are recorded in one line with the number, and reopened
  only for a new reason.

## Source and build

| Component | Repository and branch | Local checkout |
| --- | --- | --- |
| Driver (Mesa) | `jethac/turnip-rt` `radish/main` | `/run/media/jethac/Shared/radish-src/mesa`, build dir `../build` |
| ncnn | `jethac/ncnn` `radish/main` | `/run/media/jethac/Shared/radish-src/ncnn` |
| transcribe.cpp / ggml | `jethac/transcribe.cpp` `radish/main` | `/run/media/jethac/Shared/radish-src/transcribe.cpp` |
| ASR app glue | `jethac/straylight-voice` `radish/asr` | `/run/media/jethac/Shared/radish-src/straylight-voice` |
| TTS runtime, tests, notes | `jethac/radish` | `/home/jethac/Projects/radish` |

Driver build: cross-compiled with meson from
`streams/gpu-mesa16/work/venv`, cross file
`/run/media/jethac/Shared/radish/cross-aarch64.ini`, with
`-Dvulkan-drivers=radish -Dgallium-drivers= -Dfreedreno-kmds=msm
-Dbuildtype=debugoptimized`. On the tablet it loads through a private ICD
manifest and a private prefix; system Mesa is never replaced.

Working practice:

- One source tree per component. Experiments are branches or commits.
- Normal build systems only. A clean rebuild from the branch is what gets
  deployed.
- Commit as you go, push regularly.
- `radish/NOTES.md` holds the current state and the next experiment.
- The `/run/media/jethac/Shared/radish/` scratch directories and the numbered
  `radish/patches/` files are history. Don't build from them.
- The `steam-frame/ac8-vr/turnip-rt` checkout belongs to separate Turnip work.
  Don't touch it.
- Mesa's `AGENTS.md` applies: nothing goes to Mesa's GitLab.

## Device rules

- Take `.device.lock` (`/home/jethac/Projects/lenovo-8/.device.lock`) for
  device changes and quiet measurements; keep sessions short.
- The live listener and the Straylight UI stay running. Test processes are
  memory-capped, and appliance interruptions stay under about three minutes.
- Check GPU fault, reset and hangcheck logs before and after GPU work. After a
  fault, stop and inspect it; don't loop on a faulting workload.
- Record every file left on the device.
- No flashing, EDL, partition writes, or boot/kernel changes under this goal.
- Firmware, captures, model weights and secrets stay out of public repos.

## Done

- The Vulkan 1.1 mustpass (CTS 1.4.7.1, unmodified, no waivers) is clean on
  the A506. A submission is ready.
- Kokoro TTS and the shipped ASR model run end to end on the GPU beside the
  live services, within memory, matching CPU references.
- Both beat the best shipped CPU path on warm, sustained end-to-end latency,
  or the measured reason why that is impossible on this hardware is written
  down.
- The dominant kernels have an efficiency account against measured ceilings,
  with no known material, actionable opportunity left untried.
- Build, private install, test and rollback are scripted and repeatable from
  the branches.
