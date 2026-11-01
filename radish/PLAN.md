# Radish Implementation Plan

Goal: establish an isolated native A506 Vulkan compute driver and prove a first
correct SPIR-V dispatch, then implement the backend and model path. Implements
`radish/GOAL.md`; M3–M6 remain required. Later tasks record the detailed follow-on
work as hardware tests resolve the initial ABI and its limitations.

Architecture: separate `src/freedreno/radish/` driver in Mesa, using its Vulkan
runtime, Freedreno DRM BO/submission library and ir3. Start with serial compute,
fixed SSBO bindings and explicit per-process ICD loading. No Gallium wrapper.

Technology: C, Meson/Ninja, SPIR-V/NIR/ir3, libdrm/msm, arm64 cross-compilation.
Execution: inline, as authorized by “execute radish/GOAL.md”. No additional plan
approval is needed. Source documentation stays here; honor the Mesa checkout's
restriction on generated prose/comments inside that checkout.

## Global constraints

- Preserve the AC8 checkout and baseline tablet services/system Mesa.
- Code worktree: `/run/media/jethac/Shared/radish/mesa`, branch `radish/main`.
- Base: `ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230` of `jethac/turnip-rt`.
- Builds, dependency checkouts and large evidence stay on the shared disk.
- No feature advertisement without implementation; experimental API version
  negotiation does not constitute Vulkan conformance.
- Device changes and measurements use `.device.lock`; bounded memory and time.

## Review focus

- Wrong hardware must enumerate zero devices, not receive A506 packets.
- Unsupported features/extensions must fail cleanly, not be silently enabled.
- Failed allocations and partial initialization must release owned resources.
- Mapping offsets and bound buffer ranges must not escape the BO allocation.
- Completion must establish observable output, not merely a successful submit.

## Task 1: evidence, target and baseline

Files: `radish/NOTES.md`, `radish/CAPABILITIES.md`.

- [x] Inspect current state, local instructions, Mesa tree and device availability.
- [x] Create dedicated worktree at pinned base, preserving AC8 branches.
- [x] Audit ncnn and local ggml requirements; select ncnn's FP32 buffer path first.
- [x] Configure unmodified cross build and run relevant existing baseline checks.
- [x] Record exact revisions, commands, dependency gaps and hardware unknowns.

## Task 2: instance, device and memory

Files: Mesa `meson.options`, root `meson.build`, `src/meson.build`,
`src/freedreno/meson.build`, `src/freedreno/radish/{meson.build,rd_private.h,
rd_instance.c,rd_device.c,rd_memory.c}`; external harness
`radish/tests/lifecycle.c` and `radish/scripts/build.sh`.

Interfaces: ICD `rd_GetInstanceProcAddr`, generated `rd_*` Vulkan entrypoints;
private objects embed Mesa `vk_*` bases and own Freedreno device/pipe/BO state.

- [x] Write loader-independent Vulkan lifecycle harness: absent ICD fails;
  create/enumerate/properties/device/queue/allocation/buffer/map cycles must pass.
  Include unsupported-extension rejection and nonzero mapping offsets.
- [x] Run and record initial failure before driver implementation.
- [x] Integrate opt-in `-Dvulkan-drivers=radish`, generated dispatch tables,
  experimental ICD, A506-only DRM discovery and conservative capability queries.
- [x] Implement device/queue ownership and BO-backed allocation/buffer mapping.
- [x] Cross-build and run harness on tablet under lock with both services up.
  Check no new faults and verify unsupported hardware remains excluded.

## Task 3: first real compute

Files: Mesa `src/freedreno/radish/{rd_shader.c,rd_pipeline.c,rd_descriptor.c,
rd_command.c,rd_queue.c}` and updates to private header/build;
`radish/tests/compute.c`, `radish/tests/shaders/{index,vector-add}.comp`.

Interfaces: pipeline owns compiled ir3 variant, shader BO and binding map;
command buffers own dispatch snapshots; queue consumes snapshots and signals
completion only after successful GPU execution.

- [x] Add failing tests for index patterns and FP32 vector addition over sizes
  1, 31, 32, 33, 127, 128, 129 and 4099, repeated with changed inputs/sentinels.
- [x] Define SSBO/push-constant/builtin ABI from ir3 and fd5 compute source.
- [x] Implement SPIR-V/NIR lowering and fixed binding map, pipeline creation,
  descriptor sets, command recording and native a5xx command emission.
- [x] Implement conservative cache dependencies and serial DRM completion.
- [x] Verify full outputs, guard regions and repeated submissions on A506;
  reject timed-out/reset submissions and inspect logs before further testing.
