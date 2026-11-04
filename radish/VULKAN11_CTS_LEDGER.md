# Vulkan 1.1 CTS ledger

The Khronos Vulkan CTS mustpass, run unmodified on the Adreno 506 (ThinkSmart
View) through Radish. This ledger replaces the partial case counts in
[VULKAN11_COMPUTE_STATUS.md](VULKAN11_COMPUTE_STATUS.md), which is kept as
history.

| | |
| --- | --- |
| CTS | `vulkan-cts-1.4.7.1` (`0d18b347943e`), `external/vulkancts/mustpass/main/vk-default.txt`: 196 files, 3,215,124 cases |
| deqp-vk | built from that tag with `cmake/aarch64-tsv.cmake`, `DEQP_TARGET=vulkan_headless`, video tests disabled (they report NotSupported); stripped, sha256 `3cd8cd189c5b…e3d6f9`, 81 MB |
| Driver, first full pass | `jethac/turnip-rt` `radish/main` `3740401` (library `a7e796f6ddd1`) |
| Driver, re-runs | `4c51830` (`46ec36d1a5e8`) and `6eba49b` (`3c58218041a4`); the `driver` column says which run each group's numbers come from |
| Dates | first pass 2026-11-03 21:55 to 2026-11-04 02:46 JST; re-runs to 04:10 JST |
| Runner | `scripts/tsv/cts.sh` / `cts.py` (see below) |
| Raw logs | laptop only: `/run/media/jethac/Shared/radish-src/cts-runs/<run>/<group>/qpa/*.qpa.xz` |

Committed with this ledger, in `results/cts-1.4.7.1/`: the two tables at
every second-level group (`second-level-*.md`) and the non-passing cases of
the current results, one per line with status and message
(`nonpass-current.tsv.xz`).

## Compute performance baseline before graphics work

Measured on driver `6eba49b` (`3c58218041a4`), 2026-11-04, services up,
before any graphics code. Every graphics step is re-measured against these.

| workload | GPU (Radish) | CPU reference |
|---|---|---|
| Kokoro-82M decoder, 4.2 s ja audio, warm request (`tts.sh`) | synth 26.45 s, core 24.30 s | ncnn 4 threads 30.92 s (earlier) |
| Shipped en student x25k2 (step 28,000 weights), decoder core, 4.2 s audio (T=168), `core-bench.sh`, fused AdaSnake graph | 3.52–3.67 s (core RTF 0.84–0.87), rel RMS 3.0e-4 vs PyTorch | ncnn CPU 4 threads, unfused graph: 2.03–2.11 s (core RTF 0.48–0.50) |

The student core is exported with the trained weights (not a random
graph) by `scripts/export-student-core.py … paradee:nx25k2` (AdaIN width
256, upsample_initial_channel 128, resblock kernels [3, 7], dim_in 512).
The earlier student on the tablet (`student/en`, Kokoro-7M-Distill) differs
only in `dim_in` (160).

## How it was run

`cts.sh mustpass ICD RUN` runs every mustpass file in `vk-default.txt` order
through `cts.py`, in batches sized from the measured rate (120 s target, at
most 6,000 cases). Each batch is one hold of the device lock: GPU fault
lines counted, one `systemd-run` unit (`MemoryMax=400M`,
`MemorySwapMax=64M`, `RuntimeMaxSec=600`, `LimitCORE=0`, `Nice=10`) running
`deqp-vk --deqp-caselist-file … --deqp-log-images=disable
--deqp-log-shader-sources=disable --deqp-watchdog=enable`, fault lines
counted again, `TestResults.qpa` pulled and deleted. A process that dies
resumes at the next unreported case; the case that was running is
recorded as Crash (or Timeout). Fault lines are matched to the case
running at that kernel timestamp. The live services stayed up throughout.

The whole list was run once on the first-pass driver (2.5 h of device
time). Every group with any result other than NotSupported was then re-run
whole on a later driver. A 1% deterministic sample (every 100th case of
every group, 32,262 cases) was run first to size batches.

GPU faults: IOMMU page faults with no hangcheck, all recovered (the next
cases in the same process passed). No hang, no hangcheck, no GPU recovery
in any run. Fault lines are attributed to the case that started last
before the kernel timestamp (`results/cts-1.4.7.1/faulting-cases.tsv`);
NotSupported cases take about a millisecond, so the attribution can be off
by a few cases. On the current drivers the faults cluster in
pipeline.monolithic.sampler compute cases with border or mirror-clamp
addressing, glsl integer clamp-to-border cases and
binding_model.descriptor_copy sampler arrays (see "What still fails"). The
storage-format faults of the first pass are gone. `gpu_fault_count` totals
are not meaningful over hours: audit lines from the transient units roll
the kernel ring buffer; the runner compares the lines before and after
each batch instead.

## Results by group

Current results (first pass, then each group re-run whole on the newest
driver it was run on). `other` is Missing/DeviceLost/ResourceError; there
were no InternalErrors and no case was left unrun.

| group | cases | Pass | Fail | NotSupported | QualityWarning | CompatibilityWarning | Crash | Timeout | InternalError | other | not run | driver |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dEQP-VK.api | 267509 | 29472 | 8914 | 229123 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.binding_model | 150294 | 1908 | 12303 | 136083 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.clipping | 308 | 0 | 12 | 296 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.compute | 67553 | 92 | 0 | 67461 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.conditional_rendering | 1030 | 0 | 0 | 1030 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.cooperative_vector | 53562 | 0 | 0 | 53562 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.data_graph | 12632 | 0 | 0 | 12632 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.depth | 8 | 0 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.descriptor_indexing | 115 | 0 | 0 | 115 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.device_group | 21 | 0 | 10 | 11 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.dgc | 4739 | 0 | 0 | 4739 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.draw | 29475 | 0 | 467 | 29008 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.drm_format_modifiers | 1572 | 0 | 0 | 1572 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.dynamic_state | 671 | 40 | 22 | 609 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.fragment_operations | 151 | 0 | 46 | 105 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.fragment_shader_interlock | 576 | 0 | 0 | 576 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.fragment_shading_barycentric | 20991 | 0 | 0 | 20991 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.fragment_shading_rate | 110603 | 0 | 0 | 110603 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.geometry | 200 | 0 | 0 | 200 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.glsl | 28399 | 2634 | 13114 | 12651 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz | 757 | 7 | 670 | 80 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image | 105646 | 12026 | 2163 | 91457 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401, 4c51830 |
| dEQP-VK.image_processing | 1211 | 0 | 0 | 1211 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.imageless_framebuffer | 12 | 0 | 0 | 12 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.info | 23 | 19 | 1 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.memory | 6364 | 2568 | 107 | 3689 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.memory_model | 18530 | 103 | 3 | 18424 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.mesh_shader | 28061 | 0 | 0 | 28061 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.multiview | 838 | 0 | 0 | 838 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.pipeline | 1172232 | 21997 | 57836 | 1092399 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401, 4c51830, 6eba49b |
| dEQP-VK.postmortem | 24 | 0 | 0 | 24 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.protected_memory | 6000 | 0 | 0 | 6000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.query_pool | 19276 | 80 | 4223 | 14973 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization | 15019 | 0 | 99 | 14920 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ray_query | 49359 | 0 | 0 | 49359 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.ray_tracing_pipeline | 22677 | 0 | 0 | 22677 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.reconvergence | 6253 | 0 | 0 | 6253 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.renderpasses | 81687 | 0 | 13884 | 67802 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.robustness | 98776 | 33 | 232 | 98511 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.shader_abort | 8 | 0 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.shader_object | 243823 | 0 | 0 | 243823 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.sparse_resources | 19402 | 0 | 0 | 19402 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.spirv_assembly | 70311 | 1676 | 506 | 68129 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ssbo | 12225 | 3935 | 4 | 8286 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.subgroups | 48621 | 10 | 0 | 48611 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.synchronization | 64872 | 1891 | 922 | 62049 | 0 | 0 | 6 | 0 | 0 | 4 | 0 | 6eba49b |
| dEQP-VK.synchronization2 | 81617 | 2 | 0 | 81615 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.tensor | 2843 | 0 | 0 | 2843 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.tessellation | 1114 | 0 | 0 | 1114 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.texture | 25669 | 180 | 9306 | 16180 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.transform_feedback | 133719 | 0 | 0 | 133719 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.ubo | 13240 | 972 | 2170 | 10098 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.video | 9471 | 0 | 0 | 9471 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.wsi | 36876 | 0 | 0 | 36876 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401 |
| dEQP-VK.ycbcr | 68159 | 2 | 0 | 68157 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| **total** | 3215124 | 79647 | 127014 | 3008449 | 3 | 0 | 7 | 0 | 0 | 4 | 0 | |

