# Vulkan 1.1 compute qualification

Status: **incomplete**. Conformance and the applicable compute/shared feature set
come before further performance experiments. The selected fcd/d332 inference
stack is frozen. The intended GPU budget is entirely compute; CPU handles
graphics. No graphics headroom reservation should be added. Any removal must
identify a real source reservation and preserve correctness/resource guards;
this is not authorization to change OS graphics or services. The separate f222 validation candidate includes representative resource corrections and the source-backed subpixel query fix. It remains separate from selected inference and is not a qualified replacement. See the [machine-readable ledger](results/vulkan11-compute-ledger-0045.json).

| Area | Evidence | Remaining work |
| --- | --- | --- |
| Public 1.1 API, queries and promoted aliases | Real public1.1 negotiation,28core names/23KHR aliases and selected error/query semantics pass | Complete promoted command semantics and applicable API CTS |
| SPIR-V1.0–1.3 and BASIC | Legal1.1 custom BASIC consumer plus all six original BASIC compute CTS cases and four SPIR-V version cases pass | Full generic shader semantics, legal geometry/precision/memory combinations and remaining applicable shader CTS |
| Limits and resource combinations | Original guards; five-buffer Q4; combined12UBO+5SSBO+4image+hidden metadata actual PASS | All applicable limits/combinations;all 31 observed property messages closed; broader normative limits/CTS remain; no queue-based waiver |
| Required formats | Static audit of47 rows; four packed storage formats and core SNORM endpoints/OOB now pass representative checks | ETC2/EAC sRGB numerical failure remains; complete required format/filter/transfer CTS |
| Images and transfers | Representative cube, MS, oversized-array, storage and atomic consumers | Required formats/types/views/mips/alias/copy/filter combinations and CTS |
| Buffers and robustness | Corrected legal exact-range and subword consumers; original invalid module erratum retained | Scalar/vector/array/alias/atomic and memory-model CTS |
| Descriptors and lifetimes | Static compaction, dynamic offsets, templates, two-round valid recording consumers | Pools, copies, compatible layouts, destruction/re-record and allocation errors |
| Memory and synchronization | Guarded allocations, coherent mapping, barriers/events/queries/binary and timeline consumers | Full applicable allocation/alias/cache/visibility/order/lifetime CTS |
| Advertised optional features | Scoped storage8/16, Int8 and timeline actuals | Their complete conditional contracts and applicable extension CTS; other optional bits remain truthful |
| Standard-version obligations beyond compute execution |All31 observed property messages closed; required graphics-oriented format cells and valid domains retained | Normative applicability or real implementation; no invented compute-only conformance profile |

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
256 sampled words, 32 exact packed readback texels and full allocation guards.
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

## Required atomics, descriptors and compressed sampling

Nine 32-bit atomic operations pass on signed/unsigned storage images and texel
buffers: 1,152 old/final output words and all five allocation guards. Distinct
addresses per invocation limit this to operator semantics, not competing-address
ordering. [Atomic evidence](results/required-image-atomics-0045.json) includes the
public build and retained native checks. [Seven original descriptor cases](results/official11-descriptor-0045.json)
also pass, including pool reset/exhaustion, layout lifetime and two compute dispatches.

All ten approved original ETC2/EAC compute sampling cases now pass on a41b.
The first case had failed because the driver reported subpixel precision zero,
below the required minimum; that value invalidated an original CTS CPU-reference
precondition. Its captured GPU output matched all 676 bytes of an independently
reconstructed seed texture. The source-backed correction reports four bits,
matching Freedreno. The first native programs remain byte-identical, and the
original case, shader and thresholds remain unchanged. The first corrected case
and then the original nine-case suffix passed, without repeating the first case.
[Exact source and actual results](results/official11-etc2-subpixel-0045.json)
preserve the [original failure and pixel diagnosis](results/official11-etc2-first-failure-0045.json).
The independent texture reconstruction is not a captured CTS reference image.

The stricter custom sRGB tests still contain 42 mismatches. These bounded original
CTS passes do not settle that precision obligation or establish full conformance.
The isolated ETC2 feature candidate stays separate from selected inference.

One original information batch on the coherent candidate remains **four Pass,
one properties Fail**. Its messages fall from 15 to 9, with no new messages:
eight vertex/fragment properties and `maxSampleMaskWords` remain.
[The complete comparison](results/official11-subpixel-info-0045.json) preserves
the earlier 31 and 15-message checkpoints. Counts are not a normative coverage denominator.

Future grouped runners use one approved case file, pinned by hash and count
before staging, and derive execution and collection from that same file. The
historical duplicated-inventory setup rejection is preserved; it ran no CTS.

New native-format/transient changes are a separate host-built candidate. The
next consumer is being corrected for newer unconditional graphics-queue validity
rules for render-pass/framebuffer host construction. Earlier such host-object
captures remain implementation observations; they do not by themselves qualify
those calls on a device exposing only a compute queue. Image/query/view/layout
coverage and graphics execution are separate obligations.