- [x] Run relevant existing ir3 tests and new lifecycle/compute tests; record
  limitations and update next tasks toward ncnn integration, never mark the
  overall goal complete at first dispatch.

## Task 4: ncnn initialization and buffer transfers (M3)

Files: `radish/tests/ncnn/{CMakeLists.txt,main.cpp}`, build/device scripts;
Mesa `rd_instance.c`, `rd_memory.c`, `rd_command.c`, `rd_queue.c`, private header.

Interfaces: ncnn uses the normal private Vulkan loader; all operator tests use
Vulkan-only layer entrypoints and require recorded dispatches, so CPU fallback
cannot make a missing GPU implementation pass.

- [x] Cross-build pinned ncnn/glslang and observe initialization/operator failure.
- [x] Add image/format queries that honestly return unsupported for images;
  record and test any additional backend-required capability queries.
- [x] Test buffer fill/copy/update with nonzero offsets, sentinels and mixed
  transfer/dispatch ordering; observe missing-function failures first.
- [x] Record typed commands in original order; use A5XX CP memory operations,
  existing BO tracking and bounded completion, with explicit cache dependencies.
- [x] Run ncnn initialization, then Vulkan ReLU through upload/compute/download
  on three sizes; compare every output against independent arithmetic.
- [x] Keep all optional feature flags conservative; run the M2 regression suite.

## Task 5: dependent compute and real model requirements (M3–M5)

Files: new tests/shaders and any required compiler/command changes; record exact
model conversion, tensor shapes, operator coverage and CPU reference evidence.

- [ ] Test several dependent dispatches in one command buffer and across submits,
  including barriers, reuse, host visibility and bounded failure behavior.
- [x] Verify specialization constants and add shared-memory/barrier lowering and
  a reduction test before enabling kernels that require them.
- [x] Select and convert a real existing speech model; inventory all operations
  and explicitly track CPU fallback. Operator-only tests do not satisfy M5.
- [x] Compare the fixed neural-core outputs and cgroup peak working set beside
  live services; saved-source waveform check passes. Live integration remains.
- [ ] Profile actual inference, remove unnecessary per-dispatch waits using tested
  dependencies, and compare warm/cold latency to the best applicable baseline.

## Task 6: bounded hardware dispatches and logical grid ABI (M3/M5)

Files: Mesa `rd_pipeline.c`, `rd_queue.c`, private ABI if needed;
new 3D grid shader/regression modes; `radish/ABI.md` and evidence ledger.

The ncnn mean test is RED on hardware even after a valid 64-thread pipeline:
width1/channels3 faults with a long program and three workgroups. Fault captured,
GPU testing stopped. Separately, the real Kokoro graph has large convolutions
that must be split before attempting GPU model execution under the hangcheck.

Ruling: implement bounded subdivision as required for real model execution,
then use a single workgroup per hardware submission for barrier kernels as a
controlled isolation of concurrent-group behavior. This is a conservative
correctness policy, not a confirmed diagnosis or performance optimization.
Cost if wrong: extra submission overhead without fixing the reduction fault;
the model and performance goals remain unchanged and incomplete.

- [x] Add a 3D grid shader checking original NumWorkgroups, logical WorkgroupId,
  GlobalInvocationId, partial output bounds, group uniqueness and guard regions;
  independent CPU reference. Keep existing large-X vector and reduction checks.
- [x] Define base-group uploads and lowering explicitly. Logical WorkgroupId
  and GlobalInvocationId must include the chunk base exactly once; NumWorkgroups
  must retain the original API grid, not the current hardware chunk.
- [x] Split hardware grid work into bounded chunks with checked arithmetic;
  keep zero-size dispatch semantics and operation order. Begin with one group
  per submission for barrier kernels; retain bounded completion/fault inspection.
- [x] Compile/inspect resource usage before execution, run the 3D/large-X/shared
  regressions, then make one controlled reduction attempt after inspection.
  If it faults, stop and inspect rather than treating subdivision as a fix.
- [x] Cover device-lost detection: a signaled DRM fence after recovery must not
  be accepted as proof of completed output. Audit per-context fault counters
  and record any untested error paths without deliberately looping GPU faults.
- [x] Re-run loader, transfer/coherent/descriptor regressions affected by the ABI;
  update source checkpoint, build/deployment instructions and remaining gaps.


## Task 7: isolate ncnn reduction control flow (M3/M4)

