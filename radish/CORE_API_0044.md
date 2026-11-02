# Core compute implementation: checkpoint 0044

## 2026-11-02: full native resource capacity still faults

A small core-compute consumer with sixteen genuinely live sampled textures, two storage-image size queries and two storage-texel-buffer size queries compiles successfully but faults on execution. The current `c1c3a520` driver emits native texture slots0–15 and query aliases16–19; both GETSIZE and GETBUF use indirect `s2en` instructions. The shader has only five native instruction units,8 full/2 half registers, and no shared/private storage or software sampling fallback. Its first round returns an incorrect first output and ten printed GPU read-fault messages. [Exact payload, state and failure evidence](results/core-api-0044-native-capacity-fault.json).

The runner stopped after that round, drained its unit and retained the full native/BO/descriptor log. Both services remained active; the service used616ms,6.9MiB and no swap. The earlier cube/subword passes remain valid for their smaller native inventory, but the required full-capacity combination is now a concrete unresolved failure. The first mismatch does not identify which instruction faulted. All further GPU execution is held while source diagnosis continues, including irregular barriers, oversized images and full-model replay.

The exact failing consumer is retained as `tests/core_native_capacity.c` and `tests/shaders/core-native-capacity.comp`, using the existing frozen resource utilities. It is not added to the default device batch. This checkpoint changes no driver code, advertised limits or features.

## 2026-11-02: corrected cube and subword consumers pass

The child pipeline now rebuilds its native texture inventory after software lowering, preserving the original application and raw-address indices. The unchanged cube consumer passes all 6,120 output words and full allocation guards; the subword consumer then passes all 5,950 words and guards. Cube checks cover six faces, a nonzero base layer, seams/corners, spatial and mip filtering, explicit gradients and gather. Subword checks cover R8_UINT, packed R5G6B5 and R16_SFLOAT decoding/filtering. Valid descriptor re-recording, cache reuse or format-key change, and current BO data checks remain intact. [Exact results and limits](results/core-api-0044-cube-subword.json).

The measured private ICD `c1c3a520` used native and read-only BO/descriptor diagnostics. Its cube child emits seven live native textures and image-size aliases at 7/8, rather than the stale 16/17 inventory. Both consumers completed in one 3.848-second service window, 15.7 MiB peak and zero swap, with no new GPU faults and both services active. The preceding failures below remain preserved. This resolves these reproduced cases; it does not establish the hardware fault mechanism or qualify the legal combination of sixteen genuinely live native textures plus image aliases. That capacity obligation remains open.

The fifteenth increment is `0044-radish-cube-native-resources-wip.patch`. It also retains the independently reviewed sampler-count correction, default-off resource diagnostics, and a version-scoped older-core depth/stencil multisample-copy condition change with no sample-data qualification. The existing builder/runner uses `--suite array` for the cube consumer and adds `--suite subword`. Oversized execution, broader barriers and full inference regression remain subsequent functional checks. Performance work remains paused.

## 2026-11-02: cube experiment faults; qualification paused

The private cube consumer failed its first round and generated ten GPU memory faults. A separate build containing only the reviewed sampler-count correction reproduced the same output mismatch and fault addresses. The corrected run retained the full native trace; it completed cleanup in 2.082 seconds, used 15.4 MiB with no swap, and left both services active. Subword execution was not attempted. [Exact payloads, failure evidence and limits](results/core-api-0044-cube-fault.json).

The sampler-count error is real, but fixing it did not resolve the fault. Cube and count-correction candidates remain unqualified. Commit `7a8b4bf` remains the last qualified source checkpoint, including the sampled-MS results below. Further GPU qualification, large-image admission and the full inference regression are held while source diagnosis continues. Performance work remains paused.

## 2026-11-02: sampled multisample paths execute

ICDa7d6f917 passes two 120-record rounds with 18 exact words each (4,320 total), seven image allocation guards and the output allocation guard. R32_UINT/R16_UNORM images exercise per-layer clear, offset copy, native fetch at all four sample indices, and size/count queries across two layers. D16/D32/S8 checks cover creation and size/count only. Cleared samples have equal values, so this does not prove distinct sample-address selection or depth/stencil sample values. Service630ms,5.8MiB,zero swap; no new GPU faults and both services active. [Exact source, consumer and limits](results/core-api-0044-sampled-ms.json).

The unchanged, locally modified CTS harness reruns only `dEQP-VK.info.device_properties`: exactly three sampled-image count failures disappear,34→31, with no new failures. The full case still fails; its complete report and all remaining failures are retained without waivers. No complete Vulkan or CTS conformance claim follows from these results.

