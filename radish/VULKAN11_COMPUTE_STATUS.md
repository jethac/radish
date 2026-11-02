# Vulkan 1.1 compute qualification

Status: **incomplete**. Conformance and the applicable compute/shared feature set
come before further performance experiments. The selected fcd/d332 inference
stack is frozen. The intended GPU budget is entirely compute; CPU handles
graphics. No graphics headroom reservation should be added. Any removal must
identify a real source reservation and preserve correctness/resource guards;
this is not authorization to change OS graphics or services. The private integrated candidate ac2a5bbe includes representative resource corrections. It remains separate from selected inference and is not a qualified replacement. See the [machine-readable ledger](results/vulkan11-compute-ledger-0045.json).

| Area | Evidence | Remaining work |
| --- | --- | --- |
| Public 1.1 API, queries and promoted aliases | Real public1.1 negotiation,28core names/23KHR aliases and selected error/query semantics pass | Complete promoted command semantics and applicable API CTS |
| SPIR-V1.0–1.3 and BASIC | Legal1.1 custom BASIC consumer plus all six original BASIC compute CTS cases and four SPIR-V version cases pass | Full generic shader semantics, legal geometry/precision/memory combinations and remaining applicable shader CTS |
| Limits and resource combinations | Original guards; five-buffer Q4; combined12UBO+5SSBO+4image+hidden metadata actual PASS | All applicable limits/combinations;15 remaining property messages after16 fixes; no queue-based waiver |
| Required formats | Static audit of47 rows; four packed storage formats and core SNORM endpoints/OOB now pass representative checks | ETC2/EAC sRGB numerical failure remains; complete required format/filter/transfer CTS |
| Images and transfers | Representative cube, MS, oversized-array, storage and atomic consumers | Required formats/types/views/mips/alias/copy/filter combinations and CTS |
| Buffers and robustness | Corrected legal exact-range and subword consumers; original invalid module erratum retained | Scalar/vector/array/alias/atomic and memory-model CTS |
| Descriptors and lifetimes | Static compaction, dynamic offsets, templates, two-round valid recording consumers | Pools, copies, compatible layouts, destruction/re-record and allocation errors |
| Memory and synchronization | Guarded allocations, coherent mapping, barriers/events/queries/binary and timeline consumers | Full applicable allocation/alias/cache/visibility/order/lifetime CTS |
| Advertised optional features | Scoped storage8/16, Int8 and timeline actuals | Their complete conditional contracts and applicable extension CTS; other optional bits remain truthful |
| Standard-version obligations beyond compute execution |15 current property messages, historical31 and required graphics-oriented format cells retained | Normative applicability or real implementation; no invented compute-only conformance profile |

The official pinned CTS is revision
`5a9cb32498158eb60774cf8a3d4e0b8c68f7fe9e`, original binary
`0e715f9c53ec1dfd742d93a7a45740cbf4a8caa9193fe761e71dd6347c2a6660`.
The exact original binary now starts and completes all five information cases on
the real public1.1 candidate: four Pass, one `device_properties` Fail. Its31
messages match the retained property failures; no applicability waiver or fake
values were introduced. [Exact QPA/status evidence](results/official11-cts-info-0045.json)
records the nonzero exit and clean postflight. These are official case results,
not full conformance. The earlier crash and modified instance-alias harness
remain preserved diagnostics; no modified executable was used for this result.
The source-supported old alias explanation remains an inference without a PC.

The combined namespace actual uses the frozen1559 driver, not the new1.1
candidate. It proves parent17 buffers, child metadata17 and actual typed image
banks18–21 with public robustness, no forced sampling mode,560 exact words,
full21 small allocations and196 sparse large-image words per round. It does not
close every resource combination or a full128MiB image scan.