## Valid image resources and the information-query checkpoint

The corrected consumer passes 51 native-color and 7 sampled-only format queries,
16 image/view cases,80 image bindings/views,192 layout barriers and16 fences.
It includes dense transient images and calls no graphics-only render-pass,
framebuffer or graphics-pipeline functions. [The actual source and result](results/native-attachment-resources-0045.json)
qualify those public image/query/view/layout operations, not initialized samples
or rendering. Earlier graphics-host-object successes remain preserved as
implementation observations under the clarified graphics-queue validity rules.

The coherent candidate now passes all five original information cases, including
`device_properties`. The recorded message history is 31→15→9→0. The last nine
values come from matched FD5/IR3 hardware representation and conservative native
limits; they do not reserve compute resources for graphics or expose a graphics
queue. [Exact fields, reconstruction and actual QPAs](results/official11-native-scalars-info-0045.json)
retain every earlier failure and the current zero-message result.

This closes the observed information-query failures. It does not close the
complete normative matrix, shader/resource/memory/synchronization CTS or the 42
strict custom sRGB mismatches. Selected inference and all original device limits
remain unchanged; conformance still precedes performance work.

## Query lifetimes and required format traits

Eighteen further original CTS cases pass: eight for the advertised optional
pipeline-statistics feature and ten core timestamp cases. The statistics group
checks query values and 1,203 SSBO words across 22 dispatches, including copy,
offset, secondary-command and reset paths. Timestamp cases check the original
reset and 32/64-bit consistency conditions. Some original availability/ordering
checks are weak or vacuous, and their workload SSBO is not numerically checked.
[The exact cases, source bounds and results](results/official11-query-lifecycle-0045.json)
retain those limitations; these runs are correctness checks, not benchmarks.

The separate 8298 candidate adds 14 required native-backed BLEND format-query
traits. All 14 positive and 4 nonblend controls pass through both legacy and core2
queries. [The source and actual query evidence](results/required-blend-query-0045.json)
include no device creation, graphics host objects or blending execution. Broader
format, precision, memory and shader conformance remains open, including the 42
strict sRGB mismatches. The selected inference stack remains unchanged.

## Promoted queries, storage views and format traits

Thirteen original CTS cases now pass for core2 equality, six individual Vulkan
1.1 feature structures, and maintenance3. All six feature-query branches ran;
the descriptor case checked all 2,047 nonempty type combinations without resource
allocation. False optional feature bits remain false. [The exact source and results](results/official11-promoted-query-0045.json)
distinguish query consistency from feature execution.

Eight cache/lifetime cases passed initially. A ninth requested path matched zero
tests, and the wrapper correctly failed its inventory check. Only the corrected
registered lifetime case ran next; its 100-word oracle passed. [Both captures](results/official11-cache-lifetime-0045.json)
retain that error and the original oracle limitations. The destroyed object is
the pipeline layout; the executing pipeline remains alive through the fence.
Current cache serialization contains header/count metadata, without populated
pipeline entries. Cache population and measured reuse belong to the later
performance phase.

The existing EXTENDED_USAGE/block-compatible image path passed a bounded
RGBA32_UINT storage-view probe: 64 output words, 32 modified block readback words,
complete buffer guards and image outer guards across two rounds on the same
image. [This evidence](results/extended-block-storage-0045.json) does not qualify
compression decoding or graphics execution.

The new f222 source adds native-backed mandatory BLIT and VERTEX_BUFFER query
traits. All 99 positive/negative queries passed through legacy and core2 calls:
47 BLIT_SRC, 38 BLIT_DST and 45 VERTEX_BUFFER mandatory rows are covered. [The exact
source and query evidence](results/required-blit-vertex-query-0045.json) contains
no graphics commands. Full applicable conformance and the 42 strict sRGB
mismatches remain open. The selected inference stack is unchanged.

The retained result inventory contains 100 unique original CTS cases with a Pass
result, including the corrected single lifetime case. [The deduplicated inventory](results/official11-qualified-case-inventory-0045.json)
spans separately pinned candidates and preserves the original oracle limitations.
It is neither a single full conformance run nor a percentage of applicable
requirements. Historical failed captures remain available; the custom 42 strict
sRGB mismatches are still unresolved.

A separate core cache consumer also passed one guarded 32-word compute dispatch
following merge, serialization, recreation and layout destruction before submit.
[Its four cache blobs](results/cache-merge-lifetime-0045.json) are 36 bytes each:
32-byte header plus a zero object count. This qualifies callable lifetime behavior,
not populated-entry movement or cache hits. Ordinary private-array behavior has
separate evidence; deliberate compiler-spill qualification remains open.

The singleton full split-region binding observation is invalid public coverage
under VUID01421. It is retained as an erratum, separate from valid singleton
mask/bind2 paths. This does not establish a driver implementation gap; applicable
valid device-group and external-capability CTS still remain.