Apply the fourteenth increment `0044-radish-sampled-ms-wip.patch` after filtered-array. The existing builder adds `multisample`, selected with `device-core-compute.py ... --suite multisample`. Native one/four-sample layout and descriptors, sample-aware color transfers, and stencil formats are implemented; storage multisample remains unsupported. Source for cube sampling is being built separately and has no execution evidence in this checkpoint. Performance experiments remain paused.

## 2026-11-02: filtered array consumer passes

The bounded software array path now passes two 85-record rounds with 29 exact output words per record: 4,930 words total, plus all ten image and nine buffer allocation guards. The R32_SFLOAT array checks cover repeat/edge/mirror/border addressing, mag/min selection, spatial and mip linear filtering, explicit and gradient LOD with bias, layer rounding, and all four gather components. The second round changes image buffers and reuses the same cached shader after legal descriptor updates and fresh command recording. The prior 22 fields remain checked. [Paired source and result](results/core-api-0044-filtered-array.json).

ICD521f48bb completed in 930ms with 13.5MiB peak memory, no swap, no new GPU faults, and both services active. The helper uses `RADISH_SW_TEXEL_FETCH=1`; the actual specialized child has 88 native instruction units. Its lazy compilation occurs before first dispatch and is separate from the base pipeline's compile-only check. Emitted Vulkan1.0 SPIR-V validates and uses explicit LOD/gradients and ordinary base-mip gather, with no implicit derivatives or optional gather extensions.

Apply the thirteenth increment `0044-radish-filtered-array-wip.patch` after compute-properties and use the existing `--suite array` route. This qualifies the selected small array cases, not cubes, oversized array allocation, every format or full sampling conformance. Multisample source is being built separately; no multisample execution result is included here. Performance experiments remain paused.

## 2026-11-02: three property-limit failures corrected

The same locally modified CTS harness reran only its failed device-properties case on ICD3bc37c79. Exactly three failures disappear: minimum/maximum texture offsets and discrete queue priorities;37 failure messages become34 with no additions. The full test still fails and its report is retained unchanged. Service451ms,14.7MiB,zero swap; no new GPU faults and both services active. [Comparison and full failed report](results/core-api-0044-compute-properties.json).

The twelfth increment `0044-radish-compute-properties-wip.patch` reports the existing native−8/+7 texture-offset range and retains two logical application priority hints for the sole queue. Both hints keep the same default kernel priority; this makes no cross-device scheduling or global-priority claim. The earlier direct mapping to global MSM priorities was rejected before execution/publication because it could starve another device. Sampled-image multisample/stencil behavior and other compute gaps remain; graphics-specific requirements are outside this campaign, not silently removed from CTS results.

## 2026-11-02: corrected descriptor ordering passes

The replacement run on ICD48ffa7ce uses completed fence → command-pool reset → descriptor update → fresh record/bind → submit/wait. Both85-record resource rounds pass all22 fields (3740 words) and full8 image/9 buffer allocation guards. The unchanged tests cover dynamic buffers, compatible layout lifetime, mutable/fullsplit images, color clear/copy, D16/D32 sampling/comparison and depth copy. Four robust-buffer spans1/3/12/16 each pass40 output words and all five allocation guards through the ordinary public robustBufferAccess feature request. Service1.528s,13.8MiB,zero swap; no new GPU faults and both services active. [Exact replacement result](results/core-api-0044-valid-order.json).

The resource run also qualifies the bounded private array-fetch path: two mips/two layers, a different image BO between recordings, reuse of the same specialized child and exact integer/default-alpha outputs. It forces `RADISH_SW_TEXEL_FETCH=1`; filtered/cube fallback and oversized image creation remain incomplete. The eleventh increment `0044-radish-array-fetch-wip.patch` adds this real LDG-consuming path. Native compile-only checks the base pipeline; descriptor-specialized code compiles lazily before first dispatch. Build success alone does not qualify every later specialization.

The published resource and RBA helpers now use the corrected order. The existing builder also produces `array`, selected with `device-core-compute.py ... --suite array`; `--suite robust` selects public coreRBA. The40-command ledger again has representative valid-use evidence across the retained independent suites and these corrected resource paths, not complete command semantics or conformance. Historical invalid-order captures and the erratum below remain; only replacement coverage is restored. API5/templates, events/queries, BASIC, private/constant and historical reduction were independently checked as unaffected.

