# Radish: goal

Radish is a compute-only Vulkan driver for the Adreno 506 in the Lenovo
ThinkSmart View. It is built inside Mesa's Freedreno tree and shares Mesa's
Vulkan runtime and the ir3 shader compiler. The goal has two halves:

1. **Conformance.** A Vulkan 1.1 driver that is conformant for compute, with
   evidence from the unmodified Khronos CTS, and with nothing advertised that
   is not implemented.
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
| GPU nominal | 124.8 GFLOP/s FP32 (assumes 96 ALU lanes × 650 MHz × 2; not a Qualcomm-verified figure). FP16 nominally 2×. |
| Memory | 2 GB LPDDR3, single channel, shared by CPU and GPU |
| Thermal | GPU trip at 80 °C; CPU capped at 1420.8 MHz from 75 °C |
| Kernel | mainline Linux, Arch Linux ARM |

Measured so far, on Radish:

- MAD-loop calibration: **21.3–22.5 GFLOP/s** FP32 (GPU timestamps). That is
  18% of nominal. Either the nominal figure is wrong or the kernel is not
  saturating the ALUs. **The real ALU ceiling is still unknown, and finding it
  is the first performance task.**
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

**Vulkan 1.1, compute.** The device exposes one queue family with
`COMPUTE | TRANSFER` and no graphics queue. The spec allows a device without a
graphics queue. Its rule ("if any queue family supports graphics, one must
support graphics and compute") only applies when graphics exists.

What 1.1 requires that matters here:

- All 1.0 core commands present and correct for every valid use reachable
  without a graphics queue. Commands that only make sense with graphics still
  exist as entrypoints.
- Required features: `robustBufferAccess`. In 1.1, also `multiview`.
- Subgroups: `supportedStages` includes compute, and `supportedOperations`
  includes `BASIC`.
- 1.1 promoted functionality: device groups (single-device), descriptor update
  templates, `vkGetDescriptorSetLayoutSupport`, `vkCmdDispatchBase`,
  maintenance1–3, external memory/fence/semaphore capability queries, the
  `*2` query entrypoints, 16-bit storage queries (the feature itself is
  optional), variable-pointer queries, protected-memory queries (feature
  optional), and SPIR-V 1.3.
- Every limit at or above the spec minimum and truthful.
- The required format support tables.

**Open question: does a compute-only device qualify?** This has to be settled
before claiming any path to conformance, because two requirements reach into
graphics:

- `multiview` is a render-pass feature that 1.1 requires.
- The required format tables include colour-attachment and blit features,
  which can only be exercised with a graphics queue.

To settle it:

1. Read the 1.1 spec's feature and format requirements line by line against a
   device with no graphics queue.
2. Run the full 1.1 mustpass and sort non-passes into three groups: genuine
   failures, valid `NotSupported`, and tests that assume a graphics queue.
3. Ask through the route Mesa drivers use for conformance. Open-source Mesa
   submissions have gone through the X.Org Foundation's Khronos membership;
   confirm the current process and any fee before relying on it.

If compute-only can't qualify, deciding whether to add a minimal graphics path
is the user's call. Freedreno's a5xx Gallium driver is the hardware reference
for that. Until then: advertise exactly what is implemented, and never invent a
compute-only profile.

Where conformance stands (2026-11-03):

- Reports API version 1.1.
- `dEQP-VK.compute`: 93/93. Core format queries: 219/219. Both are in the
  status ledger.
- Reported without a ledger entry: floating-point precision 40/40, buffer
  atomics 19/19, buffer robustness 132/132.
- Known failure: ETC2/EAC sRGB precision.
- Not yet run: the rest of the applicable mustpass (API, SPIR-V/shaders, memory
  model, synchronization, descriptors, images, transfers, queries).
- Advertised optional features: `storageBuffer8BitAccess`,
  `storageBuffer16BitAccess`, `shaderInt8`, `timelineSemaphore` and pipeline
  statistics queries. Each carries its full conditional CTS obligations.

Conformance work order:

1. Get the full CTS running on the tablet in bounded batches, in a way that
   leaves the appliance usable. Record per-group pass, fail and unsupported
   counts in one ledger.
2. Settle the compute-only question above.
3. Fix failures by group, largest first. Re-run whole groups, not single cases.
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

- The applicable Vulkan 1.1 mustpass is clean on the A506 with no graphics
  queue, or the compute-only question is settled and the agreed scope is
  clean. A submission is ready.
- Kokoro TTS and the shipped ASR model run end to end on the GPU beside the
  live services, within memory, matching CPU references.
- Both beat the best shipped CPU path on warm, sustained end-to-end latency,
  or the measured reason why that is impossible on this hardware is written
  down.
- The dominant kernels have an efficiency account against measured ceilings,
  with no known material, actionable opportunity left untried.
- Build, private install, test and rollback are scripted and repeatable from
  the branches.
