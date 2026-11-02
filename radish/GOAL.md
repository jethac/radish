# Radish

## Goal

Build an experimental, compute-first Vulkan driver for Adreno 5xx within Mesa's
Freedreno project, starting with the Adreno 506 in the Lenovo ThinkSmart View.
First complete the applicable Vulkan 1.1 compute and shared-infrastructure
requirements and conformance evidence. Then run useful existing inference
workloads correctly and improve measured speech-inference performance. Continue
optimizing beyond a CPU win to use the attainable GPU performance.

Radish is a separate hardware driver sharing Mesa infrastructure, not a wrapper
around GLES and not a renamed Turnip. Vulkan is a route to existing inference
software and explicit compute control; it is not itself a promise of more FLOPs.

This directory holds the project goal, design and evidence index. Driver code
belongs in a dedicated branch/worktree of `jethac/turnip-rt`, initially under
`src/freedreno/radish/`. That path is a proposal, subject to the build-system audit.
The AC8 branches and working checkout must remain usable independently.

## Current implementation priority — 2026-11-03

The user has authorized implementation of all five optimization areas before
any new performance measurement. This supersedes the earlier conformance-first
execution order without removing any conformance or inference requirement below.
The paused campaign's conversion47 remains unexecuted.

1. Retain CPU neural-frontend and harmonic-source ORT sessions across requests,
   with validated graph optimization and bounded session/activation lifetime.
2. Improve TTS convolution kernels, layout and data reuse.
3. Reduce application graph/packing overhead and driver command/state overhead,
   preserving dependencies, resource guards and correctness.
4. Implement phase-specific STT kernels, quantized decoding, GQA and memory
   liveness needed for the full model to fit.
5. Add selectively validated mixed precision and real compiled Vulkan pipeline
   cache entries, including keying, serialization, merge and reuse.

Each area requires concrete integrated code and correctness/resource evidence.
Until all five are implemented, do not run latency or throughput measurements,
benchmarks, profiling or speedup selection. Source inspection, builds, native
resource admission and correctness checks are permitted. Existing timeout,
memory, disk, fault, service and device-ownership controls remain unchanged.
Historical f222, selected inference fcd/d332 and the corrected5c6d source/build
remain preserved. Later full conformance, matched performance, deployment and
GPU-efficiency obligations stay in the goal.

## Done means

- The Vulkan loader discovers Radish through an explicit, private ICD manifest
  on the tablet, without replacing the installed Mesa or breaking the live UI.
- A selected, existing inference backend runs a real model through Radish. Its
  required API version, extensions, features and limits are recorded from source
  before implementation is planned around it.
- GPU results agree with a CPU reference using stated, precision-appropriate
  tolerances. Quantized operations also have model-level accuracy checks.
- All applicable Vulkan 1.1 compute and shared requirements are implemented and
  validated, including unmodified applicable CTS. The requirements/evidence
  ledger records mandatory, conditional, optional, unimplemented and untested
  behavior. Selected application success is not conformance evidence. Any
  standard-version/property obligations outside compute execution remain
  unresolved until a normative applicability basis or implementation closes them.
  No conformance claim precedes that evidence.
- The selected model fits beside the live appliance services within the device's
  memory budget, including transient allocations, shader compilation and zram.
- For the target inference workload, warm end-to-end latency improves over the
  best applicable existing CPU/GLES baseline without unacceptable accuracy loss.
  Report cold startup, throughput, peak memory and sustained thermal behavior too.
- Account for the gap between the dominant kernels and measured attainable GPU
  compute/bandwidth limits. Pursue material remaining headroom in arithmetic,
  layout, register pressure, parallelism, memory traffic, fusion and submission.
  Completion requires evidence that no known material, actionable performance
  opportunity remains unexplored; beating the CPU alone does not satisfy this.
- A repeatable build, isolated installation, test and rollback procedure exists.

A correct driver that does not win the target benchmark is a useful intermediate
result, not completion of the performance goal. Report that outcome and pursue
the measured bottleneck rather than redefining success.
Likewise, a CPU win is a milestone, not the stopping condition. Keep optimizing
the actual inference path until its remaining limits are measured and explained.

## Scope

First device: Adreno 506 at up to 650 MHz, APQ8053, mainline `msm` DRM, arm64.
Other a5xx devices are future targets requiring their own capability and quirk
validation. Do not infer their support from an A506 result.

