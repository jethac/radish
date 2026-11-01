# Core compute implementation: checkpoint 0044

The consolidated A506 run passes three functional suites: API5, events/queries,
and native resources. Subgroup BASIC compiles but fails its first local32
election-count oracle. The batch is therefore **partial success**, not a driver
conformance pass. No GPU faults occurred; both services stayed active. Published source reports
subgroup stages/operations as zero until this is fixed; the measured private
qualification build enabled BASIC and the diagnostic helper explicitly enables
it locally. This two-field safety change is distinct from the measured binary.

Apply `0044-radish-resource-layout-storage-wip.patch` after the foundation and
combined resource WIP patches to reproduce this source. It adds the broader
format/layout/mip/cube code, border colors, compact buffer slots, pipeline-owned
private/constant storage and lost-queue cleanup correction. Those added source
paths are not all exercised by the passing suites. The measured private ICD is
`e4bd958f474249670e9df455af6660db91ba8fced590b8997d71dfa641870bb2`.
See [the per-command/result record](results/core-api-0044-consolidated.json).

The original40 worklist now has36 representative paths exercised, including
common-wrapper routes for two1.1 image APIs; sparse-image queries, CmdCopyImage
and CmdClearColorImage remain build-only. This count does not establish complete
semantics. Public core1.1 names remain filtered under API1.0. BASIC is not accepted;
required limits, large descriptor layouts, dynamic resource indexing, legal
shared-memory/workgroup execution, broader formats/views and applicable CTS
remain active work. Performance remains paused.

The service used2.286s,7.3MiB and zero swap. API5 checked two4,160-invocation
runs; events/query checked two256-invocation runs plus host-release/events,
statistics, timestamps, result availability/copies/reset; resources checked
two85-record runs with full outputs/guards, dynamic UBO/SSBO offsets, updates
after recording, typed texels/images and nearest/linear sampling.

An initial runner attempt stopped before probes because Python was absent on
the tablet. A second stopped before functional execution on a probe-only
16-bit/32-bit memcmp bug. Both failed captures remain preserved. The final runner
uses awk for between-process fault checks, and the helper compares numeric
local dimensions. It continues across independent non-fault failures and stops
on new fault evidence. The public runner changes only root-path discovery from
the measured private script.

## Consolidated reproduction

Apply the three0044 incremental patches in order to the documented0042 Mesa
stack and build the isolated driver. Build the matching internal-ABI probes:

```sh
python3 radish/scripts/build-core-compute.py "$API_WORK/build" "$API_WORK/compute-probes"
exec 9>"$ORIGINAL_DEVICE_LOCK"
flock -w 1800 9
python3 radish/scripts/device-core-compute.py probe combined-first \
  --driver "$API_WORK/build/src/freedreno/radish/libvulkan_radish.so" \
  --driver-sha256 "$DRIVER_SHA" --probe-dir "$API_WORK/compute-probes" --lock-fd 9
```

Set `RADISH_DEVICE_LOCK` to the same original lock and `RADISH_WORK` to the
host evidence directory. The private driver is selected only for the bounded
service. The current BASIC failure makes the overall process exit1 while
preserving the three passing suite outcomes; do not reinterpret that as a full
pass. No separate human gate is inserted between compile and execution checks.

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