Totals: **79,647 Pass, 127,014 Fail, 3,008,449 NotSupported**, 3
QualityWarning, 7 Crash, 4 DeviceLost, 0 Timeout. The first pass on
`3740401` had 74,470 Pass, 125,517 Fail, 3,015,115 NotSupported and 13
Crash.

What changed between the first pass and now (only groups that changed):

| group | Pass, first pass | Pass, now | Fail, first pass | Fail, now | Crash, first pass | Crash, now |
|---|---|---|---|---|---|---|
| dEQP-VK.api | 29241 | 29472 | 9145 | 8914 | 0 | 0 |
| dEQP-VK.binding_model | 1819 | 1908 | 12392 | 12303 | 0 | 0 |
| dEQP-VK.glsl | 2491 | 2634 | 13257 | 13114 | 0 | 0 |
| dEQP-VK.image | 9495 | 12026 | 4694 | 2163 | 0 | 0 |
| dEQP-VK.memory_model | 94 | 103 | 12 | 3 | 0 | 0 |
| dEQP-VK.pipeline | 19927 | 21997 | 54992 | 57836 | 0 | 0 |
| dEQP-VK.synchronization | 1788 | 1891 | 1017 | 922 | 12 | 6 |
| dEQP-VK.texture | 179 | 180 | 7555 | 9306 | 0 | 0 |

Fail went up in `pipeline` and `texture` because
`VK_KHR_sampler_mirror_clamp_to_edge` is now exposed: 4,914 pipeline and
1,896 texture cases that were NotSupported now run, and most of them need
a graphics pipeline or a 256-invocation workgroup (see below).

### Why the non-passing cases fail

Every result that is not Pass, NotSupported or a warning, split by cause
(`cts.py classify`):

- **graphics-path**: the case records a graphics pipeline, render pass or
  graphics-only command (`vkCreateGraphicsPipelines` /
  `vkEndCommandBuffer` → `VK_ERROR_FEATURE_NOT_PRESENT`), or its programs
  include graphics stages and it is not a compute variant. These need a
  graphics queue, which Radish does not have.
- **cts-queue**: the CTS asks for queue families a single compute-only
  family cannot give (see the qualification section).
- **other**: compute-path failures, the driver's to fix.

| group | graphics-path | cts-queue | other |
|---|---|---|---|
| dEQP-VK.api | 8913 | 1 | 0 |
| dEQP-VK.binding_model | 12300 | 0 | 3 |
| dEQP-VK.clipping | 12 | 0 | 0 |
| dEQP-VK.device_group | 6 | 0 | 4 |
| dEQP-VK.draw | 467 | 0 | 0 |
| dEQP-VK.dynamic_state | 22 | 0 | 0 |
| dEQP-VK.fragment_operations | 46 | 0 | 0 |
| dEQP-VK.glsl | 13114 | 0 | 0 |
| dEQP-VK.graphicsfuzz | 669 | 0 | 1 |
| dEQP-VK.image | 2163 | 0 | 0 |
| dEQP-VK.info | 0 | 0 | 1 |
| dEQP-VK.memory | 76 | 0 | 31 |
| dEQP-VK.memory_model | 0 | 0 | 3 |
| dEQP-VK.pipeline | 56526 | 0 | 1310 |
| dEQP-VK.query_pool | 4223 | 0 | 0 |
| dEQP-VK.rasterization | 99 | 0 | 0 |
| dEQP-VK.renderpasses | 13884 | 0 | 1 |
| dEQP-VK.robustness | 0 | 232 | 0 |
| dEQP-VK.spirv_assembly | 505 | 0 | 1 |
| dEQP-VK.ssbo | 0 | 0 | 4 |
| dEQP-VK.synchronization | 922 | 0 | 10 |
| dEQP-VK.texture | 5032 | 0 | 4274 |
| dEQP-VK.ubo | 2141 | 0 | 29 |
| **total** | 121120 | 233 | 5672 |

The 5,672 `other` results, by cause:

| cases | where | cause |
|---|---|---|
| 4,493 | texture `*_compute` (4,266), pipeline.monolithic.sampler `exact_sampling` (192), memory.pipeline_barrier (31), device_group (4) | `vkCreateComputePipelines` rejects the workgroup: these shaders use 16×16 = 256 invocations and Radish reports and enforces `maxComputeWorkGroupInvocations` = 128 (every case probed with `RADISH_DEBUG=pipeline`, 38 of them, stopped at that check). 128 is the spec minimum, so the CTS assumes more than it may. |
| 954 + 3 | pipeline.monolithic.sampler `*clamp_to_border*` compute; binding_model.descriptor_copy sampler arrays | border colours in compute shaders are wrong; integer formats also page-fault at an address outside Radish's border-colour table (the glsl integer clamp-to-border cases fault the same way but pass) |
| 164 | pipeline.monolithic.image `view_type.3d` `2x2x2`/`3x3x3` | small mipmapped 3D images sample wrong; larger 3D sizes pass |
| 29 | ubo (`random`, `*_array`, `multi_nested_struct`, `single_struct`) compute | "Detected non-white pixels": a uniform-block layout case the driver reads wrongly |
| 4 | ssbo.layout.random, ssbo.readonly | "Counter value incorrect" |
| 10 | synchronization.timeline_semaphore.device_host | 6 Crash in blit cases after a failed blit, 4 DeviceLost on 1D-image copies; all pass when run alone |
| 8 | texture.explicit_lod | verification failure |
| 3 | memory_model message_passing / write_after_read | ordering failure with a workgroup-memory payload and an image guard |
| 1 + 1 + 2 | info.device_mandatory_features (multiview); renderpasses.renderpass2 (Crash); one spirv_assembly and one graphicsfuzz amber case that record graphics work ("vkEndCommandBuffer Fail") | multiview: see the qualification section; the others are graphics-path cases the classifier misses |


<details><summary>Second-level groups with any result other than NotSupported (870 of 1,485; all rows are in results/cts-1.4.7.1/second-level-current.md)</summary>