## 2026-11-02: descriptor-order qualification correction

The resource and robust-buffer helpers updated ordinary descriptor sets after recording command buffers and before submission. Without update-after-bind binding flags, those updates invalidate the command buffers. Their passing outputs and guard checks remain recorded as implementation observations, but the affected executions do not establish valid Vulkan-use qualification. This includes the resource/depth/mutable/fullsplit and robust-buffer claims below. Corrected helpers will update descriptors before recording, then reset/re-record after each completed round while retaining all numerical and allocation checks. No optional binding feature is being enabled to bypass the rule.

The40-path ledger records representative execution observations, not40 fully qualified command semantics. Independent API5, event/query, subgroup, private/constant and historical reduction results are not automatically affected; their descriptor ordering is checked separately. Frozen captures are preserved and result records carry this erratum. The existing public resource/RBA helpers are pending the same correction.

The original CTS initialization crash is separately resolved by a local harness correction that retains actually advertised promoted instance extensions. The unchanged five test cases then yield4 passes and1 device-property-limit failure. The full report remains failed, including graphics limits outside this compute campaign and actionable compute texture-offset, sampled-image and queue-priority gaps. This is locally modified-harness evidence, not official conformance. [Results and full failed report](results/core-api-0044-cts-info.json).

## 2026-11-02: native depth consumers pass

ICDa601f68d passes D16/D32 nearest normalized sampling, LESS_OR_EQUAL comparison at reference0.5, host PREINITIALIZED source visibility and a same-format D32 depth-image copy consumed by native sampling. Both85-record rounds pass all20 fields per record (3400 words total) and full6 image/9 buffer allocation guards. The existing UBO, dynamic-offset, late-update, mutable/fullsplit, color transfer and typed-texel checks remain intact. Service631ms,7.7MiB,zero swap; no new GPU faults and both services active. [Exact paired result](results/core-api-0044-depth.json).

Apply the tenth increment `0044-radish-image-maintenance-wip.patch` after barrier-image-size. Additional source/build work supplies EXTENDED_USAGE and 2D_ARRAY_COMPATIBLE image contracts, the StorageBuffer-storage-class and relaxed-block-layout KHR backports, and an internal gen5 raw global-load primitive. Those newly added paths have not all been exercised by this depth consumer; raw global loads are not yet consumed in this payload and do not close large-array sampling. The published resource helper now includes the exact depth checks under the existing `--suite resources` route.

Other depth compare/filter modes, stencil, large sampled arrays/cubes and general workgroup/CTS coverage remain incomplete. The API stays1.0. The pinned CTS startup source has a mixed instance/device-version extension-routing defect under investigation; no CTS case has passed. Performance experiments remain paused.

## 2026-11-02: historical barrier reduction now passes

The exact original ncnn width1/C3 reduction passes for logical local sizes32 and64 (stride32), with all three output means exact. The compiler now maps these even1D workgroup-barrier shapes to physical16×2 and32×2 while reconstructing the original logical IDs. Both use the unchanged residency guards,1KiB shared memory,16 full/13 half registers and no private memory. The measured ICD is934665fe; service2.901s,63MiB, no new GPU faults and both live services active. This closes the reproduced historical consumer failure, not all odd/general legal-workgroup behavior. [Paired source and result](results/core-api-0044-barrier-repack.json).

Apply the ninth increment `0044-radish-barrier-image-size-wip.patch` after device-group-robustness. It also builds checked larger-image allocation/layout support, which has no large-image execution evidence here; the sampled-array/cube native-stride gap remains open. The existing ncnn `--reduce-compile` and `--reduce-smoke` paths retain their original inputs and oracle. The measured private helper adds actual logical/physical pipeline assertions; compilation preceded both tensor runs, with32 checked before64 and unchanged fault/service boundaries.

The existing CTS binary was attempted with five information/property cases but crashed during context initialization before running any case. A bounded loader trace confirms Radish was loaded and enumerated before the null-address SIGSEGV; the specific initialization defect remains under investigation. This is an unresolved compatibility issue, not a CTS pass or a proven environment-only failure. Full compute semantics and conformance remain incomplete; performance work is paused.

## 2026-11-02: public singleton groups and core robust buffers pass

ICD448cc118 passes the three targeted suites: two4,160-invocation normal/noopt pipelines through public device-group dispatch/mask aliases with a DeviceIndex-zero oracle; two resource rounds with a full-image singleton split bind and compatible R32_SFLOAT-to-UINT storage view; and all four robust-buffer cases with1/3/12/16-byte ranges, dynamic offsets, partial vectors, writes/add/CAS,40001-element texel buffers and exact backing guards. Service2.005s,14.3MiB,zero swap; no new faults and both services active.

