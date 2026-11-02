# Radish

## Goal

Build an experimental, compute-first Vulkan driver for Adreno 5xx within Mesa's
Freedreno project, starting with the Adreno 506 in the Lenovo ThinkSmart View.
Run useful, existing Vulkan inference workloads correctly and improve measured
speech-inference performance on this device. Continue optimizing beyond a CPU
win to make full use of the GPU's attainable performance on these workloads.

Radish is a separate hardware driver sharing Mesa infrastructure, not a wrapper
around GLES and not a renamed Turnip. Vulkan is a route to existing inference
software and explicit compute control; it is not itself a promise of more FLOPs.

This directory holds the project goal, design and evidence index. Driver code
belongs in a dedicated branch/worktree of `jethac/turnip-rt`, initially under
`src/freedreno/radish/`. That path is a proposal, subject to the build-system audit.
The AC8 branches and working checkout must remain usable independently.

## Done means

- The Vulkan loader discovers Radish through an explicit, private ICD manifest
  on the tablet, without replacing the installed Mesa or breaking the live UI.
- A selected, existing inference backend runs a real model through Radish. Its
  required API version, extensions, features and limits are recorded from source
  before implementation is planned around it.
- GPU results agree with a CPU reference using stated, precision-appropriate
  tolerances. Quantized operations also have model-level accuracy checks.
- Relevant compute, memory, descriptor and synchronization tests pass. Every
  unsupported or failing case is recorded; no broad conformance claim is made.
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

Initial non-goals: presentation, swapchains, graphics pipelines, render passes,
ray tracing, Android/KGSL integration, and a full Vulkan conformance claim.
Do not expose graphics queue capability. A compute-only experimental ICD does
not establish conformance to a standard Vulkan version or profile.

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

The user confirmed the campaign order: “finish the compute and shared infra,
then make the models we want to run for straylight scream on the GPU.” Complete
compute correctness and the shared infrastructure first, then validate the
existing full inference path and pursue end-to-end Straylight GPU performance.
Graphics remains outside this campaign. The original accuracy, memory, service
coexistence and material-headroom requirements remain unchanged; a CPU crossover
or an isolated kernel win is not the completion boundary.

The user's earlier 2026-11-02 direction paused performance experiments to fill
compute behavior required by Vulkan 1.0 and 1.1 for the fleet. Graphics remains
outside scope. The 40 missing, broken or blocked command paths in the audit
(33 Vulkan 1.0 paths and seven Vulkan 1.1 additions) are one tracked worklist,
not the completion boundary.

Give execution holes, descriptor/resource gaps and Vulkan 1.1 compute
requirements equal priority. Track legal workgroup and shared-memory behavior,
UBOs and dynamic resources, descriptor update and lifetime semantics, subgroup
BASIC, required SPIR-V behavior, synchronization, limits and features alongside
the command inventory. Implement the real resource consumers and dependencies;
entrypoint stubs or metadata-only objects do not close a functional path.

Keep source implementation, public availability and hardware evidence separate.
Use applicable existing CTS cases and focused independent checks where they
catch real gaps. Conditional commands do not all impose mandatory feature
requirements. No API-version, profile or conformance claim follows from symbol
counts or selected passing tests. Keep unsupported capabilities honestly
reported while implementing the remaining required compute behavior.

All implementation, integration, builds, device validation and publication are
delegated to subagents; the root agent coordinates the campaign and user
conversation. The bounded compute/shared transition gate is now satisfied: the
concrete resource closeout and unchanged full-model correctness checks pass,
with full conformance limits and failed CTS evidence retained. The user has
authorized the Straylight performance phase, starting with safe batching on
the same correct kernels and original numerical/resource guards. Keep cold,
resident, fixed-core and end-to-end results distinct. The original performance
and GPU-efficiency goals remain in force; no CPU-crossover shortcut applies.
