# Vulkan 1.1 compute qualification

Status: **incomplete**. Conformance and the applicable compute/shared feature set
come before further performance experiments. The selected fcd/d332 inference
stack is frozen. The intended GPU budget is entirely compute; CPU handles
graphics. No graphics headroom reservation should be added. Any removal must
identify a real source reservation and preserve correctness/resource guards;
this is not authorization to change OS graphics or services. The private API1.1 candidate d6b92001 now passes the bounded public contract and five BASIC cases; it is not a
qualified replacement. See the [machine-readable ledger](results/vulkan11-compute-ledger-0045.json).

| Area | Evidence | Remaining work |
| --- | --- | --- |
| Public 1.1 API, queries and promoted aliases | Real public1.1 negotiation,28core names/23KHR aliases and selected error/query semantics pass | Complete promoted command semantics and applicable API CTS |
| SPIR-V1.0–1.3 and BASIC | Legal1.1 custom BASIC consumer plus all six original BASIC compute CTS cases and four SPIR-V version cases pass | Full generic shader semantics, legal geometry/precision/memory combinations and remaining applicable shader CTS |
| Limits and resource combinations | Original guards; five-buffer Q4; combined12UBO+5SSBO+4image+hidden metadata actual PASS | All applicable limits/combinations,31 retained property failures and additional maxFramebufferLayers obligation; no queue-based waiver |
| Required formats | Static audit of47 compute-relevant rows; four packed storage-texel admissions need correction | Packed SNORM numerical failure to fix; ETC2/EAC host implementation and independent decoder ready, GPU qualification pending; applicable full format/filter/transfer CTS |
| Images and transfers | Representative cube, MS, oversized-array, storage and atomic consumers | Required formats/types/views/mips/alias/copy/filter combinations and CTS |
| Buffers and robustness | Corrected legal exact-range and subword consumers; original invalid module erratum retained | Scalar/vector/array/alias/atomic and memory-model CTS |
| Descriptors and lifetimes | Static compaction, dynamic offsets, templates, two-round valid recording consumers | Pools, copies, compatible layouts, destruction/re-record and allocation errors |
| Memory and synchronization | Guarded allocations, coherent mapping, barriers/events/queries/binary and timeline consumers | Full applicable allocation/alias/cache/visibility/order/lifetime CTS |
| Advertised optional features | Scoped storage8/16, Int8 and timeline actuals | Their complete conditional contracts and applicable extension CTS; other optional bits remain truthful |
| Standard-version obligations beyond compute execution |31 property messages and required graphics-oriented format cells retained | Normative applicability or real implementation; no invented compute-only conformance profile |

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
capture's unobserved state. No packed SNORM qualification is claimed.

The ETC2/EAC candidate reuses Mesa's block-aware image helpers and FDL5 layout.
Its independent CPU reference reuses the ISC-licensed
[detex decoder at the pinned revision](https://github.com/hglm/detex/tree/cab11584d9be140602a66fd9a88ef0b99f08a97a),
with original notices and a small defined-integer patch retained. Host sanitizer
checks and the ten-format consumer build pass; GPU qualification is separate.

Property source work corrects feature-disabled defaults and implements shared
input-attachment descriptor objects. Attachment image and mixed-stage push
increments are separate host candidates. They are not counted as actual CTS
passes. The normative ledger also includes maxFramebufferLayers: the current
zero value fails the required minimum256 even though this pinned CTS path does
not report it. The31 observed messages are not an exhaustive requirement list.