First workload class: buffer-based inference compute, including matrix products,
convolution, reductions, activation functions and data conversion. Begin with
FP32 correctness; prioritize FP16 arithmetic and storage once independently
validated. Quantized kernels follow the selected backend's actual needs.

The initial experimental scope excluded a full conformance claim. The current
user priority supersedes that limit: finish applicable conformant Vulkan 1.1
compute and shared behavior before performance experiments. Presentation, ray
tracing and Android/KGSL remain outside the intended workload. Do not invent a
compute-only standard profile or waive required properties, format capabilities
or commands solely because graphics queues are absent. Keep unresolved
standard-version obligations visible and report any scope conflict explicitly.
Do not expose graphics queue capability without implementation and validation.

Do not promise descriptor indexing, arbitrary device-pointer operations,
subgroup features, cooperative matrices or modern Vulkan versions before an
explicit hardware and implementation feasibility check. If a backend requires
an unavailable capability, evaluate an older supported path or a small backend
adaptation and record the cost; never fake feature support to pass its checks.

## Approaches and decision

1. Extend Turnip with an a5xx backend. Reuses substantial Vulkan machinery, but
   Turnip's descriptor and command-state assumptions are tied to newer hardware.
2. Separate Radish driver inside Freedreno, sharing helpers and compiler code.
   Requires more frontend integration but permits an explicit a5xx binding model.

Selected direction: option 2, following the public maintainer discussion linked
below and the compute-first scope. Extract shared helpers only where a concrete
consumer needs them; avoid a speculative rewrite of Turnip or Gallium.

## Architecture

### Vulkan frontend and device capabilities

Use Mesa's Vulkan runtime and entrypoint generation for instance/device objects,
allocation bookkeeping and applicable synchronization infrastructure. Audit each
helper's dependencies before reuse. Expose one compute queue family initially;
start with one queue and serialize submissions correctly before concurrency.

Maintain a capability matrix linking each exposed feature and limit to its
implementation, hardware evidence and tests. API version reporting and feature
advertisement require a separate audit; successful enumeration is not proof.

### Shader compiler

Use SPIR-V to NIR and the existing ir3 A506 backend. Add a Radish-specific shader
ABI for descriptor references, push constants, dispatch parameters, workgroup
memory and built-in IDs. Keep this ABI explicit and version shader-cache keys
when it changes. Reject unsupported shader capabilities clearly.

Audit the local a5xx FP16 storage and register-footprint patches against the
chosen Mesa base. Treat them as candidates requiring reproduction, not accepted
dependencies. Check generated ISA, register usage, spills and workgroup limits.
Native FP16 instructions alone do not prove FP16 storage semantics or speed.

### Descriptors

Lower Vulkan set/binding references into a5xx's bindful hardware resource slots
at pipeline creation. Build a pipeline resource map for UBOs, SSBOs, textures,
images and samplers as needed. At bind/dispatch time, materialize hardware state
from the bound sets and dynamic offsets using that map.

Determine slot counts and indexing restrictions from source and hardware tests.
Start with fixed bindings and bounded arrays required by the first workload.
Descriptor arrays, dynamic indexing, robustness, null handling and descriptor
lifetime must each have defined behavior before their associated capabilities
are exposed. Do not inherit Turnip's bindless descriptor layout.

### Command emission and memory

Use Freedreno's a5xx compute implementation as the executable reference for
shader loading, resource state, direct/indirect dispatch and required packets.
Reuse existing DRM/BO infrastructure where suitable; audit BO residency,
reference lifetime, address ranges, allocation failures and submission ownership.

Implement buffer allocation/binding/mapping, uploads, readback and buffer
transfers first. Add image operations only when the backend requires them or a
measured kernel benefits. Avoid unnecessary CPU/GPU copies on shared memory,
but explicitly honor cache maintenance and host visibility requirements.

### Synchronization

Document how Vulkan execution and memory dependencies map to a5xx waits, cache
flushes/invalidations and DRM synchronization. Begin conservatively, then narrow
barriers only with tests and measurements. Cover host-to-device, device-to-host,
dispatch-to-dispatch and transfer dependencies, command-buffer reuse, fences,
semaphores and resource destruction. Implement timeline semantics if required by
the chosen backend, using audited Mesa support where possible.