`VK_KHR_device_group` is now exposed for the actual single device. The peer-memory alias is checked but not called: Vulkan requires distinct local/remote indices, which a singleton cannot supply. Core `robustBufferAccess` is promoted after source review and these checks. The measured helper locally set the supported-feature field before ordinary CreateDevice enablement; the published helper requests the public feature directly. This property/helper adaptation is host-checked, not a separate device result. RBA2, null descriptors and general image robustness are not claimed.

Apply the eighth increment `0044-radish-device-group-robustness-wip.patch` after the compute-contract increment. The valid large storage-texel failure was fixed by separating sampler count encoding from native typed-UAV bounds coordinates while retaining the original byte address and software range checks. Earlier compiler and execution failures remain recorded. [Exact artifact/result](results/core-api-0044-group-robustness.json).

Existing `build-core-compute.py` now builds `robust`; use `device-core-compute.py ... --suite robust` for that bounded consumer, or `--suite api5`/`resources` for the group paths. The full compute contract, legal-workgroup behavior, depth consumption and broader CTS remain incomplete. Performance remains paused.

## 2026-11-02: additional compute behavior, robustness still failing

ICD4605a727 passes public singleton device-group creation/enumeration and external-memory capability queries, native resources, five BASIC shapes (including3×5×3), and actual pipeline-owned private/constant storage. The storage shader checks128 exact words while consuming2KiB private storage per invocation and a16KiB constant table. These four suites pass in7.743s,16.1MiB,zero swap; no GPU faults and both services active.

ICDf66ee860 separately passes the same4,160-invocation API5 oracle with normal compilation and `DISABLE_OPTIMIZATION`. The source supplies a per-pipeline compiler path, not only flag acceptance. Instance extensions `VK_KHR_device_group_creation` and `VK_KHR_external_memory_capabilities` are now public; external import/export features remain zero. Device extension `VK_KHR_device_group` remains withheld pending its remaining obligations.

The seventh incremental patch, `0044-radish-compute-contract-wip.patch`, includes these changes, bounds-lowering work and normal error propagation for impossible barrier residency. Robustness is explicitly incomplete and remains unadvertised: after two compiler fixes, ICD846939c6 executes the first bounds case but returns zero for a valid large storage-texel read. Remaining cases are unattempted; the876ms run has no GPU fault. All prior failures remain recorded. Depth consumption, broader workgroup legality and CTS coverage also remain open; performance work is paused.

See [the separate artifact results](results/core-api-0044-compute-contract.json). Apply this patch after the previous six0044 increments. The existing builder now also produces `storage`; `--suite storage` selects it, and `--suite api5` selects the two compiler modes. These portable selections are host-checked adaptations of the recorded private fixed command lists. No complete Vulkan1.1/conformance claim is made.

The latest A506 batch passes API5, events/queries and native resources on ICD
`61bad8227b07355bd174f1d7255ce0a3545ce8562fb006bea1ee626d696c52be`.
Public KHR aliases now exercise descriptor templates, layout support,
buffer/image binding and memory requirements, and sparse-image queries returning
zero requirements. The driver still reports API 1.0.

Subgroup BASIC now passes the four focused cases on a later paired artifact,
ICDbc8ec8e2. The compiler preserves logicalLocalID/Index/WorkgroupSize and maps
BASIC shaders to a1D physical workgroup, avoiding A5xx's shape-dependent physical
packing. Logical32/33/8×8/128 all pass IDs/counts, uniform/divergent election,
shared/SSBO subgroup barriers, atomics and full guards. The717ms run used8.9MiB,
zero swap; no new faults and both services active.

Published source now exposes compute BASIC after that evidence and source review.
The measured helper enabled it locally; the public property-only promotion is
not another execution measurement. Prior failures remain in their separate
records. See [the paired BASIC result](results/core-api-0044-subgroup-basic.json).
This is focused semantic evidence, not full Vulkan1.1 or CTS conformance.

Apply `0044-radish-descriptor-backports-wip.patch` after the three earlier0044
patches. This increment adds1024 logical descriptor storage with statically
referenced element compaction, corrected resource limits, pipeline derivative
and multi-create handling, explicit texture gradients/gather, packed formats,
the four KHR backports and the16-lane subgroup compiler option. Representative
resource checks pass; the full range of those source paths is not qualified.
That earlier artifact left BASIC supported stages/operations zero; the later
subgroup-linear increment supplies the corrected mapping and capability promotion.