| second-level group | cases | Pass | Fail | NotSupported | QualityWarning | CompatibilityWarning | Crash | Timeout | InternalError | other | not run | driver |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dEQP-VK.api.array | 82 | 5 | 0 | 77 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.buffer | 3076 | 1030 | 0 | 2046 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.buffer_memory_requirements | 240 | 12 | 0 | 228 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.buffer_view | 1004 | 279 | 12 | 713 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.command_buffers | 131 | 109 | 8 | 14 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.copy_and_blit | 201669 | 13683 | 8759 | 179227 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.descriptor_pool | 6 | 6 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.descriptor_set | 6 | 4 | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.device_init | 252 | 239 | 0 | 13 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.extension_duplicates | 4 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.external | 550 | 45 | 0 | 505 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.fill_and_update_buffer | 110 | 72 | 0 | 38 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.fragment_shader_output | 86 | 0 | 63 | 23 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.get_device_proc_addr | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.granularity | 650 | 156 | 55 | 439 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.image_clearing | 45636 | 7808 | 0 | 37828 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.info | 10487 | 5752 | 0 | 4735 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.invariance | 3 | 1 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.maintenance3_check | 178 | 2 | 0 | 176 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.null_handle | 24 | 23 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.object_management | 457 | 233 | 7 | 217 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.pipeline | 9 | 3 | 4 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.smoke | 8 | 4 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.api.version_check | 3 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.binding_model.descriptor_copy | 289 | 55 | 73 | 161 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.binding_model.descriptor_update | 123 | 26 | 37 | 60 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.binding_model.descriptorset_random | 35148 | 44 | 124 | 34980 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.binding_model.dynamic_offset | 14 | 13 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.binding_model.shader_access | 86412 | 1770 | 12068 | 72574 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.clipping.clip_volume | 43 | 0 | 12 | 31 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.compute.pipeline | 22533 | 90 | 0 | 22443 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.compute.shader_object_binary | 22510 | 1 | 0 | 22509 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.compute.shader_object_spirv | 22510 | 1 | 0 | 22509 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.afr | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.afr_dedicated | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.afr_sys | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.compute_alternate_dispatch | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.compute_alternate_dispatch_dedicated | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.compute_split_dispatch | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.compute_split_dispatch_dedicated | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.sfr | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.sfr_dedicated | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.device_group.sfr_sys | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.draw.renderpass | 12466 | 0 | 467 | 11999 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.dynamic_state.fast_linked_library | 61 | 0 | 1 | 60 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.dynamic_state.monolithic | 185 | 40 | 20 | 125 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.dynamic_state.pipeline_library | 61 | 0 | 1 | 60 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.fragment_operations.occlusion_query | 64 | 0 | 32 | 32 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.fragment_operations.scissor | 27 | 0 | 11 | 16 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.fragment_operations.transient_attachment_bit | 6 | 0 | 3 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.440 | 68 | 0 | 68 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.arrays | 159 | 53 | 106 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.atomic_operations | 1040 | 32 | 0 | 1008 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.builtin | 3193 | 753 | 0 | 2440 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.builtin_var | 111 | 0 | 38 | 73 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.combined_operations | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.conditionals | 27 | 9 | 18 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.constant_expressions | 63 | 21 | 42 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.constants | 147 | 49 | 98 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.conversions | 1590 | 530 | 1060 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.crash_test | 6 | 1 | 2 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.derivate | 1674 | 0 | 1152 | 522 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.discard | 25 | 0 | 25 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.functions | 297 | 99 | 198 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.helper_invocations | 6 | 0 | 5 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.indexing | 760 | 0 | 760 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.invariance | 72 | 0 | 72 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.limits | 15 | 0 | 2 | 13 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.linkage | 69 | 0 | 69 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.logical_copy | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.loops | 624 | 0 | 624 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.matrix | 1764 | 0 | 1764 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.opaque_type_indexing | 372 | 44 | 0 | 328 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.operator | 3630 | 0 | 3630 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.precise | 117 | 0 | 117 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.return | 38 | 0 | 38 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.scoping | 51 | 17 | 34 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.struct | 80 | 0 | 80 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.switch | 122 | 0 | 122 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.swizzles | 972 | 324 | 648 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.glsl.texture_functions | 7925 | 702 | 2338 | 4885 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.access-new-vector-inside-if-condition | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.always-false-if-in-do-while | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.arr-value-set-to-arr-value-squared | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.array-idx-multiplied-by-for-loop-idx | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.assign-array-value-to-another-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.assign-array-value-to-another-array-2 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.barrier-in-loop-with-break | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.break-in-do-while-with-nested-if | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.call-if-while-switch | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.color-set-in-for-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.color-write-in-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.conditional-return-in-infinite-while | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.continue-and-merge | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.control-flow-in-function | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.control-flow-switch | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-access-array-dot | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-and-even-numbers-from-fragcoord | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-acos-ldexp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-determinant | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-determinant-for-if | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-mix-nan | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-mod-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-module-small-number | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-negative-step-func | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-reflect-denorm | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-tanh | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-undefined-matrix-mul | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-apfloat-unpackunorm-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-array-accesses-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-array-cast-bool-float-div-by-zero-no-effect | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-array-copies-loops-with-limiters | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-array-set-element-condition-negative-modulus | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-atan-trunc-vec4 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitcount | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitfieldExtract-undefined | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitfieldinsert-undefined | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitfieldreverse-left-shift-findmsb | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitfieldreverse-loop-limit-underflow | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitwise-and-variable-and-its-negation | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitwise-inverse-uniform-condition | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitwise-or-uniform-zero-tenth-bit-loop-limit-find-lsb | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitwise-shift-right-always-select-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-bitwise-shift-right-full-bits-no-effect-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-blockfrequency-several-for-loops | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-branch-probability-identity-matrix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-cast-float-to-int-and-back | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-clamp-loop-limit-increment-float-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-clamp-lower-limit-from-always-false | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-clamp-min-bitcount-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-clamp-value-and-min-select-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-clamp-vector-component-condition-using-matrix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-clamp-vector-element-ceil-negative | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-clamp-vector-variable-negative-offset | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-color-output-undefined-in-unexecuted-branch | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-color-overwrite-identity-matrix-multiply | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-combine-and-or-xor-gt-lt | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-condition-bitfield-extract-integer | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-condition-clamp-min-from-uniform-never-larger | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-condition-increment-zero-to-one-divide-by-two | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-condition-loop-index-bitwise-not | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-condition-matrix-determinant-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-conditions-empty-blocks-index-array-one-divided-by-findlsb | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-bitfieldinsert-div-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-ceil-vec4 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-clamp-inside-while | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-clamp-max | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-clamp-min | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-clamp-vs-original | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-det-identity | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-dot-condition-true | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-dot-determinant | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-gte-const-first | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-min-as-loop-range | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-mod-one-one-lte | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-pow-large-exp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-same-condition | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-sinh-inf | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-const-folding-vector-shuffle | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-constant-folding-atan-over-tanh | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-constants-combine-add-sub | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-constants-mix-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-copy-array-elements-except-first-nested-loop-replace-identical-values | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-copy-output-color-swizzle-array-indexing | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-copy-prop-arrays-no-stores | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-copy-prop-arrays-param-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-cosh-clamped-to-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-cumulate-loops-unreachable | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dag-combiner-clamp-undefined-access-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dag-combiner-combine-casts-legalize-vector-types-xyz-swizzle-for-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dag-combiner-findmsb-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dag-combiner-glf_color | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dag-combiner-increment-color | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dag-combiner-loop-bitfieldreverse | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dag-combiner-neg-div-pow2 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dag-combiner-same-cond-nested | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dead-branch-func-return-arg | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dead-code-unreachable-merge | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-decrement-vector-elements-clamp-index | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-derivative-uniform-vector-global-loop-count | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-descending-loop-index-temporary-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-descending-loop-min-max-always-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-determinant-uninitialized-matrix-never-chosen | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dfdx-dfdy-after-nested-loops | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-divide-matrix-transpose-by-constant | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-do-while-loop-until-uniform-lt-itself | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-do-while-negative-iterator-nested-loops-increment-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-do-while-switch-case-bitcount-findmsb | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-double-if-true-in-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-double-negation-fragcoord-cast-ivec2-bitwise-and | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-dummy-function-loop-array-element-increment-never-read | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-empty-loop-minus-one-modulo-variable-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-enable-bits-pixel-location-negate-not-equal-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-exp2-two | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-extend-uniform-vec2-to-vec3-matrix-multiply | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-find-msb-input-either-zero-or-minus-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-findlsb-division-by-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-float-array-init-pow | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-and-in-for-loop-range | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-bitwise-and-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-bitwise-or-full-mask | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-bitwise-xor | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-logical-and-const-variable | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-logical-and-constant | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-logical-or-constant | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-negate-min-int-value | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-negate-variable | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-shift-gte32 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-shift-right-arithmetic | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fold-switch-udiv | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-clamp-cmp-const-first | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-merge-add-sub-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-construct-extract | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-dot-extract | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-dot-no-extract | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-merge-add-sub | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-merge-div-mul | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-merge-divs | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-merge-mul-div | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-merge-sub-add | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-merge-sub-sub | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-merge-var-sub | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-mix-uniform-weight | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-negate-div | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-negate-sub | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-redundant-mix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-shuffle-extract | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-shuffle-mix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-split-vector-init | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-folding-rules-vec-mix-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-for-array-initializing-modulo | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-for-loop-condition-one-shift-right-integer-comparison-break | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-for-loop-min-increment-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-for-loop-start-negative-increment-variable | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-for-loop-struct-as-iterator | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-for-switch-fallthrough | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fract-smoothstep-undefined | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fract-trunc-always-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcood-multiple-conditions-function-loop-global-counter | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-and-one-or-same-value | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-bitwise-and-loop-reduce-value-index-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-bitwise-not | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-clamp-array-access | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-conditions-never-return-index-array-using-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-integer-loop-reduce-to-range | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-loop-limit-negative-decrease-sum-first-iteration | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-multiple-conditions-function-global-loop-counter-simplified | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-multiply | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-fragcoord-select-always-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-always-return-negative-bitfield-extract | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-argument-uniform-float-loop-never-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-call-twice-clamp-global-variable | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-check-argument-one-always-return-minus-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-clamp-min-identical-shift-right | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-divide-argument-until-lt-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-find-lsb-ivec2-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-fragcoord-condition-always-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-global-loop-counter-sample-texture | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-global-variables-fragcoord-condition-call-twice | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-index-array-redundant-clamps | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-infinite-loop-always-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-infinite-loop-return-identical-condition | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-large-array-max-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-large-loop-always-return-first-iteration | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-large-loop-break-argument-lte-global-loop-bound | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loop-check-determinant-zero-return-vector | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loop-clamp-no-effect | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loop-condition-uniform-shift-right | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loop-condition-variable-less-than-min-itself | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loop-copy-array-elements-based-on-arguments | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loop-modify-ivec-components-infinite-loop-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loop-same-conditions-multiple-times-struct-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loop-switch-increment-array-element-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loop-variable-multiplied-unused | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-loops-vector-mul-matrix-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-max-all-ones-select-always-true | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-min-identical-integer-division-multiplication | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-min-integer-large-shift-unused | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-multiple-loops-compare-integer-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-nested-do-whiles-looped-once | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-nested-loops-limit-uniform-xor-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-parameter-zero-divided-by-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-round-unpack-half-2x16 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-set-struct-field-zero-loop-reset-first-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-struct-int-array-loop-check-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-switch-case-constant-clamp-transpose-identity-matrices | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-trivial-switch-case | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-two-loops-limit-using-arguments-array-element-copies | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-undefined-shift-left-index-array-with-return-value | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-unpack-unorm-2x16-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-unused-argument-single-loop-iteration-icrement-global-counter | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-variable-plus-one-minus-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-function-with-nested-loops-called-from-nested-loops | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-bound-true-logical-or | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-count-array-struct-field-set-int-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-accumulate-integer-condition-large-array-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-findlsb-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-for-loop-function-call-inside-never-called | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-main-function-call | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-multiply-one-minus | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-read-past-matrix-size-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-select-one-or-zero-never-greater-than-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-set-array-element-once-index-using-findmsb | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-squared-comparison | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-global-loop-counter-texture-sample-loop-condition-set-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-if-conversion-identical-branches | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-if-switch-fallthrough | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-if-true-continue | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-if-true-float-bits-to-int-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inc-array-element-loop-lsb | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inc-inside-switch-and-for | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-array-element-in-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-array-element-usub-borrow | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-float-in-loop-abs | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-global-counter-loop-function | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-inside-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-int-loop-counter-mod-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-multiple-integers | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-one-array-element-check-index-from-fragcoord | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-vector-array-matrix-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-vector-component-with-matrix-copy | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-increment-vector-function-call-conditional | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-index-array-using-uniform-bitwise-or-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-initialize-integer-array-variable-divided-by-itself | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inline-pass-empty-block | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inline-pass-nested-loops | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inline-pass-return-in-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inline-pass-unreachable-func | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-add-sub-determinant | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-add-sub-increase-negative | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-add-sub-ldexp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-add-sub-neg-func-arg | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-add-sub-pre-increase | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-and-or-xor-for-bitfieldinsert | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-and-or-xor-pack-unpack | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-and-or-xor-switch | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-and-or-xor-xor-add | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-compares-combine-select-uaddcarry | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-compares-isnan | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-compares-ldexp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-compares-pre-increment-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-compares-ternary-vector-access | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-compares-while-modulo | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-mul-div-rem-if-undefined-divide-mix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-pack-unpack | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-select-findlsb-uaddcarry | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-shifts-bitfield-bitcount | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-shifts-left-shift-for | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-shifts-mix-mix-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-simplify-demanded-pack-unpack | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-simplify-demanded-packsnorm-unpackunorm | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-simplify-demanded-switch-or-xor | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-combine-vector-ops-asin | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-inst-value-tracking-inversesqrt | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-instr-emitter-pow-asinh | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-instr-info-det-mat-min | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-instruction-simplify-bit-shifting | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-instruction-simplify-inclusive-or | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-instruction-simplify-inst-combine-calls-for-compare-function-call-result | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-instruction-simplify-sqrt | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-instructions-for-if-less-than-equal | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-int-div-round-to-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-int-full-bits-divide-by-two-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-integer-constant-mod-variable-increased-condition-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-integer-minus-one-increased-before-indexing-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-integer-modulo-negative | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-integer-variable-or-with-inversion | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-intervalmap-set-stop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-ir-builder-constant-fold-inst-combine-calls-value-tracking-findmsb-incr-if | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-irbuilder-matrix-cell-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-isnan-asinh-clamp-always-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-ivec-from-uniform-float-shift-right-add-components | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-ivec-shift-right-by-large-number | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-large-for-loop-exit-early-set-iterator-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-large-int-array-nested-loops-set-ivec-index-component-sum | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-large-loop-break-early-condition-iterator-divided | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-large-loop-multiply-integer-by-uniform-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-ldexp-exponent-undefined-divided-fragcoord-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-ldexp-undefined-mat-vec-multiply | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-left-shift-array-access | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-left-shift-right-shift-compare | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-liveinterval-different-dest | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-abs-multiply-offset | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-array-element-bitfield-insert-undefined-never-read | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-array-element-copy-index-clamp-sign | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-array-index-decrement-never-negative | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-array-struct-field-index-array-with-uniforms | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-break-after-first-iteration-set-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-break-floor-nan-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-break-fragcoord-x-empty-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-clamp-to-one-empty-condition | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-condition-bitfield-extract-set-array-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-condition-clamp-vec-of-ones | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-condition-constant-struct-field-data | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-condition-divide-by-uniform-always-false | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-condition-double-negate | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-condition-increment-integer-fallback-global-counter | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-condition-logical-or-never-iterated | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-construct-vec4-from-vec4-clamp-same-min-max | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-decrease-integer-never-break | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-decrease-vector-component-only-first-iteration | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-decrease-vector-components-assign-multiple-times | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-dfdx-constant-divide | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-divide-uninitialized-vector-min-unused | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-exit-conditions-sampler-struct-integer-variable | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-findmsb-findlsb | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-function-call-negative-argument | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-function-call-vector-matrix-multiplication | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-global-counter-break-set-ivec-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-global-counter-increment-iterator-select-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-increase-iterator-condition-uniform-copy-array-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-increment-array-elements-clamp-index | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-increment-array-index-array-usuborrow-feedback | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-increment-integer-findmsb-minus-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-increment-integer-set-output-color-break | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-increment-matrix-element-break-after-first-iteration | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-increment-or-divide-by-loop-index | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-index-array-max-negative-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-integer-half-minus-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-iterator-bitwise-negate | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-iterator-limit-xor-and-unifrom | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-iterator-plus-one-variable-outside-index-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-iterator-start-select-uniform-negative-integer | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-iterator-start-shift-left-right | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-large-array-index-clamp-negative-value | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-limiter-min-findlsb | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-limiter-uniform-bitwise-and-one-always-break | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-logical-xor | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-max-divide-integer-by-ten | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-min-max-clamp-increment-only-first-iteration | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-multiple-iterator-variables-copy-array-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-never-iterated-constant-vector-condition | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-overwrite-sample-texture-as-color-output | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-read-array-index-from-array-data | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-replace-output-color-restore-original | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-returns-behind-true-and-false | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-sampled-texel-integer-counter | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-set-vector-components-pow-two | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-start-fragcoord-while-iterates-once | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-start-from-one-switch-case-invalid-color-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-start-from-one-switch-case-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-struct-array-field-set-value-self-dependency | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-two-iterators-increment-array-empty-do-while | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-variable-less-than-itself | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loop-with-two-integers | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-loops-same-code-outside-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-machine-basic-block-for-for-for-less-than | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-machinevaluetype-one-iter-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-matching-conditions-break | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-matching-if-always-true-inside-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-matrix-double-transpose | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-matrix-mult-round-even-asinh | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-matrix-square-mul-with-vector | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-max-clamp-same-minval | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-max-min-less-than | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-mem-pass-sum-struct-members | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-mem-pass-unused-component | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-merge-return-condition-twice | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-min-identical-uint-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-min-identical-uint-uniform-check-highest-bit | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-min-intbitstofloat-undefined-never-used | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-min-negative-constant-always-below-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-min-nested-loop-same-value-for-variables | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-min-vec2-transpose-mat2-identity | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-missing-return-value-function-never-called | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-mix-uninitialized-float-never-selected | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-mix-uninitialized-vector-select-only-defined-data | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-mod-acosh | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-mod-uint-bits-float | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-modf-clamp-for | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-modf-integer-to-private | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-modulo-zero-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-multiple-fragcoord-conditions-false-never-return-sample-texture | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-multiple-fragcoord-conditions-never-return-color-uninitialized | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-multiple-functions-global-never-change | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-multiple-loops-same-condition-always-false-global-loop-counter | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-multiple-one-iteration-loops-global-counter-write-matrices | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-negative-integer-bitwise-or-uniform-increment-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-functions-accumulate-global-matrix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-functions-compare-fragcood-length-zero-vector | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-functions-struct-arrays-vector-lengths | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-functions-vec4-array-element-argument | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loop-continue-inner-copy-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loop-decrease-vector-components | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loop-initializer-value-increased-inside | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loop-not-greater-than-increment-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loop-undefined-smoothstep-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-array-choose-red-last-iteration | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-clamp-ivec-push-constant-increment-global-counter | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-copy-array-elements-skip-first | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-decrease-ivec-component | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-decrease-vector-component-by-matrix-element-global-loop-counter | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-different-iteration-rates-function-copy-array-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-divide-integer-constant-always-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-float-array-select-by-fragcoord | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-float-bits-to-int-increment-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-fragcoord-conditions-empty-blocks | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-fragcoord-never-return-descending-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-global-counter-func-set-struct-field | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-global-counter-increment-single-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-global-loop-counter-do-while-accumulate-float | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-global-loop-counter-fragcoord-negative-always-false | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-global-loop-counter-index-array-vec2 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-global-loop-counter-iterator-dependency | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-global-loop-counter-output-color-from-backup | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-global-loop-counter-reached-second-iteration | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-identical-iterator-names-multiply-divide | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-identical-iterators-compare-same-array-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-increase-integer-dot-product | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-inner-loop-min-copy-array-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-iterator-times-two-while-min | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-redundant-condition | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-return-inside-while-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-sample-opposite-corners | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-select-starting-value-fragcoord | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-set-struct-data-verify-in-function | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-substract-matrix-element-change-float | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-switch-add-zero-matrix-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-switch-case-fallthrough-increment-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-temporary-copy-output-color-index-matrix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-loops-while-min-iterator-condition-always-false | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nested-structs-function-set-inner-struct-field-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nir-array-access | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nir-opt-large-constants-for-clamp-vector-access | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nir-opt-loop-unroll-if-if-if-if-do-while | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-not-clamp-matrix-access | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-nouble-negation-fragcoord-cast-ivec2-bitwise-and | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-one-bitwise-and-bitwise-or-full-bits | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-one-minus-clamp-always-one-cast-to-int | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-optimize-phis-for | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-packhalf-unpackunorm | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-pattern-match-signum | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-pattern-match-single-bit | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-pow-distance-uniform-vector-constant-one-vector | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-pow-identical-value-sqrt | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-pow-undefined | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-pow-undefined-result-condition-with-always-true | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-rcp-negative-int | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-read-matrix-push-constant | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-reciprocal-var-minus-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-reduce-load-replace-extract | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-reinitialize-matrix-after-undefined-value | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-repeating-conditions-fract-unused | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-replace-copy-object | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-return-after-do-while | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-return-after-first-iteration | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-return-partly-undefined-vector-from-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-sample-texture-hundred-iterations | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-sampler-as-function-argument | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-scaled-number-nested-loops | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-scaled-number-nested-loops-array-access | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-schedule-dag-rrlist-mix-log-cos | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-selection-dag-assign-back-and-forth | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-selection-dag-inverse-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-selection-dag-lt-gt | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-selection-dag-same-cond-twice | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-set-array-elements-to-uniform-check-value-break | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-set-output-color-function-call-nested-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-set-vector-cos-fragcoord | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-sign-array-access-uaddcarry | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplification-unused-struct | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplification-while-inside-for | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-clamp-max-itself | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-combine-compares-max-max-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-component-uniform-idx | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-div-by-uint-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-for-bitwise-condition | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-ldexp-exponent-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-max-multiplied-values | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-modulo-1 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-mul-identity | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-not-less-than-neg | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-right-shift-greater-than-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-select-fragcoord | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-simplify-sign-cosh | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-sin-mul-mat-mat-mul-vec-mat | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-single-block-elim-self-assign | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-single-store-elim-assume-store | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-sinh-ldexp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-small-array-overwrite-most-uniform-value-check-data-break | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-ssa-rewrite-case-with-default | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-step-sinh | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-struct-array-ivec-negative-modulus-empty-function | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-struct-float-array-mix-uniform-vectors | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-struct-int-array-select-uniform-ivec | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-sum-uniform-vector-components-round | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-switch-fallthrough-variable-from-first-case | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-tail-duplicator-for-for-for | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-tail-duplicator-infinite-loops | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-target-lowering-dfdx-cos | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-texel-double-negation | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-transpose-multiply | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-trunc-fract-always-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-two-functions-loops-copy-elements-infinite-loops-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-two-functions-modify-struct-array-element-return-from-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-two-loops-global-loop-counter-clamp-ivec-elements-index-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-two-loops-global-loop-counter-shift-right-zero-increment-array-element | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-two-loops-increment-integer-global-counter-break-square-threshold | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-two-loops-never-iterated | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-types-return-in-main-never-hit | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-uadd-carry-bit-count-index-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-ucarryadd-one-and-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-uniform-vector-copy | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-uniform-vector-function-argument-mod-increment-integers | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-uninitialized-values-passed-to-function-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-unpack-unorm-mix-always-one | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-unused-access-past-matrix-elements | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-unused-matrix-copy-inside-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-val-cfg-case-fallthrough | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-value-inst-combine-select-value-tracking-flip-bits | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-value-tracking-apint-inst-combine-simplify-one-mod-loop-iterator | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-value-tracking-const-dfdy | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-value-tracking-constant-fold-refraction-dfxd-determinant | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-value-tracking-inclusive-or | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-value-tracking-known-nonzero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-value-tracking-max-uintbitstofloat | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-value-tracking-selection-dag-negation-clamp-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-value-tracking-uniform-incident | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-variable-copy-in-function-tex-sample | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-vec2-dot-max-uniform | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-vec2-dot-minus-negative-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-vec2-duplicate-min-always-half | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-vector-dce-inc-unused-comp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-vector-dce-unused-component | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-vector-illegal-index-never-executed | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-vector-log2-cosh | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-write-past-matrix-elements-unused | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-x86-instr-info-determinant-min | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-x86-isel-lowering-apfloat-nan-cos-cos | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-x86-isel-lowering-determinant-exp-acos | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-x86-isel-lowering-machine-value-type-uint-to-float | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-x86-isel-lowering-negative-left-shift | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.cov-x86-isel-lowering-selection-dag-struct-array-clamp-index | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.dead-barriers-in-loops | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.dead-struct-init | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.do-while-false-if | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.do-while-false-loops | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.do-while-if-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.do-while-loop-in-conditionals | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.do-while-with-always-true-if | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.do-while-with-if-condition | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.early-return-and-barrier | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.find-msb-from-lsb | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.flag-always-false-if | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.for-condition-always-false | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.for-loop-with-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.for-with-ifs-and-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.frag-coord-func-call-and-ifs | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.function-with-float-comparison | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.function-with-uniform-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.global-array-loops | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.if-and-switch | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.increment-value-in-nested-for-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.injection-switch-as-comparison | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.int-mat2-struct | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.loop-dead-if-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.loop-nested-ifs | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.loops-breaks-returns | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.loops-ifs-continues-call | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.mat-array-deep-control-flow | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.mat-array-distance | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.mat-mul-in-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.matrices-and-return-in-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.mix-floor-add | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.modf-gl-color | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.modf-temp-modf-color | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.nested-for-break-mat-color | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.nested-for-loops-with-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.nested-ifs-and-return-in-for-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.nested-loops-switch | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.one-sized-array | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.pow-vec4 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.return-before-writing-wrong-color | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.return-float-from-while-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.return-in-loop-in-function | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.return-inside-loop-in-function | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.returned-boolean-in-vector | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.set-color-in-one-iteration-while-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.similar-nested-ifs | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.smoothstep-after-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-access-chains | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-composite-phi | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-composite2 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-composites | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-copy-object | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-dead-break-and-unroll | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-declare-bvec4 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-double-branch-to-same-block2 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-load-from-frag-color | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-null-in-phi-and-unroll | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-bifurcation-Os-mutate-var-vector-shuffle | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-bubblesort-flag-complex-conditionals | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-collatz-O-mutate-composite-construct-extract | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-colorgrid-modulo-O-move-block-down | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-maze-O-dead-code | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-maze-O-memory-accesses | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-maze-flatten-copy-composite | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-orbit-O-mutate-variable | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-orbit-Os-access-chain-mutate-pointer | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-pillars-O-op-select-to-op-phi | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-pillars-volatile-nontemporal-store | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-quicksort-dontinline | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-quicksort-mat-func-param | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-rects-Os-mutate-var-push-through-var | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-sampler-loop-extra-instructions | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.spv-stable-sampler-polar-simple-O-access-chain | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-binarysearch-tree-fragcoord-less-than-zero | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-binarysearch-tree-nested-if-and-conditional | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-binarysearch-tree-with-loop-read-write-global | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-collatz-push-constant-with-nested-min-max | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-colorgrid-modulo-float-mat-determinant-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-colorgrid-modulo-injected-conditional-true | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-colorgrid-modulo-true-conditional-simple-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-colorgrid-modulo-vec3-values-from-matrix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-quicksort-conditional-bitwise-or-clamp | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-quicksort-for-loop-with-injection | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-quicksort-if-false-else-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-quicksort-max-value-as-index | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-rects-vec4-clamp-conditional-min-mix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-triangle-array-nested-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-triangle-clamp-conditional-mix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-triangle-nested-conditional-clamped-float | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.stable-triangle-nested-for-loop-and-true-if | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.struct-and-unreachable-infinite-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.struct-array-data-as-loop-iterator | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.struct-array-index | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.struct-controlled-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.struct-used-as-temporary | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.switch-inside-while-always-return | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.switch-loop-switch-if | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.switch-with-empty-if-false | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.switch-with-fall-through-cases | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.swizzle-struct-init-min | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.transpose-rectangular-matrix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.two-for-loops-with-barrier-function | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.two-loops-mat-add | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.two-loops-matrix | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.two-loops-set-struct | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.two-loops-with-break | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.two-nested-do-whiles | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.two-nested-for-loops-with-returns | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.undefined-assign-in-infinite-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.undefined-integer-in-function | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.uninit-element-cast-in-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.uninitialized-var-decrement-and-add | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.unreachable-barrier-in-loops | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.unreachable-continue-statement | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.unreachable-loops | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.unreachable-loops-in-switch | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.unreachable-return-in-loop | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.uv-value-comparison-as-boolean | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.vec2-modf | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.vector-values-multiplied-by-fragcoord | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.while-function-always-false | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.write-before-break | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.write-red-in-loop-nest | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.graphicsfuzz.wrong-color-in-always-false-if | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.atomic_operations | 6209 | 1145 | 0 | 5064 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.concurrent_copy | 216 | 48 | 0 | 168 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.extended_usage_bit | 216 | 0 | 32 | 184 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.extended_usage_bit_compatibility | 13248 | 1284 | 0 | 11964 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.format_reinterpret | 5944 | 2912 | 0 | 3032 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.general_layout | 29 | 0 | 2 | 27 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.image_size | 108 | 84 | 0 | 24 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.load_store | 3446 | 936 | 0 | 2510 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.misaligned_cube | 5 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.mismatched_formats | 306 | 98 | 0 | 208 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.mutable | 10646 | 1444 | 2040 | 7162 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.qualifiers | 56 | 49 | 0 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.queue_transfer | 1965 | 870 | 0 | 1095 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.sample_cubemap | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.store | 3012 | 1404 | 0 | 1608 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.store_load_consistency | 3 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.subresource_layout | 3537 | 1392 | 0 | 2145 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.image.texel_view_compatible | 8640 | 352 | 88 | 8200 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.build | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_extension_dependencies | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_extension_supported | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_extensions | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_features | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_layers | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_mandatory_features | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_memory_properties | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_no_khx_extensions | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_properties | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.device_queue_family_properties | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.instance_extension_dependencies | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.instance_extension_device_functions | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.instance_extensions | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.instance_layers | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.memory_limits | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.physical_device_groups | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.physical_devices | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.info.platform | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.memory.allocation | 202 | 202 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.memory.binding | 336 | 56 | 0 | 280 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.memory.concurrent_access | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.memory.mapping | 4466 | 2232 | 0 | 2234 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.memory.pipeline_barrier | 184 | 61 | 107 | 16 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.memory.requirements | 77 | 16 | 0 | 61 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4c51830 |
| dEQP-VK.memory_model.message_passing | 8873 | 16 | 2 | 8855 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.memory_model.shared | 210 | 70 | 0 | 140 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.memory_model.write_after_read | 8870 | 17 | 1 | 8852 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.pipeline.monolithic | 465555 | 21997 | 57836 | 385722 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3740401, 4c51830, 6eba49b |
| dEQP-VK.query_pool.concurrent_queries | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.query_pool.discard | 32 | 0 | 6 | 26 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.query_pool.frag_invocations | 12 | 0 | 2 | 10 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.query_pool.occlusion_query | 441 | 0 | 283 | 158 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.query_pool.statistics_query | 18775 | 80 | 3930 | 14765 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.culling | 43 | 0 | 18 | 25 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.depth_bias | 13 | 0 | 13 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.discard | 12 | 0 | 12 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.fill_rules | 5 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.fill_rules_multisample_4_bit | 5 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.flatshading | 15 | 0 | 7 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.interpolation | 30 | 0 | 14 | 16 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.interpolation_multisample_4_bit | 7 | 0 | 3 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.line_continuity | 2 | 0 | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.primitives | 219 | 0 | 8 | 211 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.primitives_multisample_4_bit | 54 | 0 | 2 | 52 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.provoking_vertex | 63 | 0 | 5 | 58 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.rasterization.rasterization_order_attachment_access | 280 | 0 | 6 | 274 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.renderpasses.renderpass1 | 19798 | 0 | 13884 | 5914 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.renderpasses.renderpass2 | 32680 | 0 | 0 | 32679 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.robustness.buffer_access | 1212 | 0 | 138 | 1074 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.robustness.non_robust_buffer_access | 2 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.robustness.oob_access | 121 | 31 | 0 | 90 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.robustness.robustness1_vertex_access | 4 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.robustness.vertex_access | 90 | 0 | 90 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.spirv_assembly.instruction | 43319 | 1292 | 502 | 41525 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.spirv_assembly.type | 25392 | 384 | 4 | 25004 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ssbo.layout | 5275 | 2906 | 3 | 2366 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ssbo.readonly | 1662 | 1025 | 1 | 636 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ssbo.unsized_array_length | 12 | 4 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.subgroups.basic | 70 | 6 | 0 | 64 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.subgroups.builtin_var | 36 | 4 | 0 | 32 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.synchronization.basic | 29 | 26 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.synchronization.implicit | 512 | 128 | 0 | 384 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.synchronization.internally_synchronized_objects | 2 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.synchronization.op | 20131 | 1539 | 860 | 17732 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.synchronization.smoke | 12 | 3 | 5 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.synchronization.timeline_semaphore | 2880 | 194 | 57 | 2619 | 0 | 0 | 6 | 0 | 0 | 4 | 0 | 6eba49b |
| dEQP-VK.synchronization2.timeline_semaphore | 2873 | 2 | 0 | 2871 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.compressed | 984 | 0 | 60 | 924 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.conversion | 40 | 1 | 24 | 15 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.explicit_lod | 380 | 179 | 198 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.filtering | 7510 | 0 | 5556 | 1954 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.mipmap | 513 | 0 | 425 | 88 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.shadow | 5377 | 0 | 433 | 4944 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.subgroup_lod | 3 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.swizzle | 9872 | 0 | 2592 | 7280 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.texel_buffer | 23 | 0 | 14 | 9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.texture.texel_offset | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.2_level_array | 1512 | 82 | 262 | 1168 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.2_level_struct_array | 54 | 0 | 18 | 36 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.3_level_array | 1512 | 84 | 260 | 1168 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.instance_array_basic_type | 1512 | 86 | 258 | 1168 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.link_by_binding | 4 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.multi_basic_types | 120 | 8 | 32 | 80 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.multi_nested_struct | 120 | 4 | 36 | 80 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.random | 2250 | 361 | 239 | 1650 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.single_basic_array | 1512 | 82 | 262 | 1168 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.single_basic_type | 3384 | 242 | 726 | 2416 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.single_nested_struct | 72 | 6 | 18 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.single_nested_struct_array | 72 | 6 | 18 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.single_struct | 72 | 5 | 19 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ubo.single_struct_array | 72 | 6 | 18 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |
| dEQP-VK.ycbcr.query | 1008 | 2 | 0 | 1006 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6eba49b |