Requirements are traced to the [Vulkan1.1 version contract](https://docs.vulkan.org/refpages/latest/refpages/source/VK_VERSION_1_1.html)
and pinned [Vulkan1.1.126 source](https://github.com/KhronosGroup/Vulkan-Docs/tree/v1.1.126).
Promoted optional features are tracked conditionally; an API version alone does
not require advertising every optional bit. The category inventory is being
reconciled with the registry and original CTS case inventory. It is not a test
pass percentage or completion certificate.

After applicable conformance closes, the complete Straylight campaign resumes:
model accuracy, actual GPU placement, safe peak memory, service coexistence,
thermal behavior, end-to-end deployment and material GPU efficiency. ASR host
link and33 native-only cases still do not admit a full model within640MiB.

## Public1.1 BASIC and contract checkpoint

The separate d6b92001 driver and public helper use actual instance1.1 plus a
validated SPIR-V1.3 module. All API actions resolve through public GIPA/GDPA;
there is no internal feature override. Negotiation for requests0/1.0/1.1/1.2,
28core1.1 names,23 supportedKHR aliases, individual feature/property queries,
unsupported-feature/extension errors and unsupported external-handle queries
pass. Name lookup does not establish each command's complete semantics.

The original five local shapes32,33,8×8,128 and3×5×3 pass302 complete records
(4,832 words), full output/exchange allocation guards and five successful
submissions. Native resources remain8F1H/shared64/private0. The functional scope
takes589ms,7MiB,zero swap; both services stay active and fault checks are clean.
[Source, build and independent actual receipts](results/public11-basic-0045.json)
retain exact pins. The historical API1.0/SPIR-V1.3 captures remain internal
observations; this new legal run does not retroactively qualify them.

Apply [the private1.1 contract increment](patches/0045-radish-public11-contract.patch)
after the five-SSBO checkpoint. The existing builder produces `public11` and
`combined`; stage the chosen executable/module as `resources`/`resources.spv`
for the corresponding `device-core-public11.py` or `device-core-combined.py`
split runner. Use matching internal headers, exact driver hashes and original
compile-before-functional/resource/fault gates. These scripts never change the
system ICD or production services. Full reproduction commands and original
payload pins are in the receipts.


The initial official shader partition now passes: four SPIR-V version cases and
six BASIC compute cases, with seven built-in shapes per BASIC case. The five
barrier cases ran sequentially in one unchanged 90-second/384MiB scope, with
separate QPAs and a kernel-fault check before each next case. All 35 barrier
dispatches passed; peak memory was 85.9MiB with no swap. See the
[exact case results](results/official11-cts-shaders-0045.json). Together with the
information partition, 15 executed cases comprise 14 Pass and one Fail. This is
an executed inventory, not the required-case denominator.

The packed storage-format consumer exposed a SNORM load defect. Its original
output check failed, and a separate diagnostic captured incorrect raw load bits
before scaling, rounding or integer conversion. All original oracles remain
unchanged; the failure is [retained explicitly](results/packed-snorm-failure-0045.json).
The diagnostic changes compiler lifetimes and cannot reconstruct the earlier
capture's unobserved state. The subsequent signed-descriptor/NIR correction passes the unchanged original oracle and a separate legal RGBA8 endpoint/OOB supplement; see [the fixed result](results/snorm-storage-fixed-0045.json). This remains representative format qualification.

The ETC2/EAC candidate reuses Mesa's block-aware image helpers and FDL5 layout.
Its independent CPU reference reuses the ISC-licensed
[detex decoder at the pinned revision](https://github.com/hglm/detex/tree/cab11584d9be140602a66fd9a88ef0b99f08a97a),
with original notices and a small defined-integer patch retained. Host sanitizer checks and the native gate pass. Format0 passes two rounds; format1 sRGB fails its unchanged numerical oracle. A diagnostic shows the four sampled taps exactly average to the filtered GPU value, leaving decode/conversion precision unresolved. [Failure and source provenance](results/etc2-eac-partial-0045.json) remain explicit; no complete family or compression-feature promotion is claimed.

The integrated ac2a replay of the same original five information cases remains
four Pass and one Fail, with exactly 16 of the original 31 messages removed and
15 remaining. The actual query now reports maxFramebufferLayers256, closing a
normative gap that this older CTS did not flag. [Exact remaining messages and
source-backed changes](results/official11-integrated-info-0045.json) are retained;
there is no compute-queue waiver or conformance claim.

The [bounded resource batch](results/compute11-resource-batch-0045.json) passes
INPUT_ATTACHMENT host descriptor lifetimes, core SNORM endpoints/OOB, mixed-stage
push constants with timeline operations, and tiny attachment image sampling,
layout and copy paths. It performs five dispatches under the original controls.
These host/image checks do not qualify rendering or fragment input execution.

Depth-layout erratum: DEPTH_STENCIL_READ_ONLY_OPTIMAL requires attachment usage
in the pinned1.1 contract. Earlier sampled-only depth consumers need valid-usage
requalification for that path. The new depth source/consumer is a later private
candidate and is not part of the ac2a result.

The [published patch stack](results/compute11-private-source-stack-0045.json)
reconstructs ac2a exactly. Apply its seven increments after the public1.1 patch,
in the listed order. `scripts/build-core-compute.py --only packed-storage
snorm-storage mixed-push attachment descriptor-host etc2` selects the added
consumers (also supply the existing builder's required build/header paths).
The ETC source is included with its failed qualification clearly marked.

For the consolidated resource scope, stage builder outputs as `descriptor_host`,
`core`/`core.spv` (snorm-storage), `mixed`/`mixed.spv`, and
`attachment`/`attachment.spv`. Use `device-core-resource-batch.py` compile then
probe with the same immutable payload and matching internal headers. The
[build and runner reproduction receipt](results/compute11-reproduction-0045.json)
records actual builder validation and the sole repository-path normalization.

## Further bounded shared/resource results

Four original `shared_var_*` cases and two `ssbo_cmd_barrier_*` cases now pass
on frozen ac2a. They cover shared ordering and numerical results, host UBO
visibility, a compute-to-compute SSBO dependency, compatible pipeline switching
with retained descriptors, atomic sums and readback in their fixed source
configurations. [Exact cases and original CTS evidence](results/official11-shared-command-0045.json)
remain distinct from complete memory-model or synchronization coverage.

The separate c2aa depth/layout candidate passes six D16/D32/S8 single/four-sample
host-object cases and24 maintenance2 layout transitions with fenced reuse.
There are no shaders, initialized samples or rendering in this scope; required
framebuffer masks and combined depth/stencil qualification remain open.
[Source and actual evidence](results/depth-host-layouts-0045.json) include the
valid-usage correction for depth/stencil read-only layout. Build `--only
mixed-depth`, stage it as `resources`, and use `device-core-mixed-depth.py probe`.

The [full fixed ETC2/EAC observation inventory](results/etc2-eac-family-observations-0045.json)
completes all20 cases with complete guards: seven non-sRGB formats pass both
fixtures; the three sRGB variants produce42 linear mismatches across six cases.
All nearest checks pass (encoded-domain comparisons for sRGB, not exact EOTF),
and no nonfinite values or API/guard/fault failures
occur. The overall result remains **numerical failure**. A compatible UNORM
view of the same compressed bytes reproduces all16 encoded channels at the
original failing location exactly, narrowing that discrepancy to the native
sRGB conversion path. The normative precision obligation is still unresolved;
no tolerance or feature flag was changed. Build `--only etc2-observe` and stage
as `resources`/`resources.spv` for the split `device-core-etc-observe.py` runner.
Numerical-only exit2 is recorded as failure; API, guard and GPU faults stop
before the next case. Fresh per-case images do not claim two-round image reuse.

## Initialized D24S8 and another22 original CTS cases

The9407 resource checkpoint passes two initialized single-sample D24S8 rounds:
256 sampled words,32 exact packed readback texels and full allocation guards.
Eight depth and eight stencil components genuinely change while each copy
preserves the opposite aspect. The first fixture copied identical depth values;
that earlier pass remains sampling/stencil evidence only. The corrected input,
unchanged shader and all actual limits are in [the paired record](results/d24s8-public-0045.json).
This does not qualify multisample initialization, rendering, raw children or
framebuffer sample masks. The compressed BLIT_SRC query traits follow the
reviewed graphics-command applicability basis; no blit ran on the compute queue.

Another22 original CTS buffer cases pass14,422 exact integer values under the
unchanged90s/384MiB controls. They cover UBO inversion, SSBO copies, fixed/runtime
arrays, in-place and multiple-output updates, local barriers and shared atomicAdd.
[Individual cases and QPA outcomes](results/official11-buffer-group-0045.json)
remain separate from complete memory/atomic conformance. The current43 distinct
executed cases have42 passes and one properties failure; this is an inventory,
not a percentage or denominator of required coverage.

The new [same-byte uncompressed sRGB discriminator](results/etc2-rgba8-srgb-comparison-0045.json)
reproduces all16 compressed sRGB tap bits with RGBA8_SRGB. The original filtered
numerical failure persists. This identifies shared native conversion behavior
for the fixture; it does not establish a normative precision allowance or justify
changing the oracle. ETC2 remains false in this checkpoint. Reproduce this older
d903 diagnostic with its frozen Radish headers through `--radish-headers`; the
new D24S8 transfer structure has a different private ABI.

## Public core function lookup

The pinned public loader exposes all165 core1.0/1.1 names through GIPA. Its
baseline device lookup missed five of137 required names; the corrected1cd9
driver resolves all137. Four missing commands are graphics-only and now have
typed entrypoints that record errors for invalid use on the compute queue. The
remaining dense-memory commitment query is implemented, but the driver has no
lazy memory type, so this probe does not claim a valid query execution.
[The actual pair and source](results/core-lookup-public-0045.json) include zero
commands, shaders or data allocations and clean fault/service checks.

An earlier direct-ICD check also missed the loader-owned global layer
enumeration name. That was a validation-route error, preserved separately;
public-loader validation resolves it without adding a fake layer function.
Pointer completeness does not establish every function's semantics or Vulkan
conformance. The compression-feature candidate remains separate and unqualified.