The original40 worklist now has40 representative command paths exercised.
A subsequent resource-only run on ICD4b5d6cb6 passes full color clear and an
offset11×3 image copy in both rounds, including full destination readback, row
padding and allocation guards. It takes616ms,5.2MiB,zero swap, with no new faults
and both services active. This closes the command exercise worklist, not the
complete compute contract. See [transfer result](results/core-api-0044-image-transfers.json). Core1.1 DispatchBase
and external-buffer-property evidence still uses internal dispatch tables;
the corresponding public version/extensions are not advertised. See
[the exact result and command ledger](results/core-api-0044-backports.json).

Robust buffer access, broader subgroup/CTS coverage, legal shared-memory/workgroup
execution, remaining mandatory resource/feature behavior and applicable CTS
coverage remain active work. Graphics is outside scope; performance work stays
paused. The preceding stage7 result is preserved in
[its separate record](results/core-api-0044-consolidated.json).

The fifth increment, `0044-radish-depth-consumers-wip.patch`, adds real D16/D32
sampled-depth and compare-sampler code, depth views/barriers and matching-format
depth image copies. It is built into4b5d6cb6, but depth consumption is not exercised
by the color-resource result. Core compute does not gain graphics-only depth
clear or buffer/depth copy commands.

The sixth increment, `0044-radish-subgroup-linear-wip.patch`, preserves original
logical shapes while changing physical dispatch only for BASIC shaders. Existing
register/shared-memory guards remain, including a conservative resident-thread
budget. It does not solve the earlier legal-workgroup/shared-memory failures.

## Consolidated reproduction

Apply the fifteen0044 incremental patches in order to the documented0042 Mesa
stack and build the isolated driver. Build probes against its matching internal
ABI:

```sh
python3 radish/scripts/build-core-compute.py "$API_WORK/build" "$API_WORK/compute-probes"
exec 9>"$ORIGINAL_DEVICE_LOCK"
flock -w 1800 9
python3 radish/scripts/device-core-compute.py probe backports-native16 \
  --driver "$API_WORK/build/src/freedreno/radish/libvulkan_radish.so" \
  --driver-sha256 "$DRIVER_SHA" --probe-dir "$API_WORK/compute-probes" --lock-fd 9
```

Use `--suite resources` for the bounded color copy/clear resource-only run.
The public selection option is host-checked; the actual run used the same
existing resource-only command list in a private derivative.

Set `RADISH_DEVICE_LOCK` to the same original lock and `RADISH_WORK` to the host
evidence directory. The private driver is selected only for the bounded service.
Use `--suite subgroup` for the four-case BASIC check. The public selector is
host-checked; the measured private runner used the same fixed subgroup command
list. Independent non-fault failures remain separate; new GPU faults stop the
batch. Do not interpret compile success as execution.

## Earlier API5 foundation evidence

Performance experiments are paused. The current priority is complete compute
behavior required by Vulkan 1.0/1.1, including execution, descriptors, resources,
synchronization, subgroup BASIC and required SPIR-V behavior. Graphics remains
out of scope. The audit's 40 command paths are a worklist, not a conformance
boundary.

This first checkpoint implements five API/command-buffer paths and an image
transfer foundation. The five-path probe passes on A506. Image allocation,
layout, binding and transfer code builds, but this checkpoint has no image
execution evidence. Image/texel/sampler consumers and events/queries are still
being integrated. Do not treat metadata-only view or sampler allocation as
completed usable resource support.

## What ran

The raw-ICD probe creates an API 1.0 instance and uses this driver's matching
internal dispatch tables to exercise the four unadvertised 1.1 commands. Their
ordinary public core-name lookups remain absent. This deliberately tests the
implementation without claiming public 1.1 availability.

- Two GPU dispatches use local4×2×2, grid65×2×2 and base(3,4,5). Each checks
  all 4,160 invocations' workgroup counts, workgroup/global/local IDs, local index
  and three descriptor-supplied values. Each crosses eight physical chunks and
  eight compute submissions.
- Secondary command execution preserves an independently allocated UPDATE
  payload after the original host array is overwritten. Both command buffers
  are reset and reused only after completed execution. Full input/output guards
  and the updated input words pass.
- Descriptor templates cover unaligned offset 1, a nontrivial byte stride,
  consecutive bindings and zero stride. Actual GPU reads distinguish the cases.