</details>

## Can a compute-only device qualify?

**No, not with the 1.1 spec and the CTS as they stand.** The queue rules
allow a device whose only queue family is COMPUTE|TRANSFER, but Vulkan 1.1
itself is defined on top of the graphics API layer: it requires `multiview`
and the full required-format tables (attachment, blend, blit, vertex-buffer
and linear-filter bits) with no queue-based exemption, and the shipped CTS
has no compute-only mode (it was removed before this tag) and fails a
mustpass case outright on a device without a GRAPHICS|COMPUTE family.
Whether to add a minimal graphics path is the user's decision; the
alternative is to ask the Khronos working group, through the X.Org
Foundation, whether a compute-only submission with waivers is acceptable.

Sources: Vulkan-Docs at the revision VK-GL-CTS 1.4.7.1 pins
(`external/vulkan-docs/src`, spec 1.4.364), the CTS tree at that tag, and
the web pages cited.

### Spec text

- **Vulkan 1.1 sits on the graphics layer.** `xml/vk.xml:20676`:
  `<feature api="vulkan,vulkansc,vulkanbase" name="VK_VERSION_1_1"
  number="1.1" depends="VK_VERSION_1_0+VK_GRAPHICS_VERSION_1_1">`. The
  spec now splits into `VK_BASE_VERSION_*`, `VK_COMPUTE_VERSION_*` and
  `VK_GRAPHICS_VERSION_*` layers, but `proposals/VulkanBase.adoc` says
  Vulkan Base "is not published as a standalone API, and exists solely to
  enable future API prototyping" and "has no impact for current Vulkan
  applications or implementations". There is no public compute-only
  profile.
