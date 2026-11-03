# Radish

## Goal

A compute-first Vulkan driver for Adreno 5xx inside Mesa's Freedreno project,
starting with the Adreno 506 in the Lenovo ThinkSmart View. Its job is to make
Straylight's speech models (Kokoro TTS through ncnn, Qwen3-ASR through
transcribe.cpp/ggml) run fast on the GPU, and to keep optimizing until the
remaining gap to the hardware's attainable limits is measured and explained.

Radish is a separate hardware driver sharing Mesa's Vulkan runtime and the ir3
compiler. It is not a GLES wrapper and not a renamed Turnip. Graphics, ray
tracing and presentation are out of scope; the CPU handles graphics, so the GPU
budget belongs entirely to compute.

## Done means

- Applicable Vulkan 1.1 compute behaviour is implemented, and the applicable
  CTS groups pass unmodified. Feature advertisement follows implementation; no
  conformance claim without CTS evidence.
- Kokoro TTS and Qwen3-ASR run end to end on the GPU, beside the live appliance
  services, within the device's memory budget, matching CPU references within
  stated tolerances.
- Warm end-to-end latency beats the four-thread CPU path for both, on sustained
  runs. Beating the CPU is a milestone, not the stopping point: the dominant
  kernels have an efficiency account against measured attainable arithmetic and
  bandwidth, and material remaining headroom is either taken or rejected with
  measurements.
- Build, install-to-private-prefix, test and rollback are scripted and
  repeatable from the branches below.

## Where the source lives

All work happens on these branches. Build from them; commit to them as you go.

| Component | Repository and branch | Local checkout |
| --- | --- | --- |
| Driver (Mesa) | `jethac/turnip-rt` `radish/main` | `/run/media/jethac/Shared/radish-src/mesa` (build dir `../build`) |
| ncnn | `jethac/ncnn` `radish/main` | `/run/media/jethac/Shared/radish-src/ncnn` |
| transcribe.cpp / ggml | `jethac/transcribe.cpp` `radish/main` | `/run/media/jethac/Shared/radish-src/transcribe.cpp` |
| ASR app glue | `jethac/straylight-voice` `radish/asr` | `/run/media/jethac/Shared/radish-src/straylight-voice` |
| TTS app, tests, notes | `jethac/radish` | this repository (`radish/tests/`) |

The branches were reconstructed on 2026-11-03 from the binaries deployed at the
time (driver `0f721436…`, ASR listener `52e7df4c…`, TTS runtime `52c9e565…`) and
verified object by object. The `0001`–`0046` files under `patches/` and the
directories under `/run/media/jethac/Shared/radish/` are history only; do not
build from them or add to them.

Working practice:

- One source tree per component. Experiments are git branches or commits, not
  copied files, hand-linked objects or edited generated sources.
- Build with the normal build system. A clean rebuild from the branch must
  produce what is deployed.
- Measure freely. Timing is how the work is steered; never stub it out.
- Keep `NOTES.md` short: current state, the next experiment, and results worth
  remembering. Evidence that matters goes in commit messages next to the change.

The `steam-frame/ac8-vr/turnip-rt` checkout and its worktrees belong to separate
Turnip work; do not use them.

## Device rules

Follow the [device-workspace rules](https://github.com/jethac/lenovo-thinksmart-view/blob/main/WORKSTREAMS.md).

- Take `.device.lock` for device changes and for measurements that need quiet;
  keep sessions short.
- Keep the live listener and UI running. Test processes run memory-capped;
  interruptions to the appliance stay under about three minutes.
- Use the private prefix and per-process ICD selection. Never replace system
  Mesa. Record files left on the device.
- Check fault, reset and hangcheck logs around GPU tests. After a fault or hang,
  stop and inspect it; don't retry the same workload in a loop. Keep dispatches
  bounded; raising the hangcheck timeout is not a fix.
- No flashing, EDL, partition writes, or boot/kernel changes under this goal.
- Keep firmware, captures, secrets and large artifacts out of public repos.
  Follow the Mesa checkout's `AGENTS.md`; nothing goes to Mesa's GitLab.

## Rules that matter

- Facts about the hardware are source-backed or measured; unknowns stay unknown
  until tested. Don't carry a6xx behaviour over to a5xx without checking.
- A kernel that is fast must also be correct: poisoned outputs, independent CPU
  references, full checks on small cases.
- Optimize what the real models spend time on. Microbenchmarks set the ceiling;
  wins count when they reach end-to-end latency.
- Report plainly: numbers, what failed, what's next.

## Current state (2026-11-03)

- Reports Vulkan 1.1. CTS: `dEQP-VK.compute` 93/93 and core format queries
  219/219 ([status](VULKAN11_COMPUTE_STATUS.md)). Floating-point precision
  40/40, buffer atomics 19/19 and buffer robustness 132/132 were reported on
  2026-11-02 but have no ledger entry. The rest of the applicable 1.1 CTS
  (shaders, memory model, images, synchronization, API) has not been run, and
  ETC2/EAC sRGB precision still fails.
- TTS, 4.2 s of Japanese audio, warm: GPU 36.35 s vs four-thread CPU 30.92 s
  (GPU 17.6% slower); first request 62.3 s GPU (44.7 s with a saved pipeline
  cache) vs 37.7 s CPU. All 589 layers on the GPU; outputs match the reference.
  That run used robustBufferAccess on; an earlier build with it off was 32.8 s
  warm vs 37.3 s CPU.
- ASR: two requests of a 2.8 s English clip transcribe correctly on the GPU,
  with KV-cache growth and pipeline-cache reuse working. Its timings so far come
  from a debug build of `straylight-listen` and from builds with timing stubbed
  out, so there is no usable ASR latency number yet.
- Both are far from real time.

## Next

1. Profile the warm GPU TTS run by phase and by layer: GPU execution, submission
   and waits, host work, and CPU frontend. Find where the 36 s goes.
2. Measure robustBufferAccess off against on, and the cheaper vector bounds
   checks, on the same build.
3. Release build of the ASR app; first real ASR latency against the CPU path.
4. Work down the largest measured costs, re-profiling after each change.