The bounded one-workgroup attempt still hangs; DEVICE_LOST now reaches ncnn.
Captured shader bytes/hash, push constants and descriptors match the build.
A standalone long shared reduction passes the unpreloaded instruction path.
Do not retry the unchanged ncnn workload or claim its root cause is known.

- [x] Compile short/long uint xorshift plus shared tree variants; verify the long
  variant exceeds preload capacity and both fit the observed register budget.
- [x] Run one-group smoke checks and full boundary/reference checks after fault
  inspection; preserve results and the failed ncnn dump outside the repository.
- [x] Add bounded tests separating divergent input loops, indirect temporary
  array accesses and integer division; compare every output and inspect ISA.
- [x] Validate the backend 32-thread reduction path against all 18 independent
  means; preserve the physical64 failure and reproduce its captured shader hash.
- [ ] Apply only a source/evidence-supported compiler or ABI correction, then
  rerun the actual ncnn means (including scalar conversion for packed uploads).
- [x] Resume backend operators and fixed Kokoro core execution with a memory
  budget, actual fallback trace and full output checks; Task 9 retains live
  end-to-end integration and timing.


## Task 8: convolution and style projection (M4/M5)

The ncnn reduction has a validated 32-thread backend path; physical64 remains
an unresolved driver/compiler issue even with the identical passing binary.
Do not treat the backend workaround as broader workgroup conformance.

- [x] Add Vulkan-only Conv1D and InnerProduct probes with deterministic weights,
  independent double references, full logical output checks and explicit
  packing handling. Include K3/K7/K11 and the model's 128→1028 projection shape.
- [x] Validate all allocated child/operator pipeline handles before submission;
  require operator recording to add GPU dispatches beyond uploads/downloads.
- [x] Inspect compiled local sizes, registers, spills and barriers before GPU
  execution; first small cases, then all bounded operator shapes.
- [x] Cover this fixed graph's missing layout operations with guarded equivalent
  Reshape adaptation; preserve bit-identical CPU output and driver gaps.
- [x] Measure available device memory and prepare capped full-core execution
  with explicit fallback tracing and output accuracy; end-to-end timing remains.

## Task 9: real graph execution and memory accounting (M5)

The five FP32 operator cases pass, including the model's first projection shape.
The full neural-core graph has 1368 layers and 213042160 weight bytes. Start with
its exact first style projection to exercise Net packing/graph execution using
original data, then load and execute the full corrected core. Subgraph success
does not replace full-core, waveform, memory or performance requirements.

- [x] Extract the original Crop→InnerProduct path, original weight prefix and
  style input; compare an independent double reference to the saved CPU blob.
- [x] Check available tablet memory with both services active: 995164 KiB at
  this preflight. Recheck before each run; use 384 MiB style / 640 MiB core
  process caps plus at least 192 MiB available reserve, with bounded sessions.
- [x] Add opt-in actual backend/transfer/submit tracing; distinguish CPU fallback,
  Vulkan calls without dispatches, and recorded compute work.
- [x] Preserve CPU reference behavior; compile and inspect style graph kernels,
  then run the exact graph on the GPU and compare every output.
- [x] Load the full corrected core under the memory cap, inspecting compiler
  failures before inference. Account for mappable weight uploads and CPU copies.
- [x] Execute the full core with fallback tracing, full reference comparison and
  peak-memory observations: 562342 outputs pass, 1360 GPU calls / 3 CPU
  deconvolutions, 389.6 MiB peak with zero swap and both services active.
- [x] Validate the saved harmonic-source fixture and CPU waveform tail against
  all 127800 reference samples (97.91 dB SNR). This is fixture correctness.
- [x] Port the exact waveform tail to a standalone CPU executable and validate
  all original samples on device: 0.105977 s, 5.5 MiB peak, zero swap.
- [ ] Integrate runtime harmonic generation and waveform output, then compare
  warm/cold end-to-end CPU/GLES baselines per GOAL.md. Continue profiling the
  remaining extraction/transfer path, including submissions and CPU fallback;
  record throughput, memory and sustained thermal behavior before M6 claims.
  See [RUNTIME_INTEGRATION.md](RUNTIME_INTEGRATION.md) for the verified host source
  extraction, dynamic-shape blockers and shared CPU/GPU input validation.

## Task 10: applicable CTS coverage (M3)

The existing GLES CTS source assumes a graphics+compute default queue. Current
upstream CTS accepts compute-only default queues. Use a separate pinned checkout
and build on shared scratch; preserve the existing GLES build and all sysroots.