- **Multiview is required, no exemption.** `chapters/features.adoc`
  `[[features-requirements]]`: "All Vulkan graphics implementations must:
  support the following features:" followed by the generated list; for
  1.1 that list includes `multiview` (`vk.xml:20678`,
  `<feature name="multiview" struct="VkPhysicalDeviceMultiviewFeatures"/>`
  in an unconditional `<require>`). The only exemption in the source is the
  Vulkan SC deviation SCID-8. Because 1.1 depends on the graphics layer,
  every 1.1 implementation is a "graphics implementation" in this sense.
- **Queues: graphics is not required.** `chapters/devsandqueues.adoc`:
  "Implementations must: support at least one queue family." and "At least
  one queue family of at least one physical device exposed by the
  implementation must: support at least one of the following sets of
  operations: graphics operations, compute operations, video encode, video
  decode" and "If an implementation exposes any queue family that supports
  graphics operations, at least one queue family ... must: support both
  graphics and compute operations." The ChangeLog entry for internal issue
  4566 says the same. Elsewhere the spec mentions compute-only queues
  (`samplers.adoc`, custom border colours on "VK_QUEUE_COMPUTE_BIT queues
  that do not also support VK_QUEUE_GRAPHICS_BIT").
- **Required formats are unconditional.** `chapters/formats.adoc`
  `[[features-required-format-support]]`: "Implementations must: support at
  least the following set of features on the listed formats. For images,
  these features must: be supported for every VkImageType (including
  arrayed and cube variants) unless otherwise noted." The tables include
  `COLOR_ATTACHMENT`, `COLOR_ATTACHMENT_BLEND`, `DEPTH_STENCIL_ATTACHMENT`,
  `BLIT_SRC`/`BLIT_DST`, `SAMPLED_IMAGE_FILTER_LINEAR` and `VERTEX_BUFFER`
  (e.g. `R8_UNORM` requires attachment, blend, blit-dst and vertex-buffer).
  The only conditions are extension/feature footnotes, none about queues.
- **The commands that use those bits need a graphics queue.** `vk.xml`
  `queues=`: `vkCmdBlitImage`, `vkCmdResolveImage`,
  `vkCmdClearDepthStencilImage`, `vkCmdBeginRenderPass`, `vkCmdDraw`,
  `vkCmdClearAttachments`, `vkCmdSetViewport`, `vkCmdBindVertexBuffers`
  are `VK_QUEUE_GRAPHICS_BIT` only. `vkCmdClearColorImage` is
  graphics|compute; the copies are transfer|graphics|compute. A
  compute-only device must therefore advertise format features that no
  command it accepts can use.

### What the CTS does

- `--deqp-compute-only=enable` is still documented in
  `external/vulkancts/README.md` ("all non-compute tests will report as
  unsupported") but no longer exists in `framework/common/tcuCommandLine.cpp`
  at this tag. It was added in CTS commit 1431832 (2023-12-21, "Perform
  compute-only tests") and removed in f1df0b8 (2026-07-17, "Removes
  --deqp-compute-only flag from CTS"), which moved compute-capable tests to
  a multi-queue runner; the README text was removed on the main branch
  (2a4f454, 2026-10-05) but is still in the 1.4.7.1 release.
- The default device falls back to a compute-only family
  (`vktTestCase.cpp` `findComputeCapableQueueFamily`). Graphics tests have
  no general NotSupported gate: on Radish they record graphics commands on
  the compute queue and fail (`vkCreateGraphicsPipelines` /
  `vkEndCommandBuffer` return `VK_ERROR_FEATURE_NOT_PRESENT`).
- `dEQP-VK.info.device_mandatory_features` fails: "Mandatory feature
  multiview not supported" (`vkMandatoryFeatures.inl` checks it for every
  1.1 context).
- `dEQP-VK.api.version_check.entry_points` fails: "Returned queue family
  count was 0" (`vktApiVersionCheck.cpp:336`, it looks for a
  GRAPHICS|COMPUTE family).
- CTS bugs on a single compute-only family (the default "universal" and
  "compute-only" family indices are both 0):
  - `robustness/vktRobustnessUtil.cpp` requests family 0 twice in one
    `VkDeviceCreateInfo` (invalid usage), so all 232 robust-device cases
    in `dEQP-VK.robustness` fail at `vkCreateDevice`.
  - `api/copy_and_blit/*_compute_queue` create images with
    `VK_SHARING_MODE_CONCURRENT` and the same family twice.
  - `texture/vktTextureTestUtil.cpp` compute shaders use 16x16 = 256
    invocations, more than the guaranteed minimum
    `maxComputeWorkGroupInvocations` of 128 (Radish reports 128).

### Conformance submission route (not contacted)

- Khronos Adopters (https://www.khronos.org/conformance/adopters/):
  adopters sign the Adopters Agreement and pay a per-API fee (Vulkan:
  $95,000 members / $120,000 non-members); results go to the working group
  for review.
- X.Org Foundation (https://www.x.org/Khronos/): "The Khronos Group has
  accepted the X.Org Foundation as an adopter (pro bono)." The submitter
  must be an X.Org member; the driver must be open source, community-backed
  and under the X.Org umbrella; "Before submitting conformance results,
  please ask for approval to the board of directors of X.Org!"
  (board at foundation.x.org); then a Khronos account with an @x.org
  address and "Software Freedom Conservancy" as the company.
- Mesa (https://docs.mesa3d.org/conform.html): "Mesa as a project does not
  get certified conformant by Khronos"; driver teams submit, coordinating
  their first submission with board@foundation.x.org.
- No conformant compute-only Vulkan product was found in the first third
  of the Khronos conformant-products list (not exhaustive).

Unknown: whether the working group would accept a compute-only submission
with waivers, and the policy reasons behind removing the compute-only CTS
mode (internal issues 5465 and 6983 are not public).

## Fixes made in this pass

Driver commits on `radish/main` (each message carries its own numbers):

| commit | change | CTS effect |
|---|---|---|
| `3740401` | error stubs for graphics-only commands (blit, resolve, render passes, vertex/index binds) instead of calls through null pointers | 81 crashes in the 1% sample of `api` → Fail; made the full pass possible |
| `b8cac6d` | depth/stencil buffer↔image copies (D16, D32F, S8, D24S8 depth and stencil aspects), unaligned buffer offsets, `CONCURRENT` images, `OUT_OF_DEVICE_MEMORY` for oversized buffers | api: 229 of 232 compute-path failures pass; glsl and synchronization depth uploads |
| `7315ea6` | robustBufferAccess: one bounds check per access instead of per component | Kokoro warm core 32.75 → 24.04 s with RBA on (24.49 s with it off), output bit-identical; robustness compute 138/138 either way |
| `78b5ef9` | 1D-array storage images: layer count is the IBO height | 226 cases (binding_model, image) |
| `e606a6d` | storage formats the hardware mishandles go through raw same-size integer texels; `minTexelBufferOffsetAlignment` 64 | 2,501 image/binding_model cases; storage-format page faults gone |
| `368c1c9` | `RADISH_DEBUG=pipeline` names the reason a compute pipeline is rejected | diagnosis |
| `4e55cc2` | `VK_KHR_sampler_mirror_clamp_to_edge` (hardware mirror-clamp, plus the array/cube emulation) | 100 glsl cases; exposes ~10.5k more cases (above) |
| `ae11b0c` | B10G11R11 storage pack/unpack in integer arithmetic | 119 image cases |
| `4c51830` | border-colour table address in `RADISH_RESOURCE_DIAGNOSTICS` | diagnosis |
| `6eba49b` | formatless image loads/stores take the image variable's format | memory_model: 6 pipelines compile, 9 more Pass |

Runner and tooling commits in this repository: `a1f1c99`, `445f99b` and
later (`scripts/tsv/cts.py`, `cts.sh`, `cts-run-list.sh`). The ICD manifest
written by `deploy-driver.sh` said `api_version` 1.0.0; the loader then
dropped pNext feature structs and `info.device_mandatory_features` reported
storage8/16, shaderInt8 and timelineSemaphore missing. It now says 1.1.0.

### robustBufferAccess

`dEQP-VK.robustness` cannot test it unmodified on this device: every
robust-device case fails at `vkCreateDevice` because the CTS requests queue
family 0 twice (`vktRobustnessUtil.cpp`). With that one line changed
(`results/cts-1.4.7.1/robustness-*.tsv`, patch in
`/run/media/jethac/Shared/radish-src/cts-deploy/robustness-queue-dedup.patch`;
not a conformance result) the 265 applicable cases give the same results
before and after `7315ea6`: `buffer_access.compute` 138/138 Pass,
`oob_access` 31/31, `non_robust_buffer_access` 2/2, the four
`robustness1_vertex_access` cases Fail (graphics), `vertex_access`
NotSupported. `dEQP-VK.compute` stays 92 Pass, 67,461 NotSupported, 0 Fail.

## What still fails, and the next fixes

In order of size:

1. **Workgroups over 128 invocations (4,493 cases).** The CTS texture and
   sampler compute variants use 16×16. The A5xx compiler path allows 512
   (`threadsize_base × max_waves`) and Freedreno GL exposes more; Radish caps
   at 128 because a multi-wave barrier bug is unresolved
   (`radish/NOTES.md`). Next: a barrier microtest at 256/512 under the
   register budget; if it holds, raise the limit and re-run `texture`,
   `pipeline.monolithic.sampler`, `memory`, `compute`. Risk: a GPU hang on
   the shared tablet if barriers are still wrong.
2. **Border colours in compute (957 cases, and all the remaining page
   faults).** `TPL1_TP_BORDER_COLOR_BASE_ADDR` is programmed, but compute
   border lookups return wrong colours and, for integer formats, read
   0x41a680–0x41a880, outside the table (at 0x1026000 in the probe run).
   Freedreno's a5xx compute never uses border colours, so there is no
   reference; the compute-side register (or table layout for integer
   formats) has to be found. Until then these cases fault (recoverably).
3. **Small mipmapped 3D images (164).** 2×2×2 and 3×3×3 fail, 5×5×5 and up
   pass; the minimum layer size of the last level (`MIN_LAYERSZ` = 8 KiB
   when level 0 is 4 KiB) is the first suspect.
4. **UBO layouts (29), SSBO layouts (4).**
5. **Timeline semaphores, device↔host (10)**: DeviceLost/Crash only in
   sequence; isolated runs pass.
6. Not driver work: multiview (1), `api.version_check.entry_points` (1),
   robustness queue duplication (232), and the 121,120 graphics-path
   results.

What is on the tablet after this pass: driver `3c58218041a4` (`6eba49b`)
and its ICD manifest in `/var/tmp/radish-canon/lib/`. The CTS
(`/var/tmp/radish-canon/cts`, 95 MB) was removed; `cts.sh setup` pushes it
again.