### Measurement

Provide reproducible arithmetic, bandwidth, occupancy and dispatch-overhead
microbenchmarks plus representative model operators. Record shader ISA and
resource usage with results. Validate timestamp support before relying on GPU
timings; retain wall-clock end-to-end measurements.

Compare equivalent precision, shapes and work. Include uploads, conversions,
barriers and readback where the application pays them. Report CPU and GPU
coexistence, thermal state and memory pressure. The nominal GPU FLOPs estimate
is unverified context, not an acceptance criterion.

Maintain a GPU efficiency account for the dominant real operator shapes. Measure
attainable arithmetic throughput and memory bandwidth under the appliance's
actual clock, thermal and coexistence conditions, then relate useful operation
counts and logical traffic to observed kernel time. Distinguish host emission,
submission/waits, memory movement and GPU execution; use hardware counters only
after their meaning and collection are validated. Unknown GPU utilization is an
open question, not evidence that the hardware is saturated.

Prioritize measured losses and revisit the largest remaining limit after each
change. Evaluate relevant precision/storage paths, compiler output, tiling,
occupancy, data reuse, fusion, residency and scheduling without relaxing accuracy
or hiding work outside the timing boundary. Microbenchmarks establish attainable
limits and guide implementation; accepted wins must reach representative model
and sustained end-to-end measurements. Record rejected approaches and their
evidence, and reopen them only when a concrete new mechanism changes the case.

## Milestones and exit checks

| Milestone | Deliverable | Exit check |
| --- | --- | --- |
| M0: evidence and compatibility | Pinned Mesa base; backend requirements; a5xx capability/unknowns matrix; build and device-test isolation | Choose an inference target from source evidence, initially evaluate ggml/llama.cpp Vulkan and ncnn; identify blockers before committing to either |
| M1: enumeration | Experimental ICD, instance/device, compute queue, buffers and host access | Loader enumeration and repeated create/destroy/allocation cycles work; capabilities remain conservative |
| M2: first computation | SPIR-V/NIR/ir3 ABI, fixed SSBO bindings, dispatch and completion | Vector add and index-pattern kernels match CPU results across shapes and workgroup boundaries, including repeated submissions |
| M3: dependable compute | Descriptor mapping, push constants, transfers, shared memory and required synchronization | Relevant CTS subsets and adversarial producer/consumer tests pass; indirect dispatch and other required features have explicit status |
| M4: inference operations | FP16/storage path and backend-required features; representative operators | Reference comparisons pass; register and cache behavior measured; no reset or stale-output false wins |
| M5: model integration | Selected existing inference backend runs a useful model | Output/accuracy and full working-set requirements pass beside the live services; record fallback operations |
| M6: useful performance | Profile-driven optimization and repeatable deployment | Beats the applicable CPU/GLES baseline end to end; sustained test and recovery procedure pass |
| M7: GPU efficiency | Measured hardware limits, dominant-kernel efficiency account and remaining-headroom audit | Dominant losses are explained; material actionable opportunities have been implemented or rejected with relevant measurements, including sustained inference; a CPU win alone is insufficient |

Graphics support and wider a5xx coverage are subsequent projects, not reasons to
delay the first useful inference workload. Performance work remains in scope if
the first correct implementation is slow.

## Device and repository constraints

