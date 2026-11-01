# Vulkan 1.0 / 1.1 audit

Audit date: 2026-11-01. Scope: Radish on Adreno 506, source and existing evidence,
with no new device execution. This report does not certify conformance.

## Answer

Radish currently exposes an **experimental Vulkan 1.0 compute subset**. Native
SPIR-V dispatch and an ncnn Vulkan ReLU are demonstrated. A complete Vulkan 1.0
implementation is not demonstrated, and merely changing `apiVersion` to 1.1
would expose missing functionality and incorrect properties.

**Vulkan 1.1 is a plausible engineering target, not an established hardware
ceiling or a completed compatibility level.** This audit found substantial
software work but no source-backed proof that the A506 cannot implement the
required 1.1 compute behavior. Conversely, lack of a known blocker does not prove
that implementation or CTS execution will succeed. Graphics remains outside
the approved Radish goal.

The queue rules allow an implementation with no graphics queues; the requirement
for a combined graphics/compute family is conditional on exposing graphics.
The specification's feature/limit requirement sections explicitly describe
graphics implementations. Do not turn those qualifications into either a claim
that compute-only is forbidden or a blanket waiver of compute API semantics.
The experimental subset is still missing legal compute operations. Formal
conformance requires the applicable CTS process, beyond this audit.
([Queue rules](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html#devsandqueues-queueprops),
[feature requirements](https://docs.vulkan.org/spec/latest/chapters/features.html#features-requirements),
[CTS submission requirements](https://github.com/KhronosGroup/VK-GL-CTS/blob/main/external/vulkancts/README.md#conformance-submission-package-requirements))

## Follow-up evidence on 2026-11-01

The detailed tables below preserve the initial source snapshot. Later work
supersedes its missing shared-memory and aligned-only byte-copy findings:
shared memory is lowered and tested up to the conservative 16 KiB cap; unaligned
copies use a serial CPU fallback; bounded 3D dispatches preserve logical IDs;
recovered GPU faults now return DEVICE_LOST. Internal dispatch bases do not
implement the missing public `vkCmdDispatchBase` command.

ncnn's 32-thread reduction adaptation, FP32 convolution/projection cases and
the complete Kokoro neural core pass output comparisons. The latest explicit
AffineSnake/deconvolution graph executes all 715 non-Input layers through Vulkan,
including the three former CPU deconvolutions. Full saved-core and waveform
comparisons pass. Earlier graphs also have fresh single-process tablet speech
synthesis evidence; live-service integration and a CPU performance win remain
unfinished. These workload results do not close the remaining API gaps.
The identical reduction shader
still hangs at physical64, so the backend workaround does not repair general
execution correctness. The principal baseline and 1.1 API gaps remain.

Headless CTS at `5a9cb32498158eb60774cf8a3d4e0b8c68f7fe9e` is built and
bounded case lists are reviewed; no device CTS case has executed. Current
evidence, artifact hashes and logs are in `NOTES.md`.

## Method and snapshot

- Mesa checkout: `/run/media/jethac/Shared/radish/mesa`; base
  `ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230`, uncommitted Radish additions.
- Read all ten Radish source/build files, its runtime integration, the generated
  feature/property and entrypoint helpers, and the existing tests/evidence index.
- Enumerated all **137 Vulkan 1.0 commands and 28 new Vulkan 1.1 commands** from
  the checkout's `src/vulkan/registry/vk.xml`. Internal BASE/COMPUTE/GRAPHICS
  registry groupings were combined and aliases/duplicates removed. These
  internal group names are not independently advertised API versions.
- Checked defined `vk_common_*` symbols in the existing arm64 library to avoid
  mistaking weak declarations for implementations. Source tracing determines
  whether a common wrapper has its required downstream implementation.
- Concurrent implementation work is adding shared-memory support and
  fixing findings. Findings below describe the initial audit snapshot; a later
  working-tree change does not retrospectively verify this report's gaps.
- Specification references use current Khronos documentation for current rules
  and the tagged Khronos `v1.1.126` source where a modern HTML build hides older
  version-specific language. No Roadmap 2022/2024/2026 or Android requirements
  are silently substituted for Vulkan 1.0/1.1 requirements.

Initial audited source SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `meson.build` | `bb6c3eab1ab9122555ef128ed6633c15d8bc09fd9419a3fd964584a0590fc999` |
| `rd_command.c` | `5436eb6ddb1b622f37473ade6546ac890808ebed86c3c719fa906f588224173d` |
| `rd_descriptor.c` | `a7445e4f43e9547eec6fe13664d968d57f3a20af7ba94e62c0c57541ea20955a` |
| `rd_device.c` | `0b2edd32da10ce43ba72046575d1bdc00b843be11e72f8be0e6e5cf8d2242b8c` |
| `rd_instance.c` | `3d66477d37f8624ac3b3fb339df031e38323defc3befb37783396aee7234edac` |
| `rd_memory.c` | `fe85c1123805df59f202ee1cfa7a1a204fb05692d555b2ca8eb86d5a14e471b1` |
| `rd_pipeline.c` | `4bb127846b3140dc1b731084ce783c92db9c7aa0caa9b52999c330783bd460fa` |
| `rd_private.h` | `ba71fc5b838f7a9dfa8d16b71dd1e97b299b0385c5c5f9e0c59151aa393422c6` |
| `rd_queue.c` | `310bd575839953bba9e30ec73e629a3c62ad2994fddda889f40b48eacde7d24b` |
| `rd_transfer.c` | `d7fdc0097f5be2d8b7a072d803fe1df592a3a28168de12c79445cc73aa8059d1` |

All source locations below are relative to that Mesa checkout unless prefixed
`radish/` (this repository). Narrative line numbers refer to this snapshot; appendix references can shift with concurrent edits, so use the named function and recorded hash when reproducing a finding.

Update observed while the audit was in progress: the implementer added shared
lowering (`rd_pipeline.c` SHA-256
`a21ef707abcb2447eba11adc3bc2fc53500819aacda347d16762e277ca5b9e4a`).
Compile-only testing succeeded, but the first hardware shared-reduction run
(128 invocations, 32 KiB shared allocation, accesses above 16 KiB) left the
sentinel unchanged and faulted with status `E40801C3`, followed by recovery.
`shared-128.log` and the timestamped `compute-shared-128` logs confirm this;
the device coredump was retained outside the repository. Testing stopped and
both live services remained active. Root cause is unresolved. **Shared memory
is therefore failing on hardware in that experiment, not merely untested.**

## Findings that affect existing compute clients

| Priority | Finding and source evidence | Consequence / required proof |
| --- | --- | --- |
| High | Most limits are zero-initialized: `rd_instance.c:85` initializes a small subset of `vk_properties`; the generated property query returns the remaining zero values. In particular storage-buffer descriptor counts and `maxPerStageResources` are zero. | The advertised contract does not describe the operations already exercised. Populate limits from actual implementation capacity, test their boundaries, and distinguish hardware estimates from verified limits. Setting numbers is not implementing features. |
| High | `rd_transfer.c:rd_transfer_range` requires 4-byte alignment for offsets and size, including `CmdCopyBuffer`. | Legal byte copies such as source offset 1, destination offset 3, size 5 fail command recording. Buffer copy is not subject to fill/update alignment restrictions; implement byte copies while preserving ordered GPU dependencies. |
| High | Shared memory is advertised as 32768 bytes (`rd_instance.c:101`) but the audited shader path rejects `nir->info.shared_size` and does not lower shared variables (`rd_pipeline.c:84–94`). | Reductions and tiled kernels are missing. A large shared-memory property must be validated with address, layout and multi-wave barrier tests. Concurrent implementation is underway, not evidence in this snapshot. |
| High | `vk_common_CmdDispatchIndirect` exists but delegates to absent `CmdDispatchIndirect2KHR` (`runtime/vk_command_buffer.c:765`). | A non-NULL core command can reach a NULL downstream hook. Implement indirect dispatch or keep the incomplete experimental status explicit; there is no optional core feature bit that switches off indirect compute. |
| High | Only SSBO descriptor types are accepted (`rd_descriptor.c:13,70`); dynamic offsets are rejected (`rd_command.c:94`); UBOs/images/textures are rejected (`rd_pipeline.c:90`). | UBO and dynamic buffer descriptors are core resource classes, not optional arithmetic features. ncnn's current buffers-only path avoids much of this, but general 1.0 compute compatibility does not. |
| High | Primary-only command allocation (`rd_command.c:37`); no event objects or event command implementation. Common event wrappers call absent `*Event2` hooks. | Secondary command buffers and events are missing core behavior. Generic runtime entrypoints do not implement the driver work automatically. |
| Medium | All nonzero compute-pipeline creation flags are rejected (`rd_pipeline.c:167`). | Legal core flags, including disable-optimization and derivative-related flags, need correct semantics or permitted implementation choices; blanket rejection is not a full core implementation. |
| Medium | Variant private-memory spills and constant-data allocations are rejected (`rd_pipeline.c:133`); local shapes/register demand are narrowly exercised. | Larger valid inference shaders can fail even when descriptors and API version match. Add adversarial shader/resource tests, then implement required backing storage or valid lowering paths. |
| Medium | No query pools; timestamp wrappers have missing downstream hooks. `timestampValidBits=0` honestly disables queue timestamp use. | Wall-clock measurement remains valid. Timestamp features must not be used until implemented and calibrated. Pool API completeness and graphics-only query cases are separate questions. |
| Medium | Descriptor arrays have code paths but only small fixed slots were tested. Ordinary descriptor writes are snapshotted at record time. | Test constant/dynamic array addressing, cross-set maps, zero-count bindings, compatibility/disturbance and lifetime rules. Dynamic-indexing feature is currently false; do not expose it from plausible lowering alone. |

The byte-copy finding follows the actual command/region requirements, not an
assumption about hardware DMA alignment.
([`vkCmdCopyBuffer`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html),
[`VkBufferCopy`](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferCopy.html))

## What Vulkan 1.1 adds

Promotion to core does not mean every optional feature bit becomes mandatory.
The following covers all 23 extensions promoted into 1.1, plus the new subgroup,
protected-memory and SPIR-V requirements. Extension names remain separate from
core-version availability; evaluate core structures/commands without requiring
an extension string that is not advertised.
([Khronos promotion list](https://docs.vulkan.org/spec/latest/appendices/versions.html#versions-1.1-promotions))

| 1.1 area / promoted extension(s) | Mandatory vs conditional | Radish status / work |
| --- | --- | --- |
| `KHR_get_physical_device_properties2` | Extensible queries are core. | Only advertised instance extension. Runtime features/properties queries and seven driver/query wrappers exist. General `pNext` output coverage still needs poisoned-output tests. |
| `KHR_bind_memory2`, `KHR_get_memory_requirements2` | Core buffer/image batch-binding and requirements APIs. | Buffer implementations exist; image hooks absent. Buffer requirements neglect `VkMemoryDedicatedRequirements` in `pNext`. |
| `KHR_dedicated_allocation` | Dedicated-allocation query/creation structures are core; implementation may report no preference/requirement for ordinary buffers. | Must explicitly fill both dedicated-requirement booleans, not leave caller memory untouched. Ordinary one-buffer allocation is not itself proof of dedicated-query handling. |
| `KHR_device_group_creation`, `KHR_device_group` | Single-device groups are legal; multiple physical GPUs are not required. Core dispatch-base semantics still apply. | Runtime single-device group enumeration, peer flags and device-mask helper exist. `CmdDispatchBase` is missing; allocation/bind/submit device-index chains need tests even for index/mask 0/1. |
| `KHR_external_memory_capabilities`, `KHR_external_memory` | Core capability queries/structures; platform-specific export/import handle extensions are separate. Unsupported handle capabilities can be reported honestly. | External-buffer query absent; image query always unsupported. No memory FD extension advertised. Need zero-support query implementation and `VkPhysicalDeviceIDProperties` identifiers; current UUIDs are zero placeholders. |
| `KHR_external_fence_capabilities`, `KHR_external_fence`, `KHR_external_semaphore_capabilities`, `KHR_external_semaphore` | Core capability queries/structures; actual FD/Win32 sharing is not made mandatory simply by promotion. | Runtime DRM syncobj-based queries exist. Audit handle masks against chosen sync backend and advertised platform extensions; do not infer end-to-end sharing from these helpers. |
| `KHR_descriptor_update_template` | Descriptor-set template update is core; push-descriptor template use additionally needs push-descriptor support. | Runtime template create/destroy exist; `UpdateDescriptorSetWithTemplate` is missing. |
| `KHR_maintenance1` | Includes command-pool trimming and resource/transfer/viewport changes; graphics-only parts remain outside this goal. | Runtime `TrimCommandPool` exists. Image behavior absent. Compute-only does not eliminate buffer-copy/format obligations for the resource classes eventually exposed. |
| `KHR_maintenance2` | Core behavior/structures; much is graphics/image related. | Point-clipping property remains zero enum value; graphics/render-pass/image-view features absent. No claim of graphics coverage. |
| `KHR_maintenance3` | Layout-support query and allocation/layout properties are core. | `GetDescriptorSetLayoutSupport` absent; `maxPerSetDescriptors` and `maxMemoryAllocationSize` zero. Baseline graphics limit table requires at least 1024 descriptors per set and 1 GiB per allocation. The fixed host layout cap of 32 needs a representation/resource-map redesign, not just property inflation. |
| `KHR_storage_buffer_storage_class` | SPIR-V StorageBuffer class support in the 1.1 environment. | NIR SSBO mapping is suitable in principle; compile/run SPIR-V 1.3 fixtures to prove it. Existing Vulkan-1.0 GLSL fixtures do not cover the full new environment. |
| `KHR_relaxed_block_layout` | Relaxed block layout is core in 1.1; no enabling feature bit. | NIR explicit IO is used. Add vector/struct/array/matrix stride and offset tests, including valid relaxed offsets. |
| `KHR_16bit_storage` | Storage feature bits are optional in core 1.1; uniform-and-storage 16-bit support implies storage-buffer 16-bit support. Advertising the named extension requires storage-buffer 16-bit support. | All four bits false, extension absent. This is not by itself a 1.1 blocker. Native FP16 arithmetic is a different feature/extension, not implied by 16-bit storage. |
| `KHR_variable_pointers` | Core feature bits may be false. Advertising the named extension imposes its own storage-buffer-variable-pointer requirement. | Both bits false, named extension absent. Logical fixed-descriptor NIR address pairs are not proof of variable-pointer semantics. |
| `KHR_multiview` | Required for a complete 1.1 graphics implementation under the graphics feature requirements; geometry/tessellation combinations have separate optional bits. | All false; graphics excluded. Not an inference prerequisite or a reason to invent graphics support. |
| `KHR_shader_draw_parameters` | Feature remains separately queryable; advertising the named extension requires it. Graphics-only. | False, extension absent. Not a compute blocker. |
| `KHR_sampler_ycbcr_conversion` | Feature can be false in 1.1; conversion object APIs are core but valid use requires the feature. | Runtime object helpers exist; feature false and no image support. No need to fake YCbCr support for inference. |
| Protected memory (new core) | `protectedMemory` is optional; enabling it entails protected queues/memory and additional rules. | False; no protected memory/queue advertised, and queue flags rejected. Keep disabled. |
| Subgroups (new core) | A compute queue requires subgroup BASIC operations and compute stage in 1.1; other groups such as arithmetic/shuffle are separately reported. | Size 32 is reported, but operations/stages are zero, and generated SPIR-V capabilities therefore omit GroupNonUniform. This is a real 1.1 blocker; test builtins, subgroup barriers, partial/multi-subgroup workgroups before enabling BASIC. |
| SPIR-V versions (new core) | 1.0 needs SPIR-V 1.0; 1.1 needs versions 1.0–1.3 plus GLSL.std.450. | Shared Mesa translation is promising, but Radish lowering/backend paths still need representative positive and negative SPIR-V environment tests. |

For subgroup support the requirement is explicit even for a compute-only device.
The current zero-operation result must not be relabeled as 1.1 support.
([Subgroup properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceSubgroupProperties.html))
For requirements-chain outputs, the driver must write the requested values;
caller zero-initialization is not a substitute.
([Dedicated requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryDedicatedRequirements.html))
The full environment-version requirements are preserved in the
[tagged 1.1 SPIR-V appendix](https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/v1.1.126/appendices/spirvenv.txt).

## Features, formats and shader semantics

`vk_physical_device_init` receives NULL supported-features and device-extension
tables (`rd_instance.c:108`), so all 55 `VkPhysicalDeviceFeatures` bits and all
12 1.1 feature bits are false. The runtime correctly checks requested features
against this table. That is conservative, but does not disable baseline shader,
memory, descriptor, indirect-dispatch or synchronization requirements that have
no optional feature flag.

For a complete graphics implementation, robust buffer access is required for
1.0 (without portability-subset qualification), and multiview for 1.1. Neither
is implemented/advertised. Other 1.0 feature families are optional: geometry,
tessellation, rasterization extras, precise/statistical queries, extended image
behavior, dynamic resource-array indexing, wider scalar types, sparse resources
and inherited queries. Disabling those is appropriate until tested; it does not
prove all remaining core paths.
([Feature requirements](https://docs.vulkan.org/spec/latest/chapters/features.html#features-requirements))

All format queries return zero linear/optimal/buffer feature masks and all image
format queries return unsupported (`rd_instance.c:205–224`). Thus there is no
supported image/texel-buffer format path at all. This fails every nonzero
mandatory format-table cell applicable to a complete graphics implementation;
there is no isolated format exception to audit. No sampled/storage image,
sampler, buffer-view or image-copy path exists. Those remain intentionally
outside the first buffer-only inference path, but are missing from a general
compute resource implementation. The required format tables distinguish
unconditional support from feature-conditional compression/extended formats;
do not assert that every defined VkFormat is mandatory.
([Required format support](https://docs.vulkan.org/spec/latest/chapters/formats.html#formats-required-format-support))

The compiler path uses Mesa's SPIR-V-to-NIR specialization support and ir3 A506
code generation. Tested: SSBO FP32 arithmetic, integer indexing, push constants,
basic builtins and ncnn's specialized ReLU pipelines. Still missing or unproved:

- Workgroup memory/barriers in the audited snapshot; atomics and coherent
  shader-to-shader accesses beyond the few tested serialized cases.
- UBO access, dynamic buffer descriptors, sampled/storage resources, private
  memory/spills and compiler constant-data backing.
- Complete GLSL.std.450 coverage/precision, exceptional FP32 values, integer
  edge cases, matrix layout/strides and aggregate push-constant layouts.
- Local shapes on all three axes, all advertised group sizes, large group
  counts, resource usage up to advertised limits, and register-pressure cases.
- SPIR-V 1.1–1.3 constructs and subgroup BASIC for a future 1.1 target.
- Robust-buffer semantics if that feature is ever enabled; the current SSBO
  extents and unused-descriptor placeholder do not establish robustness.

The Vulkan 1.0/1.1 baseline shader memory model is not a promise of the later
`VK_KHR_vulkan_memory_model` feature. Likewise timeline semaphores,
buffer-device-address, 8-bit storage and descriptor indexing are not mandatory
just because the target is 1.1. Do not conflate modern backend requirements with
the older core requirements.
([SPIR-V capabilities/environment](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html))

## Synchronization, ownership and object lifetime

The source deliberately drains each dispatch/transfer chunk, waits on input
syncobjs and signals output syncobjs only afterward (`rd_queue.c:275–405`). It
uses native BO relocations, cache flush/fence packets and a bounded five-second
DRM completion wait. This can implement stronger ordering than requested;
stronger ordering alone is not an error. It also explains current overhead.

The barrier implementation is effectively a no-op for buffers, relying on the
per-operation drain; image barriers fail (`rd_command.c:153`). Existing chained
dispatch and transfer tests cover useful cases, but do not prove all Vulkan
memory-domain/cache dependencies. Required next evidence includes independent
cross-submit producer/consumer tests, read/write and write/write hazards,
semaphore/fence ordering, reuse, host observation and shared-memory barriers.
([Dependency and host visibility rules](https://docs.vulkan.org/spec/latest/chapters/synchronization.html))

Host memory is WC, exposed HOST_VISIBLE|HOST_COHERENT|DEVICE_LOCAL, with a CPU
ordering barrier at submission. Existing no-flush/invalidate tests support that
path. Source inspection cannot prove every cache/coherency case on this SoC;
test aliasing, mapping subranges and fence/readback behavior under pressure.
`ABI.md` still describes noncoherent memory and is stale relative to this source.

Recorded dispatches retain raw pipeline/buffer pointers and copied descriptor
values. Vulkan applications must keep resources alive while in use, and ordinary
descriptor updates can invalidate executable command buffers without
update-after-bind features. Do not incorrectly demand unsupported
update-after-bind behavior. Conversely, valid lifetime cases still need tests:
destroy shader modules after pipeline creation, destroy layouts after dependent
objects retain them, descriptor pool reset/free, command pool reset/destroy,
repeated and simultaneous-use submission, error cleanup and custom allocators.
The runtime plus existing callback/lifecycle tests cover part of this, not all.

No evidence here establishes device-loss recovery after a GPU fault. The goal's
bounded fault-stop procedure remains authoritative. Do not exercise large or
unbounded dispatches merely to test a version claim on the live appliance.

## Feasibility and practical order

1. Correct the advertised contract, byte copies and any exposed wrapper chains
   that lead to NULL hooks; retain API 1.0 experimental status.
2. Finish tested shared memory/barriers, core buffer descriptors, local-shape
   and pressure tests, independent submit/sync tests, indirect compute and
   secondary/event paths as demanded by actual workloads and relevant CTS.
3. Run representative ncnn convolution/reduction/model workloads with explicit
   CPU fallback accounting. Implement FP16 storage/arithmetic behind separately
   tested feature gates. This advances the existing inference/performance goal;
   version-number promotion does not replace it.
4. For a genuine 1.1 compute target, add subgroup BASIC, dispatch base,
   descriptor template updates/layout-support queries, dedicated-query outputs,
   external-buffer capability queries, stable IDs/allocation properties and
   SPIR-V 1.3/block-layout tests. Re-audit every row below after changes.
5. If full graphics/conformance becomes a separate user-approved goal, implement
   graphics, mandatory formats/features/limits and the full applicable CTS
   submission process. Do not silently expand this inference project into that
   scope.

A5xx has native compute, bindful SSBOs and ir3 support. Missing bindless
descriptors makes Turnip reuse and modern descriptor indexing difficult, but
does not itself prohibit 1.0/1.1 fixed-binding compute. The maintainer discussion
supports the separate-driver design; it is not hardware validation of every
listed feature.
([Maintainer architectural assessment](https://www.mail-archive.com/mesa-dev@lists.freedesktop.org/msg225079.html))

The earlier blanket claim that 1.2 is *formally impossible* needs qualification:
the current feature requirements make the descriptorIndexing feature conditional
on advertising the named extension, and describe dependencies among its feature
bits. Vulkan specifies behavior, not a mandatory hardware bindless descriptor
implementation. The maintainer's practical warning is valuable; this audit
does **not** prove either a hard 1.1 ceiling or a viable performant 1.2 driver.
That would require a separate full 1.2 audit, including its other required
behavior, rather than accepting/rejecting it from descriptor indexing alone.
([Current feature dependency rules](https://docs.vulkan.org/spec/latest/chapters/features.html#features-requirements))

## Existing evidence and audit limits

The local evidence index records loader lifecycle (24 cycles), index/vector-add
(32 dispatches), descriptor layout/order/span/callback regressions, transfers,
dependent compute (48 dispatches), coherent host access (32 dispatches), ncnn
ReLU (128/508/16396 scalar outputs), three Mesa host checks and four GPU-log
checker cases. These are useful narrow tests, not CTS coverage. See `NOTES.md`
and logs under `/run/media/jethac/Shared/radish` for exact runs.

No Vulkan CTS subset, live end-to-end inference, FP16 correctness, sustained
thermal test or inference speedup is established by this audit. The follow-up
above records later fixed-core and waveform evidence; the original source
audit involved no device run, installation or Mesa source modification.


## Appendix A: complete core command inventory

Statuses describe implementation reachability, not a pass count. `Runtime` means
Mesa common implementation is linked; it is **not** automatic hardware proof.
`Graphics scope` records deliberate absence without turning it into this project
's implementation task. All commands are listed, including conditional and
loader-owned cases. Source paths name either a driver implementation or common
entrypoint definition; missing downstream paths are explained explicitly.

### Vulkan 1.0 (137 commands)

| Command | Status | Source / qualification |
| --- | --- | --- |
| `vkCreateInstance` | Radish | `src/freedreno/radish/rd_instance.c:126`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkDestroyInstance` | Radish | `src/freedreno/radish/rd_instance.c:151`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkEnumeratePhysicalDevices` | Runtime | `src/vulkan/runtime/vk_instance.c:483`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkGetPhysicalDeviceFeatures` | Runtime / zero | `src/vulkan/runtime/vk_physical_device.c:110`. All 55 core feature bits false. |
| `vkGetPhysicalDeviceFormatProperties` | Runtime → Radish / empty | `src/vulkan/runtime/vk_physical_device.c:189`. All format feature masks zero. |
| `vkGetPhysicalDeviceImageFormatProperties` | Runtime → Radish / unsupported | `src/vulkan/runtime/vk_physical_device.c:206`. Always VK_ERROR_FORMAT_NOT_SUPPORTED. |
| `vkGetPhysicalDeviceProperties` | Runtime / incomplete | `src/vulkan/runtime/vk_physical_device.c:126`. Most reported limits zero; see complete limit inventory. |
| `vkGetPhysicalDeviceQueueFamilyProperties` | Runtime | `src/vulkan/runtime/vk_physical_device.c:142`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkGetPhysicalDeviceMemoryProperties` | Runtime | `src/vulkan/runtime/vk_physical_device.c:173`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkGetInstanceProcAddr` | Radish | `src/freedreno/radish/rd_instance.c:161`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkGetDeviceProcAddr` | Runtime | `src/vulkan/runtime/vk_device.c:470`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkCreateDevice` | Radish | `src/freedreno/radish/rd_device.c:4`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkDestroyDevice` | Radish | `src/freedreno/radish/rd_device.c:63`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkEnumerateInstanceExtensionProperties` | Radish | `src/freedreno/radish/rd_instance.c:21`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkEnumerateDeviceExtensionProperties` | Runtime | `src/vulkan/runtime/vk_physical_device.c:84`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkEnumerateInstanceLayerProperties` | Loader | Loader owns layer enumeration; absent ICD symbol is not a device-compute blocker. |
| `vkEnumerateDeviceLayerProperties` | Runtime | `src/vulkan/runtime/vk_physical_device.c:70`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkGetDeviceQueue` | Runtime | `src/vulkan/runtime/vk_device.c:478`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkQueueSubmit` | Runtime | `src/vulkan/runtime/vk_synchronization.c:400`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkQueueWaitIdle` | Runtime | `src/vulkan/runtime/vk_queue.c:1465`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkDeviceWaitIdle` | Runtime | `src/vulkan/runtime/vk_device.c:659`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkAllocateMemory` | Radish | `src/freedreno/radish/rd_memory.c:6`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkFreeMemory` | Radish | `src/freedreno/radish/rd_memory.c:29`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkMapMemory` | Runtime | `src/vulkan/runtime/vk_device.c:531`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkUnmapMemory` | Runtime | `src/vulkan/runtime/vk_device.c:552`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkFlushMappedMemoryRanges` | Radish | `src/freedreno/radish/rd_memory.c:69`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkInvalidateMappedMemoryRanges` | Radish | `src/freedreno/radish/rd_memory.c:77`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkGetDeviceMemoryCommitment` | Unavailable / conditional | No lazy-memory type is exposed, so no valid lazy-memory commitment use. |
| `vkBindBufferMemory` | Runtime | `src/vulkan/runtime/vk_buffer.c:146`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkBindImageMemory` | Broken chain / images | `src/vulkan/runtime/vk_device.c:602`. Common wrapper requires missing BindImageMemory2. |
| `vkGetBufferMemoryRequirements` | Runtime | `src/vulkan/runtime/vk_buffer.c:95`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkGetImageMemoryRequirements` | Broken chain / images | `src/vulkan/runtime/vk_device.c:583`. Common wrapper requires missing GetImageMemoryRequirements2. |
| `vkGetImageSparseMemoryRequirements` | Broken chain / conditional | `src/vulkan/runtime/vk_device.c:620`. Missing GetImageSparseMemoryRequirements2; sparse features disabled. |
| `vkGetPhysicalDeviceSparseImageFormatProperties` | Runtime | `src/vulkan/runtime/vk_physical_device.c:239`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkQueueBindSparse` | Runtime / conditional | `src/vulkan/runtime/vk_queue.c:1331`. Sparse resources disabled; semaphore-only/empty submissions need tests. |
| `vkCreateFence` | Runtime | `src/vulkan/runtime/vk_fence.c:149`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkDestroyFence` | Runtime | `src/vulkan/runtime/vk_fence.c:189`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkResetFences` | Runtime | `src/vulkan/runtime/vk_fence.c:203`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkGetFenceStatus` | Runtime | `src/vulkan/runtime/vk_fence.c:230`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkWaitForFences` | Runtime | `src/vulkan/runtime/vk_fence.c:250`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkCreateSemaphore` | Runtime | `src/vulkan/runtime/vk_semaphore.c:136`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkDestroySemaphore` | Runtime | `src/vulkan/runtime/vk_semaphore.c:233`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkCreateQueryPool` | Missing | No driver or linked common implementation. |
| `vkDestroyQueryPool` | Missing | No driver or linked common implementation. |
| `vkGetQueryPoolResults` | Missing | No driver or linked common implementation. |
| `vkCreateBuffer` | Radish | `src/freedreno/radish/rd_memory.c:91`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkDestroyBuffer` | Radish | `src/freedreno/radish/rd_memory.c:106`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkCreateImage` | Missing | No driver or linked common implementation. |
| `vkDestroyImage` | Missing | No driver or linked common implementation. |
| `vkGetImageSubresourceLayout` | Images missing | `src/vulkan/runtime/vk_image.c:218`. Common path does not supply absent image allocation/layout support. |
| `vkCreateImageView` | Missing | No driver or linked common implementation. |
| `vkDestroyImageView` | Missing | No driver or linked common implementation. |
| `vkCreateCommandPool` | Runtime | `src/vulkan/runtime/vk_command_pool.c:100`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkDestroyCommandPool` | Runtime | `src/vulkan/runtime/vk_command_pool.c:126`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkResetCommandPool` | Runtime | `src/vulkan/runtime/vk_command_pool.c:141`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkAllocateCommandBuffers` | Runtime | `src/vulkan/runtime/vk_command_pool.c:204`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkFreeCommandBuffers` | Runtime | `src/vulkan/runtime/vk_command_pool.c:242`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkBeginCommandBuffer` | Radish | `src/freedreno/radish/rd_command.c:61`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkEndCommandBuffer` | Radish | `src/freedreno/radish/rd_command.c:70`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkResetCommandBuffer` | Runtime | `src/vulkan/runtime/vk_command_buffer.c:145`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkCmdCopyBuffer` | Radish / partial | `src/freedreno/radish/rd_transfer.c:16`. Word-aligned copies only; legal byte copies rejected in audit snapshot. |
| `vkCmdCopyImage` | Images missing | `src/vulkan/runtime/vk_cmd_copy.c:65`. Only common wrappers; image/driver copy path absent. |
| `vkCmdCopyBufferToImage` | Images missing | `src/vulkan/runtime/vk_cmd_copy.c:107`. Only common wrappers; image/driver copy path absent. |
| `vkCmdCopyImageToBuffer` | Images missing | `src/vulkan/runtime/vk_cmd_copy.c:148`. Only common wrappers; image/driver copy path absent. |
| `vkCmdUpdateBuffer` | Radish | `src/freedreno/radish/rd_transfer.c:54`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkCmdFillBuffer` | Radish | `src/freedreno/radish/rd_transfer.c:37`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkCmdPipelineBarrier` | Runtime → Radish / partial | `src/vulkan/runtime/vk_synchronization.c:199`. Converts to CmdPipelineBarrier2; buffer dependencies rely on serial draining; images fail. |
| `vkCmdBeginQuery` | Broken chain | `src/vulkan/runtime/vk_query_pool.c:109`. Common query wrappers do not provide query objects/driver execution. |
| `vkCmdEndQuery` | Broken chain | `src/vulkan/runtime/vk_query_pool.c:122`. Common query wrappers do not provide query objects/driver execution. |
| `vkCmdResetQueryPool` | Missing | No driver or linked common implementation. |
| `vkCmdWriteTimestamp` | Broken chain / disabled | `src/vulkan/runtime/vk_synchronization.c:127`. Wrapper calls absent CmdWriteTimestamp2; queue timestamps disabled. |
| `vkCmdCopyQueryPoolResults` | Broken chain | `src/vulkan/runtime/vk_command_buffer.c:983`. No query objects or downstream copy-results implementation. |
| `vkCmdExecuteCommands` | Blocked | `src/vulkan/runtime/vk_command_buffer.c:157`. Runtime replay exists, but Radish rejects all secondary command allocations. |
| `vkCreateEvent` | Missing | No driver or linked common implementation. |
| `vkDestroyEvent` | Missing | No driver or linked common implementation. |
| `vkGetEventStatus` | Missing | No driver or linked common implementation. |
| `vkSetEvent` | Missing | No driver or linked common implementation. |
| `vkResetEvent` | Missing | No driver or linked common implementation. |
| `vkCreateBufferView` | Missing | No driver or linked common implementation. |
| `vkDestroyBufferView` | Missing | No driver or linked common implementation. |
| `vkCreateShaderModule` | Runtime | `src/vulkan/runtime/vk_shader_module.c:55`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkDestroyShaderModule` | Runtime | `src/vulkan/runtime/vk_shader_module.c:102`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkCreatePipelineCache` | Runtime | `src/vulkan/runtime/vk_pipeline_cache.c:694`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkDestroyPipelineCache` | Runtime | `src/vulkan/runtime/vk_pipeline_cache.c:716`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkGetPipelineCacheData` | Runtime | `src/vulkan/runtime/vk_pipeline_cache.c:730`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkMergePipelineCaches` | Runtime | `src/vulkan/runtime/vk_pipeline_cache.c:813`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkCreateComputePipelines` | Radish / partial | `src/freedreno/radish/rd_pipeline.c:171`. Only flags=0; rejects unsupported resource classes, spills and constant data. |
| `vkDestroyPipeline` | Radish | `src/freedreno/radish/rd_pipeline.c:207`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkCreatePipelineLayout` | Runtime | `src/vulkan/runtime/vk_pipeline_layout.c:110`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkDestroyPipelineLayout` | Runtime | `src/vulkan/runtime/vk_pipeline_layout.c:143`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkCreateSampler` | Missing | No driver or linked common implementation. |
| `vkDestroySampler` | Missing | No driver or linked common implementation. |
| `vkCreateDescriptorSetLayout` | Radish / partial | `src/freedreno/radish/rd_descriptor.c:4`. Only storage-buffer type, at most 32 total descriptors; canonical binding order. |
| `vkDestroyDescriptorSetLayout` | Runtime | `src/vulkan/runtime/vk_descriptor_set_layout.c:89`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkCreateDescriptorPool` | Radish / partial | `src/freedreno/radish/rd_descriptor.c:62`. Storage-buffer type only; callbacks/budget accounting tested narrowly. |
| `vkDestroyDescriptorPool` | Radish | `src/freedreno/radish/rd_descriptor.c:110`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkResetDescriptorPool` | Radish | `src/freedreno/radish/rd_descriptor.c:99`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkAllocateDescriptorSets` | Radish | `src/freedreno/radish/rd_descriptor.c:122`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkFreeDescriptorSets` | Radish | `src/freedreno/radish/rd_descriptor.c:159`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkUpdateDescriptorSets` | Radish | `src/freedreno/radish/rd_descriptor.c:170`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkCmdBindPipeline` | Radish | `src/freedreno/radish/rd_command.c:77`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkCmdBindDescriptorSets` | Radish / partial | `src/freedreno/radish/rd_command.c:89`. Compute only; dynamic offsets rejected. |
| `vkCmdClearColorImage` | Missing | No driver or linked common implementation. |
| `vkCmdDispatch` | Radish | `src/freedreno/radish/rd_command.c:116`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkCmdDispatchIndirect` | Broken chain | `src/vulkan/runtime/vk_command_buffer.c:765`. Wrapper calls missing CmdDispatchIndirect2KHR. |
| `vkCmdSetEvent` | Broken chain | `src/vulkan/runtime/vk_synchronization.c:271`. Wrapper calls absent CmdSetEvent2; event objects absent. |
| `vkCmdResetEvent` | Broken chain | `src/vulkan/runtime/vk_synchronization.c:294`. Wrapper calls absent CmdResetEvent2; event objects absent. |
| `vkCmdWaitEvents` | Broken chain | `src/vulkan/runtime/vk_synchronization.c:308`. Wrapper calls absent CmdWaitEvents2; event objects absent. |
| `vkCmdPushConstants` | Radish | `src/freedreno/radish/rd_command.c:104`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkCreateGraphicsPipelines` | Graphics scope | `src/vulkan/runtime/vk_pipeline.c:2245`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCreateFramebuffer` | Graphics scope | `src/vulkan/runtime/vk_framebuffer.c:30`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkDestroyFramebuffer` | Graphics scope | `src/vulkan/runtime/vk_framebuffer.c:72`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCreateRenderPass` | Graphics scope | `src/vulkan/runtime/vk_render_pass.c:67`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkDestroyRenderPass` | Graphics scope | `src/vulkan/runtime/vk_render_pass.c:1279`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkGetRenderAreaGranularity` | Graphics scope | `src/vulkan/runtime/vk_render_pass.c:1293`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdSetViewport` | Graphics scope | `src/vulkan/runtime/vk_graphics_state.c:2593`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdSetScissor` | Graphics scope | `src/vulkan/runtime/vk_graphics_state.c:2618`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdSetLineWidth` | Graphics scope | `src/vulkan/runtime/vk_graphics_state.c:2820`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdSetDepthBias` | Graphics scope | `src/vulkan/runtime/vk_graphics_state.c:2801`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdSetBlendConstants` | Graphics scope | `src/vulkan/runtime/vk_graphics_state.c:3244`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdSetDepthBounds` | Graphics scope | `src/vulkan/runtime/vk_graphics_state.c:3015`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdSetStencilCompareMask` | Graphics scope | `src/vulkan/runtime/vk_graphics_state.c:3066`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdSetStencilWriteMask` | Graphics scope | `src/vulkan/runtime/vk_graphics_state.c:3087`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdSetStencilReference` | Graphics scope | `src/vulkan/runtime/vk_graphics_state.c:3108`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdBindIndexBuffer` | Graphics scope | `src/vulkan/runtime/vk_command_buffer.c:188`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdBindVertexBuffers` | Graphics scope | `src/vulkan/runtime/vk_command_buffer.c:173`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdDraw` | Graphics scope | No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdDrawIndexed` | Graphics scope | No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdDrawIndirect` | Graphics scope | `src/vulkan/runtime/vk_command_buffer.c:435`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdDrawIndexedIndirect` | Graphics scope | `src/vulkan/runtime/vk_command_buffer.c:458`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdBlitImage` | Graphics scope | `src/vulkan/runtime/vk_cmd_copy.c:189`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdClearDepthStencilImage` | Graphics scope | No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdClearAttachments` | Graphics scope | No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdResolveImage` | Graphics scope | `src/vulkan/runtime/vk_cmd_copy.c:238`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdBeginRenderPass` | Graphics scope | `src/vulkan/runtime/vk_render_pass.c:249`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdNextSubpass` | Graphics scope | `src/vulkan/runtime/vk_render_pass.c:277`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |
| `vkCmdEndRenderPass` | Graphics scope | `src/vulkan/runtime/vk_render_pass.c:265`. No driver graphics pipeline/rendering implementation; not covered by inference goal. |

### Vulkan 1.1 additions (28 commands)

| Command | Status | Source / qualification |
| --- | --- | --- |
| `vkEnumerateInstanceVersion` | Radish | `src/freedreno/radish/rd_instance.c:14`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkBindBufferMemory2` | Radish | `src/freedreno/radish/rd_memory.c:126`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkBindImageMemory2` | Missing | No driver or linked common implementation. |
| `vkGetDeviceGroupPeerMemoryFeatures` | Runtime / single device | `src/vulkan/runtime/vk_device.c:568`. Single-device peer flags; no multi-GPU promise. |
| `vkCmdSetDeviceMask` | Runtime / single device | `src/vulkan/runtime/vk_command_buffer.c:217`. Assert/no-op for mask 1, matching one-device group. |
| `vkEnumeratePhysicalDeviceGroups` | Runtime / single device | `src/vulkan/runtime/vk_instance.c:537`. Single physical device per group. |
| `vkGetImageMemoryRequirements2` | Missing | No driver or linked common implementation. |
| `vkGetBufferMemoryRequirements2` | Radish / partial | `src/freedreno/radish/rd_memory.c:116`. Core output written; dedicated-requirements pNext output ignored. |
| `vkGetImageSparseMemoryRequirements2` | Missing | No driver or linked common implementation. |
| `vkGetPhysicalDeviceFeatures2` | Runtime / zero | `build/src/vulkan/runtime/vk_physical_device_features.c:9390`. All supported feature bits false; generated pNext handling. |
| `vkGetPhysicalDeviceProperties2` | Runtime / incomplete | `build/src/vulkan/runtime/vk_physical_device_properties.c:33`. Generated query; 1.1 properties largely zero. |
| `vkGetPhysicalDeviceFormatProperties2` | Radish / empty | `src/freedreno/radish/rd_instance.c:205`. All format feature masks zero. |
| `vkGetPhysicalDeviceImageFormatProperties2` | Radish / unsupported | `src/freedreno/radish/rd_instance.c:220`. Always VK_ERROR_FORMAT_NOT_SUPPORTED; no image path. |
| `vkGetPhysicalDeviceQueueFamilyProperties2` | Radish | `src/freedreno/radish/rd_instance.c:175`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkGetPhysicalDeviceMemoryProperties2` | Radish | `src/freedreno/radish/rd_instance.c:189`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkGetPhysicalDeviceSparseImageFormatProperties2` | Radish | `src/freedreno/radish/rd_instance.c:228`. Driver implementation; hardware evidence remains scoped to existing tests. |
| `vkTrimCommandPool` | Runtime | `src/vulkan/runtime/vk_command_pool.c:267`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkGetDeviceQueue2` | Runtime | `src/vulkan/runtime/vk_device.c:504`. Common implementation present; untested edge/lifetime behavior remains unproven. |
| `vkGetPhysicalDeviceExternalBufferProperties` | Missing | Core1.1 capability query absent; unsupported handles need honest output. |
| `vkGetPhysicalDeviceExternalFenceProperties` | Runtime / untested | `src/vulkan/runtime/vk_fence.c:295`. DRM syncobj-derived capabilities; full handle/API audit still required. |
| `vkGetPhysicalDeviceExternalSemaphoreProperties` | Runtime / untested | `src/vulkan/runtime/vk_semaphore.c:250`. DRM syncobj-derived capabilities; full handle/API audit still required. |
| `vkCmdDispatchBase` | Missing | Required compute behavior for a 1.1 target; no base-group mapping. |
| `vkCreateDescriptorUpdateTemplate` | Runtime / incomplete pair | `src/vulkan/runtime/vk_descriptor_update_template.c:33`. Object creation exists; actual update command missing. |
| `vkDestroyDescriptorUpdateTemplate` | Runtime / incomplete pair | `src/vulkan/runtime/vk_descriptor_update_template.c:95`. Object destruction exists; actual update command missing. |
| `vkUpdateDescriptorSetWithTemplate` | Missing | Core1.1 template-update execution absent. |
| `vkGetDescriptorSetLayoutSupport` | Missing | Core1.1 layout-support query absent. |
| `vkCreateSamplerYcbcrConversion` | Runtime / feature off | `src/vulkan/runtime/vk_ycbcr_conversion.c:35`. Feature false; no valid conversion-enabled use claimed. |
| `vkDestroySamplerYcbcrConversion` | Runtime / feature off | `src/vulkan/runtime/vk_ycbcr_conversion.c:99`. Feature false; no valid conversion-enabled use claimed. |

## Appendix B: every Vulkan 1.0 limits field

This inventory covers all fields of `VkPhysicalDeviceLimits` in the registry.
Values are from the initial `rd_instance.c` initializer, not an inferred device
dump. Omitted scalar fields are zero, omitted arrays have all-zero components.
The normative [limit table](https://docs.vulkan.org/spec/latest/chapters/limits.html#limits-required)
contains feature-conditional and graphics-specific requirements. A zero optional
limit is not automatically a violation; a nonzero advertised limit is not
automatically implemented. The disposition column avoids both mistakes.

| Limit | Current reported value | Disposition |
| --- | --- | --- |
| `maxImageDimension1D` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxImageDimension2D` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxImageDimension3D` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxImageDimensionCube` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxImageArrayLayers` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxTexelBufferElements` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxUniformBufferRange` | 16384 | Nonzero property, but UBO implementation absent. |
| `maxStorageBufferRange` | 134217728 | Advertised; small SSBO offsets/readback tested; 128 MiB boundary unproven. |
| `maxPushConstantsSize` | 128 | Backing storage 128 B; tests exercise 8 B, complete layouts/boundary unproven. |
| `maxMemoryAllocationCount` | 4096 | Populated memory property; verify contract/edge cases. |
| `maxSamplerAllocationCount` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `bufferImageGranularity` | 0 | Unfilled memory/priority/alignment property; audit nonzero/power-of-two requirements before complete core claims. |
| `sparseAddressSpaceSize` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxBoundDescriptorSets` | 4 | 4-set implementation bound; only small maps tested. |
| `maxPerStageDescriptorSamplers` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxPerStageDescriptorUniformBuffers` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxPerStageDescriptorStorageBuffers` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxPerStageDescriptorSampledImages` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxPerStageDescriptorStorageImages` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxPerStageDescriptorInputAttachments` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxPerStageResources` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxDescriptorSetSamplers` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxDescriptorSetUniformBuffers` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxDescriptorSetUniformBuffersDynamic` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxDescriptorSetStorageBuffers` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxDescriptorSetStorageBuffersDynamic` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxDescriptorSetSampledImages` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxDescriptorSetStorageImages` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxDescriptorSetInputAttachments` | 0 | Descriptor contract incomplete: fixed SSBO path exists but counts are zero. |
| `maxVertexInputAttributes` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxVertexInputBindings` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxVertexInputAttributeOffset` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxVertexInputBindingStride` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxVertexOutputComponents` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxTessellationGenerationLevel` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxTessellationPatchSize` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxTessellationControlPerVertexInputComponents` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxTessellationControlPerVertexOutputComponents` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxTessellationControlPerPatchOutputComponents` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxTessellationControlTotalOutputComponents` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxTessellationEvaluationInputComponents` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxTessellationEvaluationOutputComponents` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxGeometryShaderInvocations` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxGeometryInputComponents` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxGeometryOutputComponents` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxGeometryOutputVertices` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxGeometryTotalOutputComponents` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxFragmentInputComponents` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxFragmentOutputAttachments` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxFragmentDualSrcAttachments` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxFragmentCombinedOutputResources` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxComputeSharedMemorySize` | 32768 | Advertised, rejected in initial snapshot; later 32 KiB hardware test faults. |
| `maxComputeWorkGroupCount` | (65535,65535,65535) | Advertised; only narrow local shapes/counts validated, not entire bound. |
| `maxComputeWorkGroupInvocations` | 128 | Advertised; only narrow local shapes/counts validated, not entire bound. |
| `maxComputeWorkGroupSize` | (128,128,64) | Advertised; only narrow local shapes/counts validated, not entire bound. |
| `subPixelPrecisionBits` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `subTexelPrecisionBits` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `mipmapPrecisionBits` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxDrawIndexedIndexValue` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxDrawIndirectCount` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxSamplerLodBias` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxSamplerAnisotropy` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxViewports` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxViewportDimensions` | all components 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `viewportBoundsRange` | all components 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `viewportSubPixelBits` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `minMemoryMapAlignment` | 64 | Populated memory property; verify contract/edge cases. |
| `minTexelBufferOffsetAlignment` | 0 | Unfilled memory/priority/alignment property; audit nonzero/power-of-two requirements before complete core claims. |
| `minUniformBufferOffsetAlignment` | 16 | Populated memory property; verify contract/edge cases. |
| `minStorageBufferOffsetAlignment` | 16 | Populated memory property; verify contract/edge cases. |
| `minTexelOffset` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxTexelOffset` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `minTexelGatherOffset` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxTexelGatherOffset` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `minInterpolationOffset` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxInterpolationOffset` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `subPixelInterpolationOffsetBits` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxFramebufferWidth` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxFramebufferHeight` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxFramebufferLayers` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `framebufferColorSampleCounts` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `framebufferDepthSampleCounts` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `framebufferStencilSampleCounts` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `framebufferNoAttachmentsSampleCounts` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxColorAttachments` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `sampledImageColorSampleCounts` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `sampledImageIntegerSampleCounts` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `sampledImageDepthSampleCounts` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `sampledImageStencilSampleCounts` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `storageImageSampleCounts` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `maxSampleMaskWords` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `timestampComputeAndGraphics` | 0 | Queue timestamp support disabled; do not use GPU timestamps. |
| `timestampPeriod` | 0 | Queue timestamp support disabled; do not use GPU timestamps. |
| `maxClipDistances` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxCullDistances` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `maxCombinedClipAndCullDistances` | 0 | Related optional feature disabled; zero may be appropriate. Graphics/sparse implementation absent. |
| `discreteQueuePriorities` | 0 | Unfilled memory/priority/alignment property; audit nonzero/power-of-two requirements before complete core claims. |
| `pointSizeRange` | all components 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `lineWidthRange` | all components 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `pointSizeGranularity` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `lineWidthGranularity` | 0 | Image/texel/graphics capability absent or property unfilled; full graphics requirement backlog. |
| `strictLines` | 0 | False Boolean; graphics path absent, not independent proof of a violation. |
| `standardSampleLocations` | 0 | False Boolean; graphics path absent, not independent proof of a violation. |
| `optimalBufferCopyOffsetAlignment` | 0 | Unfilled memory/priority/alignment property; audit nonzero/power-of-two requirements before complete core claims. |
| `optimalBufferCopyRowPitchAlignment` | 0 | Unfilled memory/priority/alignment property; audit nonzero/power-of-two requirements before complete core claims. |
| `nonCoherentAtomSize` | 64 | Populated memory property; verify contract/edge cases. |

Inventory count: **106 fields**. All 1.0 limit fields are accounted
for above. Sparse properties are all false because sparse features are false.

### Other basic properties

`apiVersion=1.0`, `driverVersion=1`, vendor `0x5143`, device `506`, integrated-GPU
type and an experimental name are populated. `pipelineCacheUUID` is all zero;
cache serialization identity needs an ABI/driver key before persistent use.
One compute/transfer family with one queue is exposed; `timestampValidBits=0`,
image-transfer granularity `(1,1,1)`. One WC host-coherent/device-local memory type
and a device-local heap of half host physical RAM are reported. The heap is a
capacity estimate, not reserved model memory or a guarantee allocations fit
beside the live services. Memory-pressure verification remains required.

### Every 1.1 property

The aggregate `VkPhysicalDeviceVulkan11Properties` structure itself was added in
1.2; its members are inventoried here as shorthand for the original1.1
individual query structures. Do not require a 1.1-only application to use the
aggregate.

| Property | Current value | Disposition |
| --- | --- | --- |
| `deviceUUID` | all zero | Stable per-device identity not implemented. |
| `driverUUID` | all zero | Stable driver identity not implemented. |
| `deviceLUID` | all zero | Acceptable placeholder when deviceLUIDValid=false. |
| `deviceNodeMask` | 0 | Meaningful with valid LUID; no multi-node promise. |
| `deviceLUIDValid` | false | No Windows LUID claimed. |
| `subgroupSize` | 32 | Size alone does not establish subgroup operation support. |
| `subgroupSupportedStages` | 0 | Compute stage required for 1.1 compute target; missing. |
| `subgroupSupportedOperations` | 0 | BASIC required for 1.1 compute target; missing. |
| `subgroupQuadOperationsInAllStages` | false | QUAD not supported; false is conservative. |
| `pointClippingBehavior` | enum 0 | Graphics absent; no behavior validated. |
| `maxMultiviewViewCount` | 0 | Multiview false; full graphics 1.1 backlog. |
| `maxMultiviewInstanceIndex` | 0 | Multiview false; full graphics 1.1 backlog. |
| `protectedNoFault` | false | Protected memory feature false. |
| `maxPerSetDescriptors` | 0 | Layout-support/core 1.1 property missing. |
| `maxMemoryAllocationSize` | 0 | Core1.1 allocation-size property missing; allocation code accepts below 4 GiB. |

## Appendix C: complete feature-bit disposition

All 55 Vulkan 1.0 feature bits below are **false**, by the NULL supported-feature
table passed to Mesa initialization. This list is exhaustive; none should be
inferred true from compiler ISA availability. For a full graphics implementation,
robustBufferAccess is a required missing feature. Other disabled bits remain
conditional/optional as discussed above.

- `robustBufferAccess`, `fullDrawIndexUint32`, `imageCubeArray`, `independentBlend`, `geometryShader`.
- `tessellationShader`, `sampleRateShading`, `dualSrcBlend`, `logicOp`, `multiDrawIndirect`.
- `drawIndirectFirstInstance`, `depthClamp`, `depthBiasClamp`, `fillModeNonSolid`, `depthBounds`.
- `wideLines`, `largePoints`, `alphaToOne`, `multiViewport`, `samplerAnisotropy`.
- `textureCompressionETC2`, `textureCompressionASTC_LDR`, `textureCompressionBC`, `occlusionQueryPrecise`, `pipelineStatisticsQuery`.
- `vertexPipelineStoresAndAtomics`, `fragmentStoresAndAtomics`, `shaderTessellationAndGeometryPointSize`, `shaderImageGatherExtended`, `shaderStorageImageExtendedFormats`.
- `shaderStorageImageMultisample`, `shaderStorageImageReadWithoutFormat`, `shaderStorageImageWriteWithoutFormat`, `shaderUniformBufferArrayDynamicIndexing`, `shaderSampledImageArrayDynamicIndexing`.
- `shaderStorageBufferArrayDynamicIndexing`, `shaderStorageImageArrayDynamicIndexing`, `shaderClipDistance`, `shaderCullDistance`, `shaderFloat64`.
- `shaderInt64`, `shaderInt16`, `shaderResourceResidency`, `shaderResourceMinLod`, `sparseBinding`.
- `sparseResidencyBuffer`, `sparseResidencyImage2D`, `sparseResidencyImage3D`, `sparseResidency2Samples`, `sparseResidency4Samples`.
- `sparseResidency8Samples`, `sparseResidency16Samples`, `sparseResidencyAliased`, `variableMultisampleRate`, `inheritedQueries`.

All 12 Vulkan 1.1 feature bits are likewise **false**. The aggregate
`VkPhysicalDeviceVulkan11Features` query structure is a 1.2 convenience; original
1.1 feature structures carry the same bits.

| Bits | Status / implications |
| --- | --- |
| `storageBuffer16BitAccess`, `uniformAndStorageBuffer16BitAccess`, `storagePushConstant16`, `storageInputOutput16` | Disabled; optional 1.1 storage paths. Uniform-and-storage would imply storage-buffer support if enabled. |
| `multiview`, `multiviewGeometryShader`, `multiviewTessellationShader` | Disabled; complete graphics 1.1 needs multiview; other combinations remain separate. |
| `variablePointersStorageBuffer`, `variablePointers` | Disabled; do not advertise the named extension without its required storage-buffer path. |
| `protectedMemory` | Disabled; no protected queues/memory claim. |
| `samplerYcbcrConversion` | Disabled; no conversion-enabled image/sampler claim. |
| `shaderDrawParameters` | Disabled; no named draw-parameters extension, no graphics claim. |