- [x] Build headless deqp-vk at `5a9cb32498158eb60774cf8a3d4e0b8c68f7fe9e`
  and pin dependencies; artifact hash and manifests recorded in NOTES.md.
- [x] Select bounded API/resource/compute cases from its actual case inventory;
  record expected unsupported cases and known API gaps before execution.
- [ ] Run private, memory-capped locked sessions and inspect faults between runs.
  Record failures and NotSupported separately from passes. No CTS case has
  executed on the device; no conformance claim.

## Task 11: measured model performance (M6)

The fixed neural core and device waveform tail pass. GPU extraction falls from
193.390 s to 103.192 s first / 101.275 s warm for 5.325 s of audio, with
identical output. The same fixture's packed six-thread CPU warm mean is 32.052 s.
The GPU warm result contains one iteration after warmup; GPU performance remains
behind, and integrated end-to-end timing is still required.

- [x] Profile actual command wall time and hardware submission counts while
  preserving output checks. The repeated run passes in 193.390 s, with 349033
  submissions and 178.784 s inside recorded commands. These include GPU execution
  and host overhead; they are not isolated GPU or fence timings.
- [x] Locate existing ncnn shape metadata and its packing/local-size rules.
  Unknown BinaryOp shapes select 4×4×4; truthful rank-2 hints select up to 8×8×1.
- [x] Capture scalar CPU blob dimensions without retaining all tensor storage;
  annotate a guarded copy of the fixed graph, prove CPU output identity, and
  compare full GPU output and timing with batching disabled.
- [x] Evaluate bounded batching only for short, loop-free, non-barrier shaders.
  Preserve original chunks and cache/wait packets; retain single-chunk behavior
  for long shaders and barriers. Run existing grid/dependency checks and the
  full model; compare actual submissions and output, not just speed.
- [x] Propagate Conv1D helper shape hints and enable its three CPU fallbacks to
  use the requested threads in a separate SIMPLEOMP build. Retain full output
  and waveform gates; record memory/swap and avoid timing attribution without
  an isolated comparison.
- [x] Replace FP32 GEMM inner-loop division/remainder with equivalent induction;
  inspect generated code and reuse operator/full-model comparisons.
- [x] Measure packed CPU on the same fixture with six SIMPLEOMP threads and
  repeated validated extractions; record warmup, warm mean and peak memory.
- [x] Validate opt-in shared32 group budgets through eight groups per hardware
  dispatch: existing shared outputs/guards and all 18 ncnn means pass. Full core
  passes two extractions, bit-identical, 37358 submissions each; warm 101.275 s.
- [x] Compile and run bounded packed-FP16 conversion/scatter/ReLU probes with
  FP32 arithmetic, changing inputs, subnormal unpack and untouched neighboring
  half-value checks. All outputs/guards pass; no new optional features advertised.
- [x] Build and inspect optional FP32 shared32 convolution (0013); replace the
  unsupported `gl_WorkGroupSize` read with specialization ID15 and pass existing
  K3/K7/K11 operators at shared group budgets 1 and 8 with no faults.
- [x] Validate shared32 full-model output and timing: bit-identical output, but
  198.453 s versus 101.275 s warm on the existing path; reject for performance
  and leave disabled by default. Conv command time rises to 162.176 s.
- [x] Build packed-FP16 helper modes in regular/SIMPLEOMP variants and expose
  wrapper flags with separate evidence/candidate/output files. Inspect actual
  operator resources and pass all five Conv1D/InnerProduct fixtures with
  predeclared storage-rounding bounds and no new GPU faults.
- [x] Compile packed local32 means and pass all 18 existing cases with shared
  group budget 8 and no faults; compare all 84 scalar outputs with declared
  one-store rounding bounds.
- [x] Run the packed full model with unchanged accuracy gates and preserve its
  failed candidate: all 562342 outputs are NaN, exit 1, no new GPU faults.
  The 107.515 s failed extraction is not a valid benchmark or speed win.
- [ ] Locate the first nonfinite packed graph value, distinguish normalization
  subnormal conversion from storage overflow or another correctness issue, and
  validate any fix against unchanged model/waveform gates. Mean scaling occurs
  in FP32 before its output store; do not assume an unscaled half mean sum.
  Packed Conv1D/InnerProduct local64 paths have no barriers and do not resolve
  the physical64 reduction fault.
- [ ] Attribute remaining convolution, reduction and CPU fallback costs; compare
  equivalent warm work against an optimized CPU baseline before declaring a win.
  Current warm command wall time includes 64.539 s convolution and 13.784 s
  reductions; GPU performance still trails the CPU.