- Layout support covers empty layouts, sparse binding numbers with ignored
  zero-count entries, the 32-descriptor cap, rejection above that cap and an
  unsupported descriptor type. Supported layouts are also created/destroyed.
- External-buffer properties honestly return zero import/export features and
  the queried handle type as compatible.

The functional service completed in 116 ms, with 2.2 MiB peak memory, no swap, no
new GPU fault/recovery messages, and both live services active. This is
correctness evidence, not a performance comparison. The shader uses 8 full/1 half
registers, no loop/shared/private memory and native length 5.

Measured private ICD SHA256:
`6de62b26776079a07d4e4a7269672d5d0cedc2b5b5e95af98486db2563bc6924`.
Probe `f4d1e0beaa895e63a3e4f6ec84fe7e552b353baca15a9e000534ac294f316a65`;
SPIR-V `b3b1a5925dcfe24ec14d3ab2b64170a69c9ae328ecc091519b32efb1c66e4421`.
The compact [result record](results/core-api-0044-api5.json) pins the source and
both logs. Private full evidence is
`WORK/core-api5-0044-first/{compile,probe}`. The original runner is preserved;
before the first functional run a delimiter-only correction made its exact
profile check match the driver's comma-separated grid/local fields.

## Reproduction

Use a separate Mesa checkout at
`ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230`. Apply cumulative 0041, then the
incremental 0042 Radish timer patch, then
`patches/0044-radish-core-api-foundation.patch`. Do not apply the ncnn patch to
Mesa. This WIP is not automatically applied to the normal inference checkout.
Configure/build that isolated driver using the existing `scripts/build.sh`
cross environment, with `RADISH_MESA` and `RADISH_WORK` pointing to the isolated
source and build root. Keep the original device lock path explicit.

Build the probe against that exact generated/internal Mesa ABI:

```sh
python3 radish/scripts/build-core-api5.py "$API_WORK/build" "$API_WORK/api5"
```

The build helper uses the existing compile command and AArch64 sysroot. Set
`RADISH_GCC_INSTALL` when its GCC runtime path differs from the documented
version 16 layout. Obtain SHA256 values from the built files and use one fresh
safe label for both phases:

```sh
exec 9>"$ORIGINAL_DEVICE_LOCK"
flock -w 1800 9
python3 radish/scripts/device-core-api5.py compile api5-first \
  --driver "$API_WORK/build/src/freedreno/radish/libvulkan_radish.so" \
  --driver-sha256 "$DRIVER_SHA" --probe "$API_WORK/api5/api5-probe" \
  --probe-sha256 "$PROBE_SHA" --shader "$API_WORK/api5/probe.spv" \
  --shader-sha256 "$SHADER_SHA" --lock-fd 9
python3 radish/scripts/device-core-api5.py probe api5-first \
  --driver "$API_WORK/build/src/freedreno/radish/libvulkan_radish.so" \
  --driver-sha256 "$DRIVER_SHA" --probe "$API_WORK/api5/api5-probe" \
  --probe-sha256 "$PROBE_SHA" --shader "$API_WORK/api5/probe.spv" \
  --shader-sha256 "$SHADER_SHA" --lock-fd 9
```

Use `--suite resources` for the bounded color copy/clear resource-only run.
The public selection option is host-checked; the actual run used the same
existing resource-only command list in a private derivative.

Set `RADISH_DEVICE_LOCK` to that same original lock if the public checkout is
elsewhere. Inspect actual compiler resources before the functional phase.
The runner requires an identical successful compile payload, verifies hashes
before/after, preserves services, applies 384 MiB/128 MiB swap/45 s caps and drains
its own unit on failure. Stop after a new GPU fault. The public runner differs
from the measured private runner only in portable repository-root discovery;
that adaptation has host checks, not an additional device run.

## Historical foundation limitations

The image foundation advertises transfer usages only. It supports explicitly
bounded uncompressed color formats and owns checked linear physical layouts;
mutable reinterpretation, sparse binding and image shader consumers are not
claimed here. Views/samplers need real legal descriptor consumers. The current
image paths still require execution and cache-coherence qualification.

Event/query integration, generic UBO/dynamic and image/texel descriptors,
descriptor update/lifetime behavior, legal shared-memory/workgroup execution,
subgroup BASIC, limits/features and applicable CTS remain active work. Neither
closing the 40 symbols nor this small passing probe establishes Vulkan 1.0/1.1
conformance. The system device ICD and canonical inference sources are unchanged.