Follow the [device-workspace rules](https://github.com/jethac/lenovo-thinksmart-view/blob/main/WORKSTREAMS.md)
and the local handoff for tablet access and hazards.
All device mutations and measurements needing quiet take `.device.lock` for a
short bounded session. Keep the live listener running, use memory-capped test
processes, and honor its approximately three-minute maximum interruption.

Use a separate Mesa prefix and per-process ICD selection. Record every file
left on the device. Never replace system Mesa for an experiment. Check reset,
fault and hangcheck logs before and after GPU tests; stop after a reset or hang
to inspect it rather than retrying a faulting workload in a loop. Keep dispatches
bounded; raising the hangcheck timeout is not a performance fix.

No flashing, EDL, partition writes or unrelated boot/kernel changes are authorized
by this project goal. Preserve firmware, captures, secrets and large artifacts
outside the public repository. Follow the destination Mesa checkout's AGENTS.md;
public-community interaction and contribution requirements are separate from
local implementation authorization.

## GPT-6 failure modes and required mitigations

These are working safeguards for this project, not claims about a measured
model-wide defect rate.

| Failure mode | Mitigation and evidence required |
| --- | --- |
| Inventing register meanings, throughput or Vulkan guarantees | Label facts as source-backed, measured or hypotheses. Cite file/revision or a public source. Unknown fields stay unknown until tested. |
| Treating a6xx behavior as a5xx behavior | Trace every reused hardware path to a5xx definitions and validate on A506. No generation checks bypassed merely to make compilation succeed. |
| Claiming features because code compiles or enumeration succeeds | Gate advertisement on implementation and relevant tests. Track unsupported, untested and passing separately. Never claim Vulkan conformance from selected tests. |
| Copying plausible synchronization code | Write the dependency/cache mapping first; test chained dispatches, host visibility, buffer reuse and destruction under repeated execution. |
| Reporting a fast kernel that did no work | Initialize outputs with changing sentinels, validate the full output for small tests and independent samples for large tests, inspect faults, and reject incomplete submissions. |
| Optimizing a microbenchmark while inference remains slow | Profile the actual graph and report end-to-end latency, transfers, CPU fallbacks and memory. Keep operator and application wins distinct. |
| Stopping once GPU beats CPU, or declaring the GPU saturated without evidence | Continue against measured attainable GPU limits. Account for dominant losses and keep material unexplored headroom open; theoretical FLOPs and a CPU win do not prove efficient GPU use. |
| Inventing FP16 or quantization accuracy assumptions | Define tolerances per operator; test cancellation, extremes and accumulation; validate model accuracy before accepting a precision change. |
| Pulling every interesting AC8 branch into the base | Pin a base; inspect individual candidate commits and dependencies; port only changes with demonstrated relevance and validation. |
| Expanding into a complete graphics driver or framework rewrite | Keep the first backend's requirement matrix and milestone exit checks authoritative. Record new scope separately and get a decision before making it a dependency. |
| Writing a large speculative implementation | Work in small reviewable increments, each with a build and a discriminating correctness check. Resolve architectural unknowns with bounded experiments. |
| Bypassing framework behavior when composing layers | Preserve each child’s packing, precision and ownership contract; compare original and composite through actual CPU/GPU execution. Direct layer calls can bypass Net conversions and copy rules. |
| Tests that only mirror the implementation | Use independent CPU references, existing CTS where applicable, boundary cases and negative tests that would catch a plausible incorrect implementation. |
| Premature completion or stopping at a scaffold | Report milestone status against exit evidence. An ICD stub, first dispatch or isolated GEMM is not delivered inference support. Continue through integration and measurement. |
| Losing constraints across sessions | Keep a concise current status, next experiment, exact revisions, commands, results, failures and device changes in `radish/NOTES.md` once work starts. Read it before resuming. |
| Hiding uncertainty behind confident prose | State what is known, what failed and what the next check resolves. Separate an untested design from a measured result. |
| Disrupting the live appliance or other development | Separate worktrees/prefixes; bounded locked device sessions; memory limits; baseline restoration and explicit accounting of leftover changes. |

## Public evidence to harvest

- [Freedreno architecture and tooling](https://docs.mesa3d.org/drivers/freedreno.html):
  hardware model, descriptors, command decoding and replay tools. Validate tool
  applicability to a5xx rather than assuming all examples cover it.
- Mesa source: `src/gallium/drivers/freedreno/a5xx/` (especially
  `fd5_compute.c`, `fd5_emit.c`, `fd5_image.c` and `fd5_texture.c`),
  `src/freedreno/registers/adreno/a5xx.xml`, `src/freedreno/isa/`,
  `src/freedreno/ir3/`, and existing performance-counter definitions.
- [Connor Abbott / Rob Clark, a5xx Vulkan discussion, November 2025](https://www.mail-archive.com/mesa-dev@lists.freedesktop.org/msg225079.html):
  descriptor and draw-state differences; separate-driver recommendation. Treat
  statements about feature ceilings as design constraints to audit, not a reason
  to advertise a modern version through incomplete emulation.
- [Qualcomm a4xx/a5xx matrix multiplication](https://www.qualcomm.com/news/onq/2016/10/matrix-multiply-adreno-gpus-part-1-opencl-optimization)
  and [memory optimization](https://www.qualcomm.com/news/onq/2016/06/better-opencl-performance-qualcomm-adreno-gpu-memory-optimization):
  workload design guidance, with model-specific assumptions checked on A506.
- Public Linux `msm` and Qualcomm KGSL a5xx sources: initialization, counters,
  quirks and recovery behavior. Respect source licenses; do not copy GPL kernel
  implementation into differently licensed Mesa code.
- Local `streams/gpu-compute/`, `streams/gpu-mesa16/` and
  `streams/kokoro-gpu/`: existing measurements and candidate patches. Reproduce
  consequential findings against the chosen base before building on them.

## Immediate next step

The latest user instruction is: “once we have conformance, the full vulkan1.1
compute feature set, we will make it scream.” Conformance comes first. Freeze
performance artifacts and run no new performance experiments while closing
applicable compute/shared requirements. The user assigns the GPU budget entirely to compute and CPU work to graphics;
reserve no GPU headroom for graphics. Remove an actual reservation only after
tracing its source and units. This does not authorize global graphics/service
changes or relaxing residency, safety or correctness guards.

The selected fcd/d332 inference stack
stays unchanged; a separate Vulkan 1.1 candidate is not promoted by an API string,
entrypoint count, native compilation or a few passing consumers.

[VULKAN11_COMPUTE_STATUS.md](VULKAN11_COMPUTE_STATUS.md) is the current requirement
and evidence ledger. Implement real defects, then run bounded, applicable,
unmodified CTS partitions and meaningful independent consumers. Preserve
failures, legal shader environments, complete numerical/guard oracles and the
original device, memory, time, disk and service controls. Modified CTS harness
results remain diagnostics. The historical31 property failures are retained; the successive integrated checkpoints reduce 31 to 15 to 9 to 0 messages,
with all five original information cases now Pass. The broader normative matrix
and applicable CTS remain incomplete. None is waived
by a compute-only queue. The earlier 40-command
worklist and bounded resource milestones do not define completion.

After this phase, resume the full intended Straylight inference campaign:
accuracy, safe memory headroom, service coexistence, sustained behavior,
end-to-end deployment and material GPU efficiency all remain required. A CPU
crossover does not complete that later phase. Implementation, integration,
validation and publication remain delegated; root coordinates user priorities.

## Historical milestone chronology

The following entries preserve earlier phases and their evidence. Any earlier
instruction to continue performance or exclude formal conformance is superseded
by the current priority above.

The first performance recovery, 0045, preserves exact model outputs and reduces resident fixed-core time by 22.0028% through existing safe math batching. The candidate still takes 52.746539 seconds versus the historical 34.303633-second fixed-core result. Continue normalization/submission recovery and end-to-end Straylight optimization; this result is not campaign completion. See BATCHING_RECOVERY.md.

The separate normalization batching recovery preserves exact outputs and reduces resident fixed-core time another 2.4763% to 51.440372 seconds. The remaining historical gap and end-to-end Straylight goals remain open. App-selected robustness is a separate reviewed opt-in experiment; the driver keeps complete requested robustness, and unsafe shader tails must be fixed before robustness-off execution.

The explicit application robustness pair now passes after four shader-tail fixes and a full used-family bounds review. On the same binary/driver/loader, explicit false reaches 34.333299 seconds resident versus true 51.504783, with identical outputs and scheduling. Driver support and application defaults remain unchanged. This workload-scoped result restores the approximate historical fixed-core baseline; address optimization, Japanese integration and end-to-end Straylight targets remain active. See EXPLICIT_ROBUSTNESS.md.

The reviewed nonnegative packed-A shift now passes the same complete model with eight fewer encoded instructions per packed variant. Its 33.952417-second resident observation is modest and not wholly attributable to math savings. Select this correct private candidate for the existing Japanese runtime boundary/import/validation route; no end-to-end claim precedes actual capture validation.

The first intended Japanese runtime request now passes all 443,542 core values and 100,800 waveform samples on its exact captured tablet boundary. First synthesis is 47.314285 seconds; no resident or CPU comparison is inferred. Next use the existing resident request loop and the matching four-thread ncnn CPU engine with the same utterance, voice and tokens, retaining full per-request validation and original resource limits. RUNTIME_INTEGRATION.md and results/japanese-first-runtime-0045.json record the scope.

The matched Japanese resident comparison is now a bounded CPU-crossover milestone: Vulkan32.835343s versus four-thread ncnn CPU37.262337s, all four actual boundaries and complete outputs validated. Cold GPU44.566534s remains slower than CPU41.565206s; GPU peak502.9MiB/10.3swap is higher than CPU379.1MiB/0swap. One sequential pair is not statistical confidence or completion:4.2s audio still needs7.82x realtime. Continue the original end-to-end efficiency goal and bounded source/native/output gates; RUNTIME_INTEGRATION.md records precise scope.

A separate04bd checkpoint now qualifies public KHR_16bit_storage/storageBuffer16BitAccess through a valid storage-only module and500 output words/full allocation checks. API1.0 and other16-bit features remain unchanged; ASR is not yet runnable. All existing inference performance comparisons stay frozen on fcd, with explicit FP32 option checks required before selecting expanded features.

The bounded packed-B reciprocal experiment passes exact full outputs but regresses resident fixed-core33.952417→34.667620s and the12target command walls. It is published as unselected; fewer native instructions alone are not a performance win. Keep d332 selected and use retained profiles for subsequent source work, without repeating unchanged workloads to chase a timing.

Separate public storage8 and timeline consumers now pass on f784: 500 byte-storage outputs/full guards and initial/finite/ANY-ALL/host/queued 64-bit timeline operations around real compute. API remains 1.0; no ASR inference/conformance claim. Current inference stays fcd/d332, with FP32 options explicitly verified. Continue backend compatibility and measured GPU efficiency without changing defaults or lowering guards.

The current selected-stack arithmetic diagnostic passes all independent raw oracles and exact native/timer selectors, attaining 21.2518 and 22.5264 GFLOP/s over elapsed dispatch intervals. The 5.6584% balanced unroll reduction is calibration evidence, not model acceleration or pure ALU utilization. Continue end-to-end model efficiency and ASR compatibility with all accuracy, memory/headroom and service constraints retained.

A separate public Int8 arithmetic checkpoint now passes 1,280 generic integer/conversion/shared/private results and complete allocation guards with only Shader+Int8 capabilities. This closes a real bounded backend prerequisite; complete ASR module legality, resource limits, numerical workloads and end-to-end efficiency remain separate work. Selected fcd/d332 inference and defaults are unchanged.

The sequence remains compute/shared functionality first, then intended Straylight model performance. The bounded command/shared milestone is qualified; remaining ASR feature/module/resource/backend requirements continue alongside isolated performance candidates on the frozen selected inference stack. Full graphics and formal conformance are outside this campaign. End-to-end deployment, accuracy, material memory headroom, service coexistence and GPU efficiency are still required; no primitive test, CPU crossover or kernel microbenchmark completes the goal.

The exact-order packed inner-dot unroll4 experiment is correct but unselected: resident33.952417→35.671223s and all twelve targeted commands regress, with candidate23.9MiB swap as a confound. Keep d332 and retain complete evidence; no same-candidate benchmark hunting or cache-cause claim. Continue measured end-to-end efficiency through distinct source-backed candidates and remaining compute/shared requirements.

The distinct unroll2 experiment also remains unselected: native length31 preserves preload, yet target command time grows9.36% and resident time1.91% with zero swap. Keep d332; no identical retry or factor sweep. Remaining work concerns compute/backend coverage and effective data movement/kernel organization, then full intended model efficiency and deployment.

The full explicit KHR backend catalog and opt-in native listener now link with 50 validated required modules, nine GPU placement gates and lazy KV. This is host-only: source-known memory still leaves insufficient established headroom, and actual selected-family/native/whole-model numerical and deployment gates remain. Keep fcd/d332 selected and all original budgets; no host-link completion shortcut.

The first actual-hotspot timestamp diagnostic is numerically correct but unmeasured: zero expected records exposed an identifier-domain error (compact descriptor slots versus Vulkan binding identities). Preserve this failure; require semantic lookup plus exact count/guard validation before any interval/rate claim. Successful execution alone is not measurement qualification. This is a narrow diagnostic correction, not a new audit framework or campaign completion.

The corrected logical-binding timestamp diagnostic now passes all 2,400 raw records and unchanged full numerical/scheduling gates. The twelve resident hotspots spend 8.085s inside measured GPU command intervals out of 9.359s command wall, attaining 13.6954 issued-dot GFLOP/s over intervals that include CP/setup/flush/wait work. This is not ALU utilization or a performance optimization. Continue concrete data-movement/kernel-organization work and remaining ASR admission; fcd/d332 stays selected.

The separate ASR memory-policy checkpoint now cross-links a real Cortex-A53 AArch64 backend and listener, with bounded metadata construction and exact cache-operation modules. This closes target ABI build evidence only; actual native-family, full-model memory, numerical execution and deployment gates remain open. The known600.026MiB subtotal excludes material runtime overhead, so the unchanged640MiB full-model scope is not admitted. Selected fcd/d332 inference remains unchanged.

The selected ASR native-only gate now passes33 actual pipeline cases/31 modules
under the original public feature and resource checks, with zero tensor dispatch.
Full-model numerical, placement, peak-memory and performance gates remain open.
A retained BASIC probe used SPIR-V1.3 in an API1.0 helper with an internal override;
its successes are internal semantics, not legal public1.0 qualification. Public
legal1.1 BASIC/environment coverage remains an explicit compute requirement; see
the scoped erratum in CORE_API_0044.md. Selected fcd/d332 remains unchanged.

The target-only half-spatial/two-group experiment now passes exact native order,
resource limits, actual two-group dispatch and full model correctness, but loses
32.31% resident time (target layers+117.43%,zero swap). It remains unselected;
fcd/d332 is unchanged. Source preload/barrier work increases are recorded without
claiming a measured bottleneck cause. No identical retry or extra factor follows
from this result; the broader model efficiency and ASR functional/memory goals
remain incomplete.

Current conformance-first delta: legal public1.1 BASIC, bounded shared/command
semantics and22 further original buffer/atomic CTS cases pass; initialized
single-sample D24S8 aspect copies now have meaningful changing-input evidence.
The official properties case still fails, compressed sRGB strict observations
remain unresolved, required core pointer lookup now passes public-loader validation, and
full applicable shader, format, memory, descriptor and synchronization CTS is
still missing. No small test inventory establishes conformance. The full later
model correctness, placement, memory, speed, thermal and deployment goal remains
unchanged. GPU resources are for compute; CPU graphics does not waive normative
requirements or justify reserving GPU capacity for unimplemented graphics.

The current coherent validation checkpoint adds required typed atomic operator
evidence, seven original descriptor cases and ten original ETC2/EAC compute
sampling cases. A source-backed subpixel property correction repairs the original
CTS reference precondition without changing shaders or thresholds. Nine property
messages and 42 stricter custom sRGB mismatches remain; full applicable validation
and standard obligations are still incomplete. Newer graphics-queue validity
clarifications also require the next attachment consumer to exclude graphics-only
host-object calls. These are requirement deltas, not campaign completion.

The valid image/query/layout consumer now passes without graphics-only host
objects. The original five information cases also pass after source-backed
scalar corrections, closing their observed 31-message history. This is a bounded
query milestone: strict sRGB precision and complete applicable compute/shared
validation remain open, and the later inference campaign is unchanged.

The next bounded evidence adds eighteen original query lifecycle cases and the
required native-backed blend format-query traits. Their source/oracle limitations
remain explicit; the full required behavior/precision/CTS work remains active.
No performance experiment or inference-selection change follows from these passes.

The next concrete requirement evidence covers individual 1.1/core2/maintenance3
queries, the existing extended block storage-view path, required native format
query traits, and precisely scoped cache/lifetime behavior. Full applicable
shader, memory, image, precision and CTS validation remains unfinished. Real
pipeline-cache population is a later optimization opportunity, not evidence
manufactured for conformance and not an assumed speedup. Conformance remains
first; full model accuracy, deployment, memory and performance work follows.

## Named compute milestone

The user's next milestone, all 93 applicable cases in the pinned named
`dEQP-VK.compute` catalog, now passes on the same f222/e699 candidate. Original
case work, oracles, limits and failure stops were retained; earlier-driver
evidence was confirmed where needed. This is one completed milestone within
the full applicable Vulkan 1.1 compute/shared goal. It does not complete the
other API, shader, memory, robustness, format, descriptor or synchronization
requirements, resolve the strict sRGB findings, or authorize resumed tuning.
Selected inference remains frozen until the broader conformance phase closes.
