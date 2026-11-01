# Radish status

## 2026-11-02 (JST): normalization and AffineSnake fused

The opt-in `norm-affine-snake-deconv2d` graph replaces 42 sole-consumer
normalization/AffineSnake chains with `RadishNormAffineSnake`. Fixed full-core
first/warm extraction is **36.409648 / 35.730754 s**, versus 37.355882 / 37.058738 s
at the accepted 0034 checkpoint. Warm improves **1.327984 s / 3.58%**. Both full
562,342-value references pass and output is byte-identical to 0034
(`e128ce63…d4d287`). These are one first/warm pair per checkpoint with different
binaries and execution histories. Peak is 401.9 MiB plus 11.6 MiB swap, versus
402.8 MiB plus 6.7 MiB previously. No new GPU faults; both live services active.

Attribution accounts for the gain: 84 old final-stage commands become 42,
reducing affected warm command time 3.577951791 → 2.280919922 s (1.297031869 s).
All 1478 retained signatures match: 672 statistics commands move under the
composite owner, and 806 other commands remain, including 96 transfers and
all 58 tiled math commands. Retained work saves only 0.029873014 s in this pair.
Totals are **1520 commands / 10235 DRM submits / 67163 chunks**, down from
1562 / 11663 / 78587. The exact graph has 594 layers / 755 blobs and executes
**589 GPU / zero CPU layers**. Fewer graph layers represent fused work, with
all 226 weighted rows, 96 MemoryData rows and every stored weight preserved.
Command timers include host work and waiting; they are not GPU timestamps.

The complete short/short/full/full speech loop passes all four unchanged
original-ORT core and waveform references. Synthesis takes
**27.427916 / 18.163352 / 45.676598 / 42.819037 s**; core extraction takes
13.788183 / 13.654213 / 35.978882 / 36.437867 s. All eight boundary inputs and
both outputs are byte-identical to matching 0034 captures. Short/full waveform
SNR remains 97.411696248 / 97.912821255 dB. Whole-session peak is 531.4 MiB plus
57.2 MiB swap; session is 137.660401781 s including 0.296271739 s cleanup,
service 137.771 s. Resident full/short improve over 43.578287 / 18.589849 s,
but CPU remains faster at 36.802922 / 15.411271 s. The CPU measurement uses
the earlier equivalent unfused graph. Single observations and different memory
pressure limit comparisons; sustained operation and live application integration
remain open. Runtime reproduction is in `RUNTIME_INTEGRATION.md`.

Incremental ncnn patch 0035 exposes existing normalization coefficients without
changing legacy arithmetic or allocation order. The project composite allocates
a fresh output, obtains owning coefficients, then immediately records its final
shader. Raw and residual aliases remain read-only. The shader preserves explicit
FP32 normalization before style/Snake arithmetic and uses the stored reciprocal.
It requires exact Radish A506, FP32 pack4, C128/256, known shapes and SUM4; no
precision relaxation or coefficient folding. The six shared-normalization
AffineSnake branches and twelve remaining InstanceNorm chains retain their paths.
OUTPLACE/RESHAPE_ALIAS still control eligible legacy norms independently.

Both ARM builds and the runtime pass. Actual focused and full-model A506 finals
use seven SSBOs, local32x1x1, 8 full/1 half registers, native length4, no loops,
shared or private memory; unchanged batch8 admission applies. All 23 tiled
specializations retain their accepted resources. Focused CPU pack1/4 and GPU
pack4 compare full original/composite outputs and residual sums bit for bit,
check independent double references, and preserve all five input buffers.
Full same-runner host CPU proof checks every output and exact original bytes.
The first host run caught a real delegation error: x86 InstanceNorm does not
support packing, and the composite bypassed Net's conversion. A seven-line
conditional conversion restores that child contract; the full rerun passes.
ARM's packing-capable child and all GPU code are unchanged by that correction.
The initial failing evidence is retained. Source review alone missed this error.

Pins: graph `ccab4ff803f05c6ca1f9dbc86b557e28d72488c94f0584f149108523b474aa43`;
patch `d468a7b2dcadda77b4e06a286eba806aac28c6222f0f3ec9e8330cddf8dba657`;
model `0325d04a…8af863`; runtime `56c8969c…1b74c2`. Driver 0032 remains
`c8fee967…cb44c`; this adds no Vulkan conformance claim. Evidence under WORK:
`norm-affine-implementation-0035-ltsusd7j/`,
`norm-affine-compiler-audit-qts93nec/`, `norm-affine-result-0035-9bf3sq64/`,
`norm-affine-actual-review-ne_ex_bj/`, and
`norm-affine-runtime-validation-zat6_thf/`. Independent runtime profile audit:
`norm-affine-runtime-audit-2erfkd8e/REPORT.txt`. Four full reference reports are
`norm-affine0035-request-{000,001,002,003}-validation.json`.

Before runtime staging, two identical immutable library paths were deduplicated.
Eight completed older executable paths (three inode groups, 0031 deconv,
0028/0029 and 0033 padded-channel runs) were archived with verified host backups,
metadata and a hardlink-preserving restore script. Captures, outputs, models,
the CPU baseline and 0034 executable remain. See
`dedup-runtime-assets-before-norm-affine-0035*`,
`archive-probes-before-norm-affine-0035.log` and
`device-probe-executable-archive-before-norm-affine-0035/`.
Free storage changed 188376 → 230056 → 263688 KiB; services stayed active.

### GPU efficiency remains open

The independent frozen-0034 audit attributes 24.478507659 s to 58 tiled math
commands: 267.030343680 useful GFLOPs / 270.312407040 issued padded GFLOPs,
**10.909 useful GFLOP/s on host command wall**. Padding wastes only 1.214% of
arithmetic. Neither the earlier nominal 124.8 GFLOP/s formula nor the old GLES
GEMM result establishes a measured hardware ceiling. Source traffic counts are
not physical DRAM bandwidth; compiler resource limits are not occupancy data.

The audit identifies repeated shared-memory loads/barriers and serialized
per-chunk restore/preload/WFI/flush as concrete candidates. 98.924% of math wall
lies inside submit-to-fence intervals, which include GPU work and host/kernel
waiting. The next bounded diagnostic is GPU elapsed samples around an unchanged
accepted eight-chunk batch, followed by same-driver arithmetic and streaming-copy
calibration. Existing K2, vector-bias, X2 and smaller-tile rejections remain valid;
none proves saturation. Report `tiled-headroom-0034-n88h8l_j/REPORT.md`, SHA
`d110308ae6be1393fe352fedb6449528c1a6b203176f84a238f36ab0567f7f7e`, pins all
58 per-layer counts and source evidence. The goal continues beyond a CPU win.

## 2026-11-02 (JST): normalization input copies removed

Opt-in ncnn patch 0034 removes 49 normalization input copies. In the same-binary
comparison, full-core warm extraction improves **38.467278 → 37.058738 s**
(1.408540 s / 3.66%); first extraction improves 39.186235 → 37.355882 s.
All 562,342 output values are byte-identical to both the disabled path and the
published 0033 output (`e128ce63…d4d287`), and pass the unchanged full reference.
These are one first and one warm extraction per mode, measured sequentially.
Baseline peak is 405.6 MiB with zero swap; candidate peak is 402.8 MiB plus
6.7 MiB swap. Both runs have 715 GPU/zero CPU core layers, no new GPU faults,
and active appliance services. CPU remains faster overall.

The complete short/short/full/full loop passes all four original-ORT core and
waveform references. Complete synthesis takes 28.003366/18.589849/46.939051/
43.578287 s; core extraction takes 14.223749/14.054969/37.448352/37.088662 s.
The retained full request improves over the prior 45.500714 s observation;
short stays essentially flat against 18.498217 s. CPU remains ahead at
36.802922/15.411271 s resident full/short. Different session order, cache history
and swap limit the earlier-GPU comparison. All eight boundary files and both
outputs are byte-identical to matching validated 0033 captures, with 715 GPU
layers per request. Short/full waveform SNR remains 97.411696248/97.912821255 dB.
Whole-session peak is 504.9 MiB plus 84.2 MiB swap, with no faults and both
services active; session time is 137.809958348 s including 0.456133282 s cleanup.
External cache entries are 177/177/271/271, stable on same-shape reuse.
Exact reproduction and limitations are in `RUNTIME_INTEGRATION.md`; frozen
references are in `WORK/norm-outplace-runtime-validation-drx8p6l9/` and
`norm-outplace0034-request-{000,001,002,003}-validation.json`.
The independent runtime profile/provenance audit is in
`WORK/norm-outplace-runtime-audit-0034-gxlmdgfl/`.

`RADISH_NCNN_NORM_OUTPLACE=1` requires Vulkan FP32, `RADISH_NCNN_NORM_SUM4=1`
and `RADISH_NCNN_RESHAPE_ALIAS=1`. It remains default off. Eligible named pack4
normalizations use a rank3 input view and explicitly allocate a disjoint output,
so retained Split residuals are preserved without protective cloning. The five
pack1 chains and deconvolution input reshapes retain their existing paths.
The graph, weights, reduction order and final `v*a+b` arithmetic are unchanged.
Creation checks the exact Radish A506 identity, shape, precision and pipeline
contracts; execution checks buffer ownership, strides, bounds and disjointness.

The complete profile contains 1611 → 1562 commands, 13179 → 11663 DRM submits
and 90715 → 78587 chunks. Exactly 49 input copies disappear and 49 final
normalization dispatches use the separate output binding. All 1513 remaining
command signatures match, including the unchanged 96 transfers. Unique FIFO
ownership mapping accounts for changed flush boundaries (93 → 74 partitions).
The removed copies cost 1.618795382 s; replacement finals cost 0.052835442 s
more, for 1.565959940 s saved in the affected work. Remaining command time grows
0.167344535 s, accounting for most of the difference from the whole-core gain.
Command timing includes host submission and waits; it is not isolated GPU time.

Both ARM64 builds pass. Actual A506 compilation covers the four full-model
shape families and four focused widths: new and old finals use local 8×1×4,
8 full/1 half registers, native length 4, and no loops or private memory.
Their native instruction sequences differ only in the final output binding.
The focused retained-residual probe checks all 36,606 normalized/residual-sum
values, exactly matches the old output bytes, and removes precisely four input
dispatches while preserving all 70 other dispatch signatures. Driver 0032 and
the experimental Vulkan 1.0 advertisement are unchanged; this adds no conformance
claim.

Evidence under `WORK`: `norm-outplace-implementation/`,
`norm-outplace-private-e7ffhsde/`, `norm-outplace-review-96c5i3nh/`,
`norm-outplace-compiler-audit-gr5snlul/`,
`norm-outplace-result-0034-6xx9oeni/`, and
`norm-outplace-output-0034-3co1hdcg/`. The model binary is
`4ccf2730…735999a`; the runtime binary is `d1f09b2d…5f12ecb`.

Before the complete runtime run, two byte-identical immutable runtime library
paths were deduplicated, retaining every path. Four older full/short runtime
executable paths from the 0026 planar-pad64 and 0031 affine-index runs were then
archived with verified host backups, original metadata and restore instructions.
Those four executables are absent on-device; their captures and outputs remain.
See `dedup-runtime-assets-before-norm-outplace*`,
`archive-probes-before-norm-outplace.log`, and
`device-probe-executable-archive-before-norm-outplace/`. Free space changed
197088 → 243168 KiB, then 242876 → 265256 KiB after the focused output dumps
and archival. Both services stayed active.

## 2026-11-02 (JST): pipeline reuse across shape changes

New default-off `--reuse-pipeline-cache` retains a lazy Vulkan device and an
external ncnn PipelineCache for the bounded two-to-four-request loop. It requires
Vulkan and `RADISH_NCNN_PIPELINE_CACHE=1`. Exact inferred-parameter matching still
controls Net reuse; a shape change destroys the old Net/weights before creating
and loading the next. Stock pipelines use the external cache; custom/raw
pipelines use the retained device cache. No shader, precision, graph, stored
weight or driver changes. Final cleanup remains Net/custom contexts, external
cache, then device, included in session timing.

The public short/full/short/short run passes all four original-ORT core/waveform
references. All eight input files and both output files are byte-identical to
the matching validated 0033 captures. Every request has 715 GPU/zero CPU layers.

| Request | T / Net hit | Load s | Extraction s | Synthesis s | External/device cache entries |
| --- | --- | ---: | ---: | ---: | --- |
| 000 | 78 / no | 9.710722 | 14.733876 | 28.937692 | 173 / 4 |
| 001 | 213 / no | 3.988262 | 39.090510 | 49.434000 | 263 / 4 |
| 002 | 78 / no | 1.162545 | 14.568416 | 20.455279 | 263 / 4 |
| 003 | 78 / yes | 0 | 14.191422 | 18.498217 | 263 / 4 |

The T78 revisit reloads its weights; it is not a retained-Net hit. The same 140
executing pipeline handles and all 1425 ordered dispatch signatures recur across
the first, revisited and retained T78 requests. Counts alone are not hit or byte
measurements. Cache growth stops after these two shapes; this is not an unbounded
worker-cache policy. Session 120.478595258 s includes final cleanup 0.279266953 s;
printed outer elapsed 120.492458 s, service 120.596 s. Whole-session peak is
525.7 MiB plus 41.6 MiB swap, no faults and both live services active.

Prior short shape reload took 9.328511 s and synthesis 28.964791 s. The current
revisit takes 1.162545/20.455279 s, with different cache history, request sequence
and swap. These observations support retaining compiled pipelines, not an isolated
convolution speedup. The new resident short observation is 18.498217 s; CPU remains
faster at 15.411271 s. No new full resident sample, sustained result, live Japanese
baseline or application integration is established. The earlier tablet cancellation
probe predates this flag; its enabled cancellation path has source/ownership review
but has not been separately exercised on the tablet.

Runtime source SHA `27f81a603923ce135920bd0c858ee74c5e734ed39d6e905aac7e1568b29e3737`;
canonical binary `dfc2f7a65ffe153ebffc18597e16aa771dfb9cd69e417a723ffdcc7e6de05d66`.
Driver 0032 and ncnn patch 0033 are unchanged. The strict isolated build, CLI/
provenance checks and independent review preceded the canonical two-step rebuild.
Evidence under WORK: `resident-pipeline-cache-implementation/checkpoint-{before,after}/`,
`resident-pipeline-cache-review-k9qwltmm/review.json`,
`resident-pipeline-cache-result-o2y_6g4d/{REPORT.md,audit.json}`,
`shared-pipelines-runtime-validation-3906w_1n/{REPORT.md,summary.json}` and
`shared-pipelines-request-{000,001,002,003}-validation.json`. Terminal log:
`runtime-shared-pipelines-first.log`. The complete capture and exact command are
in [RUNTIME_INTEGRATION.md](RUNTIME_INTEGRATION.md). Staged assets and outputs
remain in the corresponding private `runtime-runtime-short-vulkan-shared-pipelines-first-…-pipeline-cache`
directory. API advertisement remains experimental 1.0, without conformance.

## 2026-11-01

Latest opt-in path adds padded channels 0033 to AffineSnake, height-one
deconvolutions, planar FP32 convolution, norm sum4 and generic/tiled batch 8,
with X1. All 715 non-Input layers use Vulkan. Five formerly general convolutions
now use the existing tiled kernel, raising the tiled count from 53 to 58.
Fixed-core first/warm extraction passes at 38.788999/38.556751 s, 405.9 MiB and
zero swap. The intended five padding/math pairs save 2.090428300 s of the
2.166974 s warm reduction from 40.723725 s. Other 1601 command signatures match.
Outputs differ from the earlier GPU result but pass every unchanged reference
gate; the prior run's 71.7 MiB swap and different binary/driver remain caveats.

The public four-request full/full/short/short loop passes all original-ORT
core/waveform gates. Fresh full synthesis is 54.928111 s; resident full is
45.500714 s. Short after shape reload takes 28.964791 s, then 18.767525 s on
reuse. Core times are 39.689168/39.181155/14.378353/14.370964 s. Every request
has 715 GPU / zero CPU layers and identical boundaries to prior CPU/GPU captures.
Whole-session peak is 525.8 MiB plus 80.1 MiB swap, not an isolated request peak;
no faults occur and both live services stay active. Resident CPU remains faster
at 36.802922 s full / 15.411271 s short (404.8 MiB, zero swap). These are single
observations with different session sequences and memory pressure, not sustained
or isolated whole-runtime speedup evidence. PAD_CHANNELS remains default-off.

Driver 0032 is unchanged by 0033; its 36 indirect and 16 direct-grid checks
pass on A506. API advertisement remains experimental 1.0. Public staging
supports two to four complete requests and per-request validation. The earlier
tablet cancellation probe preserves the completed request after its active GPU
core drains; marker-return to exit is 15.793739 s. Prompt interruption, a new
request after cancellation, sustained behavior and live application wiring remain
unproven. Detailed earlier checkpoints and the new 0033 evidence follow below.

A private vector-bias shader experiment is not admitted to the production path.
Its original repeat-MAD hypothesis failed: the hot loop still has 32 scalar
MADs, with no repeat MAD or instruction-count improvement. Vector bias loads
do reduce the whole program from native length 27 to 21 and consolidate two
shared-load/wait phases into one, motivating a separate timing experiment.
Both initial batch1 operator pairs pass all 27264 output, independent-reference
and input-preservation checks. However, the second call is slower in both
orders: accepted/vector-bias math 10.594/35.536 ms, then vector-bias/accepted
11.789/66.611 ms. First-use setup and changing GPU frequency prevent a shader
ranking from these pairs. The subsequent warmed balanced comparison rejects
the candidate: math mean 11.850724→12.704529 ms, 7.20% slower, with the candidate
slower in all four adjacent pairs. All twelve outputs pass. Driver 0032 and its
production batching allowlist remain unchanged.

Historical evidence follows.

Overall goal active. M0–M2 bring-up is demonstrated; selected M3 paths now pass,
including 16 KiB shared reductions, transfers, dependent compute and bounded
3D dispatch subdivision. Recovery now propagates DEVICE_LOST. ncnn ReLU,
32-thread means, Conv1D K3/K7/K11 and InnerProduct pass full GPU output checks.
The fixed Kokoro neural core passes all 562342 outputs. A guarded FP32
InstanceNorm+tiled graph with output reshape aliases and four sequential tiled
chunks per submission now passes first/warm extractions in 56.853/56.014 s,
including input/output transfer, and its device waveform passes all 127800
samples. The same fused graph's packed six-thread CPU warm mean is 32.079 s:
the GPU remains about 1.75 times slower, despite a 44.7% reduction from the prior
graph's 101.275 s warm extraction. GPU evidence has one warm iteration; CPU has
two. These are core measurements. The single-process runtime with 0022 completes
fresh tablet T=78 synthesis in 34.864 s including frontend startup/release,
metadata-only shapes, GPU core load/extraction and waveform tail. All 205942
core values and 46800 audio samples pass against references from its exact
captured boundary; peak 341.8 MiB, zero swap, no new faults. No host calibration
or reference preparation occurs between its inference phases. The physical64 complex-reduction hang remains unresolved;
the backend uses 32 threads. A separate compiled-register guard now rejects
programs beyond the measured register budget before upload.
Packed-FP16 conversion/ReLU, all five Conv1D/InnerProduct fixtures, all 18 means,
and up to eight shared32 workgroups per hardware dispatch now pass on device.
The first packed full-model run produces NaNs. Explicit mixed-storage conversion
and FP32 normalization regions remove the diagnosed zero denominators and make
all outputs finite, but 168167 core values and the waveform gate still fail.
Neither failed run is an accepted performance result. The revised tiled FP32
convolution fits 16 full registers and passes its complete operator fixture;
17.920 ms versus 18.809 ms for the default kernel improves to 10.868 ms after
the shared-A transpose. The full tiled model passes all 562342 outputs in
both first/warm extractions, now including dilation 3/5, with a passing waveform.
A performance win and sustained runtime behavior remain unproven. API
advertisement and conformance status are unchanged.

The separate opt-in Snake graph now passes the full fixed core with 0023 batch8
in 49.267/49.429 s first/warm, 3.436% below its prior 51.188 s batch4 warm result,
with identical output bytes, 419 MiB peak, zero swap and no faults. Fresh T213
Snake runtime with batch8 passes all core/waveform checks in 63.528 s,
458 MiB/zero swap. Earlier batch4 T78/T213 results remain 33.764/68.158 s,
383.3/445.9 MiB with zero/22.9 MiB swap. The runtime now defaults to batch8;
InstanceNorm remains the default graph and Snake is explicit opt-in.

Historical candidate 0020 uses local32 with a 64x32 tile and 12 KiB shared
memory. Builds, all 20 model compiler variants and focused dilation1/3/5 output
checks pass, but its best dilation1 math command is 21.885 ms versus the existing
tile's 10.868 ms. The full core passes with byte-identical output but takes
101.354/100.467 s first/warm, with 414.8 MiB peak memory and 52 MiB swap.
Reject 0020 for performance and keep it default-off; 66.919 s warm was the
accepted baseline for that comparison. Sequential batching is recorded below. The new swap use limits causal attribution
of the slowdown.

Incremental candidate 0021 now changes the same default-off
`RADISH_NCNN_TILED32=1` path to a 32x32 tile, eight vec4 accumulators and 8 KiB
shared memory. Source review, ordered reconstruction and private syntax/SPIR-V
checks and all builds pass. All 20 model compiler variants use 16 full/0 half
registers with no private allocation. Dilation1/3/5 operators pass, but the best
dilation1 math time is 33.229 ms, slower than both earlier tiles. The full first
extraction passes with byte-identical output but takes 138.633996 s versus the
accepted 67.138187 s first, with 425.2 MiB/zero swap and no faults. Reject 0021
for performance and keep TILED32 default-off. No warm run was performed; the
accepted local128 tile is retained; later sequential batching improves its
66.919494 s warm baseline as recorded below.

- Mesa worktree `/run/media/jethac/Shared/radish/mesa`, branch `radish/main`,
  base `ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230`. AC8 checkout untouched.
- ncnn checkout `/run/media/jethac/Shared/radish/ncnn`, revision
  `f18f3ffc94dbc76a277e94556c588ab2c2d0e1d9`.
  User-requested fork: [jethac/ncnn](https://github.com/jethac/ncnn), verified
  parent `Tencent/ncnn`. Local `origin` is `git@github.com:jethac/ncnn.git`;
  `upstream` is `git@github.com:Tencent/ncnn.git`. Local changes remain uncommitted
  and unpushed; the source revision is unchanged.
- Local transcribe.cpp revision `087328dca63b644aaca048658cfdf110ee95b8c5`.
- Public backup: [jethac/radish](https://github.com/jethac/radish), MIT license,
  initial commit `ba13c70`, local publication mirror `/home/jethac/Projects/radish`.
  Editable project files remain in `/home/jethac/Projects/lenovo-8/radish`;
  Mesa/ncnn source and private builds, fixtures and evidence remain at the shared
  work locations above. The public snapshot carries audited source patches,
  not model weights or private captures; publication does not replace those
  working trees or constitute an upstream submission.
- Existing cross sysroot and Python build environment are available in
  `streams/gpu-mesa16/work/{sysroot,venv}`. Reuse read-only; Radish outputs go on
  `/run/media/jethac/Shared/radish` because the root filesystem has only 5.5 GiB free.
- Tablet read-only check: kernel `7.3.0-g755ffc44d5d8-dirty`, renderD128 present,
  both `straylight-listen` and `straylight-voice` active, about 1 GiB available RAM.
  No Vulkan loader or vulkaninfo found at the checked system paths. The initial
  test harness initially loaded the ICD directly. A separately built loader is
  now staged privately, and loader discovery is verified below.

## Decisions

- Ruling: use ncnn FP32 buffers as the first compatibility target. Its instance
  code explicitly supports Vulkan 1.0 and prefers compute-only queues; optional
  features are queried. The local ggml Vulkan implementation requires instance
  Vulkan 1.2 (`ggml-vulkan.cpp:5194`) and storageBuffer16BitAccess (`:4642`).
  These requirements make ggml a later target. Cost: ncnn model conversion and
  graph support must be checked for a real speech model; this does not waive M5.
- Ruling: implement inline and retain this directory's plan/status as the ledger.
  The user's execution request supplies implementation authorization. Mesa code
  gets no generated comments or commit-message prose under its AGENTS.md.

## Evidence and progress

- `radish/scripts/build.sh`: arm64 ICD and test builds use the existing sysroot,
  with all outputs on the shared disk. Base tree is Mesa 27.0.0-devel.
- Baseline `computerator` builds with OpenGL enabled. With all OpenGL and Vulkan
  disabled, Mesa chooses the NIR stub and this utility fails to link against
  ir3's NIR references; enabling OpenGL resolves this configuration issue.
- Baseline XML check passes: `meson test -C .../build-baseline
  check-xml-includes --print-errorlogs` (1/1).
- Lifecycle test first failed at missing ICD, then passed on A506: 24 independent
  device/buffer/BO cycles, nonzero memory binding/mapping offsets, full byte
  readback, and unsupported extension/feature rejection. This used direct ICD
  loading; subsequent loader discovery is verified below.
- Initial safety checker incorrectly treated old messages evicted from the kernel
  ring buffer as new faults. Inspection found no new fault. Timestamp-based
  checker now passes the captured logs; four regression cases pass, covering new
  faults, eviction, timestamp reset and missing evidence.
- Compute test first fails at missing `CreateDescriptorSetLayout`. It covers
  fixed sparse binding numbers 2/7/11, one allocation with three nonzero-offset
  buffers, push constants, 32 dispatches across workgroup boundaries, full output
  validation and changing sentinel/guard regions. Bring-up modes compile only
  or submit exactly one tiny dispatch before the full run.
- M2 code uses native Freedreno DRM submission and ir3, not Gallium. Shader
  restore/program setup is adapted from Mesa computerator with its original
  license retained. At the M2 checkpoint, unsupported: UBOs, textures/images, shared memory,
  private-memory spills, secondary command buffers and indirect dispatch.

## Verified bring-up and review

- Compile-only testing caught missing `nir_lower_system_values`: ir3 rejected a
  remaining `load_deref` before any GPU submission. Adding that lowering pass
  resolved the failure. Both generated shaders use two full registers and no
  half registers. The index shader stores through slot 2; vector add reads slots
  0/1 and stores slot 2, as intended.
- One index-dispatch smoke test passed, then all 32 index/vector-add dispatches
  passed with changing inputs and full output/guard comparisons.
- Independent read-only review identified four real descriptor issues. Each was
  reproduced before correction: reverse-order compatible layouts swapped
  buffers; unused sets caused EndCommandBuffer failure; descriptor allocation
  used device rather than pool callbacks; consecutive writes/copies asserted.
- Corrected bindings are canonicalized. Shader resource-use masks permit unused
  sets/descriptors. Pool-owned allocation callbacks are retained. Sorted packed
  SSBO slots permit updates spanning compatible bindings.
- Final regression runs: 32 sparse/reversed-layout dispatches, 32 consecutive
  write dispatches and 32 consecutive copy dispatches all pass. Tests include an
  unbound unused set, uninitialized unused descriptors, pool callbacks with the
  device allocator deliberately failing during set allocation, and zero live
  callback allocations after destruction.
- Final Vulkan-loader lifecycle run passes 24 device/BO cycles through private
  `VK_DRIVER_FILES`, not direct ICD entrypoints. Loader revision
  `aa6dee8385afe4b2cc0152d086af2184743e16fd`, headers revision
  `9fbc2a782ee3f8a4db64e7d991e23da75e893f44`; reproducible build script is
  `radish/scripts/build-loader.sh`.
- Host lifecycle excludes the real Intel GPU and simulated A530. The existing
  DRM shim does not recognize GPU ID 506 and aborts if asked for it; core metadata
  confirms `print_supported_chips_and_abort`, not a Radish crash. Do not use it
  as A506 hardware evidence.
- Native Mesa checks pass 3/3: ir3_disasm, ir3_delay_test, check-xml-includes.
  Log-checker regressions pass 4/4. All device test runs preserved both live
  services; no new GPU fault/recovery messages in the timestamped logs.
- Evidence logs live in `/run/media/jethac/Shared/radish`, including
  `compute-final.log`, `span-{write,copy}-green.log`, `loader-final.log`,
  `ir3-tests.log` and `evidence/`. Failed runs are preserved separately.
- Source snapshot: `radish/patches/0001-radish-compute-bringup.patch`, SHA-256
  `28be2f82f4678dcb399b2eeba4f84b704ad0926440b20d3b823be0b39085bc8e`.
  Reverse `git apply --check` passes against the implementation worktree.
  No Mesa commits, pushes or community interactions performed.

## Next

Use the validated 0035 opt-in checkpoint: 35.730754 s fixed-core warm,
42.819037/18.163352 s resident full/short synthesis, unchanged reference gates
and exact 0034 output bytes. CPU remains faster. Qualify bounded GPU elapsed
measurement on accepted tiled work, then arithmetic/bandwidth calibration and
an evidence-led convolution change. Keep 531.4 MiB/57.2 MiB whole-session memory
and swap explicit. Measured attainable GPU efficiency and sustained inference
remain completion requirements. The earlier optimization sequence follows.

Continue from the passing FP32 InstanceNorm+tiled+dilation core and waveform:
56.013717 s GPU warm with 0022 batching versus 32.079044 s packed CPU warm mean
(the prior 0019 GPU checkpoint was 66.919494 s). The GPU has one
warm sample and CPU two; sustained end-to-end evidence remains open. Incremental
0018 now covers the 16 dilation3/5 convolutions, with unchanged operator/model
accuracy gates and no new faults. The narrowed output-only reshape alias removes copies without introducing
protective clones. Candidate 0020's separate local32/tile64x32 kernel now passes
source review/reconstruction, builds, the conservative model-wide compiler gate
and focused operators. Its full-model result is byte-identical but regresses to
100.467084 s warm with 52 MiB swap; reject it for performance and keep it
default-off. The profile attributes the increase to the 53 changed tiled commands.
Candidate 0021 halves that variant's channel tile and accumulator count while
retaining local32 and all eligibility gates. The focused compile reaches 16
full/0 half registers and full outputs pass, but its math command is slower.
All 20 model variants pass the zero-private/F+H/2<=32 gate at 16 full/0 half,
and dilation3/5 complete-output checks pass too. The full first extraction
passes byte-identically but takes 138.633996 s versus accepted 67.138187 s first.
Reject this geometry and keep TILED32 default-off; no warm repeat is warranted.
The separate 48-chain Snake candidate is now integrated as an explicit opt-in
graph in the model/runtime helpers. Its host full-core reference check and
bounded tablet CPU/GPU equivalence probes pass. The full GPU core is
byte-identical at 51.936026/51.188057 s first/warm, 8.615% lower warm latency
than 0022, with zero swap and no faults. Fresh T78/T213 runtime passes all
core/audio values in 33.764051/68.158274 s, with exact earlier GPU output bytes;
the full-length run uses 22.9 MiB swap. InstanceNorm remains the default graph;
its recorded 0022 batch4 baseline is 56.013717 s warm. Measure equivalent Snake
CPU work and sustained/cancellation behavior next; preserve these checkpoints.
The later 0023 batch8 fixed-core repeat now passes in 49.266772/49.429406 s
first/warm, 419 MiB/zero swap/no faults. Complete warm attribution preserves all
resource/grid/chunk signatures and changes only 53 tiled submit counts. Fresh
batch8 T213 runtime passes in 63.528396 s, 458 MiB/zero swap. The runtime wrapper
now defaults to 8; driver/operator/model defaults stay 1.
The new planar-input candidate is default-off. All 20 math shader variants pass
actual compiler/resource inspection; integrated compile and full padded operator
outputs pass. The full fixed core passes in 48.738814/48.863090 s first/warm,
420.1 MiB/zero swap, with identical output bytes. The single warm comparison is
1.1457% faster; slower padding absorbs much of the math gain. Local8x4 was not
fresh-runtime validated; keep its result and the accepted49.429406 s baseline distinct.
The later0026 local8x8 prepass passes the same full core at46.932926 s warm,
418.4 MiB/zero swap and exact output identity. Attribution confirms padding
submits3696→1848 and0.839163 s less padding wall time; other timing variation
also contributes. Fresh T78/T213 runtime passes in31.890578/63.320730 s with
unchanged core/waveform bytes and zero swap. Full startup is essentially unchanged
from0023's63.528396 s observation; PLANAR remains opt-in/default0.
Later0029 generic batch8 is recommended as an opt-in for the measured fixed path:
paired warm47.413913→45.339510 s, exact outputs, fresh T21361.429138 s with16.7 MiB
swap. T78 total32.622528 s is slower despite its faster17.289251 s core. Keep
norm0/X1 and existing defaults; preserve single-pair/swap/startup limitations.

The new metadata-only shape pass matches every original and fused shape at
T78/T213. The integrated frontend/core/tail executable builds for host and
AArch64; its host T78 full core and waveform pass against identical boundary
inputs. Short tablet synthesis and its same-boundary reference validation pass;
full T213 synthesis and the equivalent CPU startup measurement also complete.
Both complete output gates pass. Pursue the measured remaining costs and
sustained behavior. Shared32 remains rejected for latency; packed FP16 remains rejected
for accuracy. Keep FP32 as the accepted path. Give performance updates about
every five minutes during active work. Keep pushing validated increments and
measurements to the public GitHub repository, as requested.

The harmonic-source audit and complete runtime measurements are in
[RUNTIME_INTEGRATION.md](RUNTIME_INTEGRATION.md).

The identical reduction shader still hangs at physical64. M3 also needs the
outstanding API/synchronization checks and device execution of the prepared CTS
subset. The CTS binary and reviewed case lists are built, but no CTS case has
run. See `ABI.md` and `VULKAN_AUDIT.md` for unresolved limits and API gaps.

### First complete command profile

`core-profile-baseline.log` repeats full-core correctness with identical maximum
error, no faults and both services active. Cold load 10.454 s; extraction with
transfers 193.389627 s; cgroup memory 416.2 MiB, zero swap. This run uses the same
graph and execution policy with opt-in command wall-time instrumentation.

`baseline-profile-summary.json` records 1601 commands, 349033 hardware submissions
and 178.783512 s inside those commands. By compiled local size: 16×4×1 accounts
for 74.664 s; 4×4×4 for 65.259 s; 32-thread barrier shaders for 24.053 s;
other 32-thread shaders for 13.434 s. Timings include GPU work, host submission
and waiting. The remaining extraction time is not a measured CPU-fallback time.
Some convolution chunks average 170 ms, so broad batching would be unsafe.

Checkpoint 0009 adds `RADISH_BATCH_CHUNKS=1..4`, default 1, only for shaders
without loops/barriers and within the existing instruction-preload limit. All
per-chunk state, waits and cache packets remain. Long/barrier kernels stay at
one chunk per submission. Profile output separates GPU chunks from actual
submission attempts. Grid, shared-grid, dependent-dispatch and full-model checks
pass; the results below retain the separate shape-hint and batching steps.

### Full-graph shape hints

`hint-kokoro-shapes.py` captures all 1739 scalar blob dimensions using one CPU
Extractor and writes upstream `-23330` metadata for all 1368 layers. It retains
only shape records, guards the exact graph/fixtures, and validates CPU output
before and after annotation. Both runs are bit-identical to the prior CPU
baseline. The new private fixture is `kokoro-core-shapes`, parameter SHA-256
`857d5c2e8f3ef728f33baea250fb58090a099818e8cfec2821613fc6b851eb36`.

`core-shapes-profile.log`: with batching disabled and CPU OpenMP still off,
cold load 10.317 s; extraction/transfers 152.542228 s, down from 193.389627 s.
All 562342 values pass with unchanged max/relative errors. Hardware submissions
fall from 349033 to 179887; BinaryOp groups fall from 14230992 to 3563743,
using existing rank-2 local-size selection. Peak cgroup memory 404.3 MiB, no
swap/faults, both services active. This is one fixed-fixture diagnostic comparison,
not a warm or end-to-end performance win. Exact dimensions are shader
specializations; different runtime shapes require matching hints/pipelines.

Batching passes the existing plain/shared 3D-grid checks and 48 dependent
dispatches with no faults. Plain grids record 30 chunks in 12 submissions;
shared shaders retain one chunk per submission. The full hinted-model batch4
run also passes. Checkpoint 0009 records that driver; current cumulative Mesa
checkpoint 0016 includes the later shared32 group budget and register guard.

The separate SIMPLEOMP build succeeds with `RADISH_NCNN_SIMPLEOMP=ON` and
`RADISH_NCNN_BUILD_DIR=.../build-ncnn-simpleomp`; it includes its pthread-based
OpenMP runtime and needs no libomp/libgomp. Default remains OFF. Patch 0010
propagates known Conv1D shapes into its internal Padding helper; its combined
run with four-thread CPU fallbacks passes. That comparison does not isolate
the contribution from each change.

### Current model performance

GPU timings include transfers, tracing and command profiling. Earlier rows are
single validated extractions; the groups8 row records first/warm extractions
from one loaded Net. Memory and swap are cgroup peaks; cold load is separate.

| Log / change | Extraction (s) | Cold load (s) | Submissions | Memory / swap (MiB) |
| --- | ---: | ---: | ---: | ---: |
| `core-profile-baseline.log` | 193.389627 | 10.454 | 349033 | 416.2 / 0 |
| `core-shapes-profile.log` | 152.542228 | 10.317 | 179887 | 404.3 / 0 |
| `core-shapes-batch4.log` | 136.402930 | 11.609 | 74131 | 430 / 92.3 |
| `core-padding-simpleomp.log` | 124.523801 | 10.392 | 68728 | 401.2 / 5.4 |
| `core-induction-simpleomp.log` | 113.523641 | 10.185 | 68728 | 394 / 0 |
| `core-groups8-repeat2.log` | 103.191648 first / 101.275010 warm | 10.117 | 37358 per extraction | 416.1 / 0 |

All 562342 GPU outputs remain bit-identical, SHA256
`d7232da21a2a6dd1c04e202a405ef83a2ba07d0ae2e021221a6e0933f2816433`:
max absolute error 0.000118255615, relative RMS 7.813705e-7, zero mismatches.
No new GPU faults; both services remain active. Patch 0011 replaces FP32
nonshared GEMM division/remainder with tap/channel induction while preserving
accumulation order. K11 compile inspection reduces instructions 377→254 and
NOPs 151→75, retaining 16 full registers and eight waves; K3/K7/K11 operator
checks and the full model pass (`operators-induction-execute.log`).

The separate FP32 InstanceNorm variant passes its first extraction in
`core-instancenorm-first.log`: 88.314593 s, max absolute error 0.000129699707,
relative RMS 7.77219834e-7 and zero mismatches. Service wall time is 99.279 s,
peak memory 424 MiB and swap 72.2 MiB; no new faults and both services active.
Its 1943 recorded dispatch commands use 34075 submissions and total
84.3403444 s of command wall time. These include submission/wait overhead;
transfer and CPU work are not independently timed by subtracting the totals.
The result changes rounding and is not part of the bit-identical table above.
It is one first extraction, not a controlled warm speedup measurement.

`core-instancenorm-tiled-transpose-first.log` enables 0017 on the same fused
FP32 graph and passes all 562342 outputs: first extraction 75.743194 s,
cold load 9.673 s, max absolute error 0.000106811523, relative RMS
7.80397212e-7 and zero mismatches. Peak memory is400.6 MiB with zero swap,
both services active and no new faults. Actual layer trace records 1090 GPU
calls and three CPU deconvolutions (one DepthWise1D, two Deconvolution1D),
independently counted from execution records rather than graph inventory.
The 1943 dispatch commands use 50047 submissions and total 71.650563844 s;
37 tiled math commands account for 17400 submissions and 26.624574599 s.
Their instruction lengths are 36 (25 commands) and 37 (12), all local16x8 with
four loops and barriers. Timing includes host submission/wait costs. The 75.743 s
and 88.315 s runs are first extractions with different swap observations;
the later repeated/core-and-waveform validation is recorded below.

`core-instancenorm-tiled-transpose-repeat2.log` passes two complete GPU
extractions after 9.379 s cold load: 76.534312 s first and 74.317956 s warm.
Both compare all 562342 values with max absolute error 0.000106811523,
relative RMS 7.80397212e-7 and zero mismatches. Frozen first and warm outputs
share SHA256 `324de97d2c224635b1f54dcb8bf89c73efab2e09a8538691dfe68d4b9c2c0b37`.
Peak memory is 417.8 MiB, zero swap, no new faults and both services active.

The same fused graph's packed FP32 CPU comparison uses six SIMPLEOMP threads
(`core-instancenorm-cpu-repeat3.log`): cold load 1.040 s, first 32.326136 s,
then 32.025450/32.132638 s, warm mean 32.079044 s. All 562342 outputs pass on
every iteration, max absolute error 0.000507354736, relative RMS 4.01799868e-6,
zero mismatches; peak 444.4 MiB, zero swap and no new faults. The GPU warm
extraction remains 2.317 times slower. Relative to the prior graph's 101.275010 s
warm GPU run, 74.317956 s is 26.6% lower, but this is a historical comparison,
not a controlled repeated A/B experiment. GPU warm count is one; CPU is two.
The earlier nonfused CPU warm baseline remains 32.052271 s.

The tiled device waveform also passes all 127800 samples
(`core-instancenorm-tiled-transpose-waveform-device.log`): 0.104488 s,
max absolute error 1.12576771e-5, SNR 97.967904 dB, zero mismatches,
peak 4.7 MiB and zero swap, with no new faults. Core/style and waveform
tolerances are unchanged; the source remains the saved fixture.

The padding/SIMPLEOMP device process exited successfully, but editing its active
shell wrapper caused host exit 126 after completion. Saved output was recovered
under the lock and its hash verified. Wrappers now parse a complete function
before executing; do not edit a running runner.

`core-cpu-packed-omp6.log` runs the same fixed fixture with packed FP32 and six
SIMPLEOMP threads: cold load 1.016 s, first extraction 32.624074 s, then
31.976996/32.127545 s, warm mean 32.052271 s. Every output passes (max absolute
error 8.88109207e-5, relative RMS 5.24520211e-7); peak 440.8 MiB, zero swap.
The GPU is still much slower. Its latest warm measurement excludes the first
extraction but contains only one subsequent iteration; the CPU mean uses two.
These are same-fixture core measurements, not live end-to-end timing.

`device-model.sh` supports `--cpu-packed`, `--threads 1..6` (default 4), and
`--repeat 1..10` (default 1; first extraction excluded from warm means).
`RADISH_MODEL_BINARY` selects the executable; `RADISH_PROFILE` and
`RADISH_NCNN_TRACE` accept 0/1; `RADISH_BATCH_CHUNKS` accepts 1..4, default 1,
and `RADISH_SHARED_GROUPS` accepts 1..8, default 1.
`build-ncnn.sh` accepts `RADISH_NCNN_SIMPLEOMP=ON|OFF` and a separate
`RADISH_NCNN_BUILD_DIR`. The original validated model executable used patches
0005/0006/0008/0010/0011; optional candidates are applied but remain gated.
The earlier overlapping-patch verification failure is resolved; the regular
and SIMPLEOMP builds pass with corrected 0013 and revised 0014/0015.
Shared32 operators and full-model correctness pass, but the model slows to
198.453 s; keep this path opt-in.

### Shared32 groups and convolution candidate

Historical cumulative Mesa patch 0012 adds `RADISH_SHARED_GROUPS=1..8` (default 1),
allowing that many workgroups per hardware chunk only for barrier shaders with
at most 32 physical invocations. Larger barrier workgroups retain one group;
non-barrier chunks and short-shader batching rules are unchanged. Every chunk
retains state/cache packets, bounded fence completion and fault checks.

`shared32-groups8.log` passes all 16 dispatches and guards, including grid129 in
17 chunks. `ncnn-reduce-groups8.log` passes all 18 actual ncnn means. The complete
model then passes twice in `core-groups8-repeat2.log`, with bit-identical output,
no faults and both services active. Each extraction records 1601 commands and
37358 submissions, down from 68728. Warm command wall time totals 97.351292 s:
60 convolution math commands take 64.538893 s and 108 reductions take 13.784405 s
in 4490 submissions. These measurements include GPU work and host overhead.

Patch 0013 is a separate, source-reviewed ncnn candidate: exact Radish A506 plus
`RADISH_NCNN_SHARED32=1`, FP32 only, enables local8x4 and 8 KiB shared arrays for
Conv1D GEMM. Padding and other operators keep their existing options. Tail
loaders zero inactive inputs/weights, skip inactive bias and preserve barrier
participation. The first shader compile inspection rejected `gl_WorkGroupSize`
without a fixed local-size declaration. Specialization ID15 now selects this
mode explicitly, leaving nonshared specialization entries unchanged. Both helper
builds pass. `operators-shared32-specialization-compile.log` confirms local8x4;
K7 uses 16 full/1 half registers and K11 uses 21 full/12 half registers.
`operators-shared32-execute.log` and `operators-shared32-groups8-execute.log`
pass K3/K7/K11 at group budgets 1 and 8 with no faults. K11 checks all 6496
outputs with max absolute error 3.81309655e-06; its grid7x2 uses two submissions
at budget8.

`core-shared32-first.log` then passes all 562342 model outputs, bit-identical
SHA-256 `d7232da21a2a6dd1c04e202a405ef83a2ba07d0ae2e021221a6e0933f2816433`,
but extraction takes 198.453405 s (cold load 21.726 s), versus 103.192 s first /
101.275 s warm on the existing nonshared path. Peak 397.6 MiB, zero swap,
both services active and no GPU faults. The 60 convolution commands rise from
64.539 s / 2198 submissions to 162.176387 s / 26840 submissions; total submissions
rise to 62000. Command wall time combines GPU execution and host overhead, so
this does not isolate a shared-memory hardware cost. Summary and distinct output
are preserved as `core-shared32-first-summary.json` and
`evidence/model-core-shapes-vulkan-shared32-first-output.f32`.
The candidate is rejected for performance and remains disabled by default.

Source/ISA review identifies two structural disadvantages: 12.2 times as many
convolution submissions, and instruction lengths 111..143 versus 13..28 on the
nonshared path. Radish disables instruction preloading above 32. The inspected
K11 shader grows from 254 instructions/416 dwords/one loop to 2299 instructions/
3616 dwords/four loops, with extra local-memory traffic, barriers and remaining
division/remainder expansions. Unrolling expands static code, not the arithmetic
count by the same factor. These observations do not apportion the measured
slowdown between execution and host overhead; induction alone would leave the
submission and instruction-footprint disadvantages.

Patch 0014, `RADISH_NCNN_TILED=1`, is a separate source-reviewed FP32 candidate:
local16x8, a 64-channel by 64-position tile, K blocks of 32 and 16 KiB shared
memory. Its gate requires exact Radish A506, pack4, stride/dilation1, output
channels divisible by 64, K divisible by 32 and at most 3584, and no fused
activation or optional reduced-precision features. Upload/layout, tails,
barriers, cache keys and weight lifetime passed independent source review;
reverse patch application checks pass. Both ncnn builds pass. The first compiled
version used 21 full registers at local128 and was never executed. Revised
0014, SHA256 `d1081b712ae8a4518b494ced59c7c1ba5fa1da501e3758e33030ddfb22d14695`,
uses scalar shared-A loads with sequential K accumulation: 16 full/0 half
registers, 954 instructions, four loops and 16 KiB shared memory. All 25984
operator outputs pass with maximum absolute error 3.422e-5 and no new faults
(`operators-tiled-scalar-execute.log`). Compute command wall time is
17.919664 ms in eight submissions versus 18.809093 ms in one submission for
the default kernel (`operators-tiled-shape-default-execute.log`). This single
case establishes no accepted model performance improvement.

Current cumulative driver patch 0016 checks the measured A506 budget of 2048
full vec4 registers before shader upload: resident threads times
`ir3_shader_halfregs(v)` must not exceed 4096 half-register units. Barrier
workgroups round their entire local size up to waves; non-barrier programs need
one resident wave. This combines full and half footprints. The old local128 /
21-full program now returns `VK_ERROR_FEATURE_NOT_PRESENT` (-8) before upload
or execution (`operators-tiled-register-reject.log`); the revised 16-full
program passes. This guard does not establish the root cause of every earlier
physical64 failure or prove general occupancy safety.

Incremental 0017 transposes only shared A to make each thread's eight consumed
scalars contiguous, preserving loader coverage, K order, arithmetic and both
barriers. Both ncnn builds and independent source review pass.
`operators-tiled-transpose-compile.log` shows 16 full/0 half registers,
950 instructions, 1152 dwords and four loops. The inner reduction uses three
vec4 local loads instead of nine shared loads. All 25984 operator outputs pass
with max absolute error 3.42226122e-5 and no faults
(`operators-tiled-transpose-execute.log`). Compute command wall time falls to
10.868408 ms in eight submissions, versus 17.919664 ms for scalar 0014 and
18.809093 ms for the default kernel. The later full fused graph also passes
in 75.743194 s, with 37 actual tiled math dispatches as recorded above.
The repeated GPU run then passes 76.534312/74.317956 s first/warm, and its
device waveform passes at 97.967904 dB. The same fused graph's packed CPU
warm mean is 32.079044 s; M6 remains unmet.

### Packed FP16 storage and operators

`packed-compile.log` compiles four existing ncnn conversion/scatter/ReLU
pipelines at local32. The scalar half-store CAS variant uses 8 full/5 half
registers, four loops and 352 dwords; no spills were observed. `packed-execute.log`
passes all outputs/guards across two changing-input repetitions, pack4 width33,
four-row scalar widths3/5/33/129, and partial-word stores with width3/stride4/5.
The partial-word cases verify untouched neighboring half-values. Seeded
subnormal unpack preserves values; normal boundaries and rounding gates pass.
Both services remain active and no new GPU faults are reported.

This uses packed 16-bit values inside ordinary uint SSBOs with FP32 arithmetic;
it does not advertise native FP16 arithmetic/storage features. In ncnn source,
`GpuInfo::support_fp16_packed()` is unconditional; `ncnn_glsl_ext.comp` selects
`uint`/`uvec2` storage, `float`/`vec4` arithmetic and `packHalf2x16`/`unpackHalf2x16`.
Scalar stores preserve the neighboring half through a 32-bit compare-and-swap
loop. `gpu.cpp` requires neither the 16-bit storage nor float16 arithmetic
extension for this option alone and retains Vulkan 1.0 / SPIR-V 1.0 when
subgroups and cooperative matrices are disabled.

Both helper builds pass. `operators-fp16-packed-compile.log` then compiles the
actual K3/K7/K11 and InnerProduct pipelines; `operators-fp16-packed-execute.log`
passes every output against the independent original-FP32 reference, with both
services active and no new GPU fault/recovery messages:

| Operator fixture | Outputs checked | Maximum absolute error |
| --- | --- | --- |
| Conv1D 3x33 -> 5x31, K3 | 155 | 0.000937474775 |
| Conv1D 4x33 -> 8x27, K7 | 216 | 0.00156244391 |
| Conv1D 16x213 -> 32x203, K11 | 6496 | 0.00390616877 |
| InnerProduct 13 -> 7 | 7 | 0.000312502525 |
| InnerProduct 128 -> 1028 | 1028 | 0.010312252 |

The K11 GEMM uses local16x4, 16 full/15 half registers, one loop and 768 dwords.
The large InnerProduct uses local8x8 sum8 and local64 reduce-sum8; both use
8 full/7 half registers. These shaders contain no shared-memory barriers.
The five cases retain the FP32 allowance `2e-5 + 2e-5*abs(reference)` and add a
predeclared storage bound with `e=2^-10`, `a=2^-14` and
`S=sum(abs(original input*weight))`:
`[(1+e)*((1+e)^r-1)+e]*S + e*abs(reference) + [(1+e)*m+1]*a`.
Here `r=1,m=0` for direct operators and `r=2,m=4` for the large InnerProduct's
four half-stored partial sums. Inputs/biases are exactly half-representable;
the extra `e*S` conservatively covers FP32 accumulation. The absolute term
permits subnormal conversion flushing; seeded unpack preservation alone does
not prove pack conversion preserves subnormals.

Helpers and `device-{ncnn,operators,model}.sh` accept `--fp16-packed`, with
distinct evidence tags. Model mode requires Vulkan and rejects CPU/capture
combinations; FP32 remains the default. Operator `--compile-only` and ncnn
`--reduce-compile` inspect pipelines without operator dispatch. The model's
existing `--load-only` includes weight upload. `device-operators.sh` also
validates/forwards `RADISH_NCNN_SHARED32=0|1` and tags value 1 separately.
Packed candidates use `ncnn-vulkan-fp16-packed-candidate.f32`; validated output
uses `ncnn-vulkan-fp16-packed-output.f32`. The model wrapper clears only the
packed candidate before launch, retrieves it into the run's evidence even on
comparison failure, and pulls successful output as
`ncnn-device-vulkan-fp16-packed-output.f32`. FP32 proof files are unchanged.

`ncnn-reduce-fp16-packed-compile.log` compiles the mean pipeline at local32,
16 full/13 half registers, two loops and 1856 dwords.
`ncnn-reduce-fp16-packed-execute.log` passes all 18 mean cases (84 scalar
outputs) with `RADISH_SHARED_GROUPS=8`, both services active and no new faults;
the largest absolute error is 0.000184955019.
The declared mean bound is `7.103515625e-5 + 0.0009785625*abs(reference)`, allowing
one output-storage round after exact-half inputs and FP32 accumulation. Source
audit finds all 108 core Reduction nodes are means at local32 with 1 KiB FP32
shared scratch. All 58 style projections use barrier-free pack4 sum8 paths;
other active floating shader families have no barriers. Conv1D GEMM's shared
branch stays off in packed mode; the optional shared32 override explicitly
requires FP32. Actual packing in the mean run uses local32; other selected
operator kernels use local64, so the standalone probe's local size is not a
general backend limit. This does not resolve the physical64 barrier
fault. The graph also has 54 additions of `1e-5`: their constants remain FP32,
but stored results can be half subnormals before normalization, making full
model validation necessary.

The first full packed run **fails** in `core-fp16-packed-first.log`: all 562342
outputs are NaN and exceed the unchanged core gate `atol=rtol=0.002`. It exits 1
after 107.514615 s extraction (process CPU 20.836027 s), with service peak
246 MiB, zero swap, both services active and no new GPU faults. These are failed
execution measurements, not a valid inference benchmark or speed win. The
printed `max_abs=0` is an artifact of comparisons against NaN, not zero error.
The candidate is preserved at
`evidence/model-core-shapes-vulkan-fp16-packed-packed-first-candidate.f32`;
no validated packed output is produced and FP32 proof files remain unchanged.

Targeted device dumps now narrow the failure. Blobs `333`, `774` and `1281`
are finite; blob `829` contains 3271808 NaNs. In a separate normalization
branch, `437` is the variance-plus-1e-5 output of `add_47`: its 128 values are
finite, but channels **13, 52, 57, 67, 84, 86 are zero**. The corresponding
`sqrt_48` denominator feeds `div_49` blob `439`, whose nonfinite values occur
in exactly those six channels (153366 values). Logs `packed-blob 437.log` and
`packed-blob 439.log` and their raw evidence preserve this localization; both
services remained active and no new GPU faults were reported. This identifies
a failed normalization path without claiming it is the only packed-model issue.

The first normalization's `333` result is finite, consistent with the earlier
FP32 variance calculation: all 514 channel variances are normal half-range
values. The mean kernel accumulates in FP32 and scales before its half store;
an overflowing half-stored unscaled mean sum is not its mechanism.

Source audit also found that ncnn's existing layout conversion can change
packing without changing storage width when a per-layer mask disables packed
FP16. Patch 0015 adds exact-Radish, opt-in `RADISH_NCNN_MIXED_STORAGE=1`
conversion between 16-bit packed and 32-bit values for the consuming layer's
masked options, including CPU-to-GPU uploads. It enables the proposed `31=2`
FP32 normalization sections to receive and return correctly typed buffers.
Both ncnn builds pass. With `keep-kokoro-norms-fp32.py`'s guarded 54-chain /
486-layer `31=2` variant, blob 437 has 128 positive finite values, minimum
4.52838685e-5, and blob 439 has all 3271808 values finite. The previous 153366
nonfinite values disappear with no new faults.

The full rerun (`core-norm32-mixed-first.log`) nevertheless fails the unchanged
core gate: all 562342 values are finite, but 168167 mismatch, max absolute error
0.368797302 and relative RMS 0.00359251526. Failed extraction takes 100.270060 s,
peak memory 307.1 MiB and swap 42 MiB. The host waveform diagnostic also fails
(`packed-norm32-waveform-diagnostic.log`): SNR 40.421776 dB, max absolute error
0.01251896 and 43435 mismatches. The conversion fix resolves the diagnosed
nonfinite path, not packed-model accuracy. Gates remain unchanged; native FP16
features remain disabled and these failed runs are not accepted benchmarks.

### Guarded FP32 InstanceNorm graph

`scripts/fuse-kokoro-norms.py` creates a new pinned private graph without
changing the source graph or weights. Each of 54 exact normalization chains
becomes rank2-to-rank3 Reshape, affine-disabled InstanceNorm with epsilon1e-5,
and rank3-to-rank2 Reshape. It retains the original Split input aliases and
downstream style affine operations; liveness removes only dead normalization
arithmetic and Split outputs. All 108 generic means disappear. The result has
1098 layers / 1307 blobs, with every new layer masked to FP32 storage (`31=2`).
Packed execution would still require 0015 and the mixed-storage opt-in; the
accepted run uses full FP32.

Host validation captures all 1307 shapes and passes the unchanged full core
gate (max error 6.48498535e-5). Host waveform SNR is 106.344961 dB with zero
mismatches. GPU blob 439 passes all 3271808 values, with maximum difference
0.0003991 from the original CPU intermediate. The complete GPU core passes
in 88.314593 s as recorded above. Its device waveform then passes all 127800
samples (`core-instancenorm-waveform-device.log`): max absolute error
1.19900454e-5, SNR 98.017571 dB, zero mismatches, 0.103437 s, peak 4.4 MiB,
zero swap and no new faults. This uses the saved-source fixture; the arithmetic
changes rounding and is not claimed bit-identical to the previous graph.
The same graph with 0017 tiled convolution subsequently passes the complete
core, including a 74.317956 s warm extraction, and all 127800 device waveform
samples at 97.967904 dB. Its same-graph packed CPU warm mean is 32.079044 s.

### Fresh runtime inputs and another sequence length

`scripts/export-kokoro-runtime.py` now exports dynamic original ONNX frontend,
harmonic-source, core and waveform-tail graphs, then captures fresh tensors
from supplied token IDs/style/speed. `scripts/prepare-kokoro-runtime-ncnn.py`
combines a capture with the pinned unhinted graph/weights, captures all 1739
logical shapes and annotates 1368 layers. It validates the original ORT core
gate before and after annotation and requires byte-identical CPU outputs.
Capture inputs/references and template weights remain unchanged.

Both host captures pass: full T=213/F=25561 and a 12-token prefix producing
T=78/F=9361. Their ncnn CPU maximum errors are 4.95910645e-5 and 3.0040741e-5,
respectively. References use the same freshly computed frontend/source boundary;
the fresh `reference-waveform.f32` is separate from the old b-en waveform.
The model helper infers dimensions from raw input sizes and rejects stale
boundary shape hints; the waveform helper has explicit `--runtime-frames` mode
while its original fixed-fixture invocation remains unchanged.

`runtime-short-ncnn-device.log` validates the T=78 FP32 GPU core against that
fresh ORT reference: all **205942 outputs** pass, max absolute error
**9.29832458e-5**, relative RMS **8.99170127e-7**, zero mismatches. Extraction
including input/output transfers takes **38.332986 s**, peak **321.1 MiB**, zero
swap. Both services remain active and no new GPU fault/recovery is recorded.
The matching T=78 device waveform also passes all 46800 samples
(`runtime-short-ncnn-waveform-device.log`): max error 1.1001e-5, SNR 96.176020 dB,
0.043091 s, peak 2.9 MiB, zero swap and no new faults. These core inputs were
generated on the host.

A separate actual tablet frontend/source run now passes for T=78
(`runtime-short-frontend-device-first.log`): boundary total 4.66509 s, frontend
load/run 3.554571/0.468618 s, source load/run 0.011151/0.248083 s, peak 181.3 MiB
and zero swap. The phases are validated separately; no integrated end-to-end
timing is claimed. Further procedure,
pins and remaining work are in [RUNTIME_INTEGRATION.md](RUNTIME_INTEGRATION.md).

The actual tablet boundary is now also preserved and passed through the GPU
core, with fresh host references computed from those exact inputs rather than
rerunning the frontend (`runtime-device-short-ncnn-device.log`). All 205942
outputs pass:38.844565 s, max absolute error 0.000102519989, relative RMS
9.15445936e-7, zero mismatches, 318.7 MiB peak and zero swap. The matching device
tail (`runtime-device-short-ncnn-waveform-device.log`) passes all 46800 samples
in 0.040910 s: max error 8.36180584e-6, SNR 97.957328 dB, zero mismatches,
2.4 MiB peak and zero swap. Both stages report no new faults and active
services. These are linked runtime stages with preserved device inputs;
the separately timed stages and intervening preparation are not a live
end-to-end latency measurement.

### Portable waveform tail on device

`tests/kokoro_waveform.cpp`, `export-kokoro-tail.py`, `build-waveform.sh`
and `device-waveform.sh` reproduce the saved ONNX tail without an ONNX runtime.
`waveform-device.log` passes all 127800 original waveform samples: wall 0.105977 s,
CPU 0.105470 s, max error 1.4829321e-5, SNR 97.908145 dB, peak 5.5 MiB, zero swap.
The original per-sample/SNR gates are unchanged. This is a separate tail timing
using saved GPU-core output and harmonic source, not an integrated core+tail
measurement or live front end.

The existing `reference-waveform.f32` is the extracted ORT-tail reference; it
differs from the original NPZ waveform by up to 9.83477e-7. The exporter accepts
paired `--fixture b-en.npz --reference-output NEW_PATH`, verifies the exact
fixture/reference hashes, and writes the original samples to a new private file.
Current `kokoro-reference-original.f32` SHA256:
`cf90b53cf7427b2b97ac47da41fc2d174d64eb1c21e0ca7240d543fbf58bcdff`.
The 1936-byte `kokoro-tail.bin` SHA256:
`e04b5c7b07c51b98b8883ba4feb753dbcb5bad9260fc93d009aa7fd577e80749`.


## Left on device

`/var/tmp/radish/{libvulkan_radish.so,libvulkan.so.1,radish_icd.json,lifecycle,compute,transfer,grid,control,radish-ncnn,radish-operators,radish-model,radish-packed,index.spv,vector-add.spv,grid.spv,control.spv,kokoro-waveform,kokoro-tail.bin,kokoro-reference-original.f32}` plus private `kokoro-style/`, `kokoro-core/`, `kokoro-core-layouts/`, `kokoro-core-shapes/` and `kokoro-runtime-short-ncnn/` model/fixture/output directories. The core-shapes directory includes `ncnn-waveform.f32`. No installed system
files, service settings, boot files or kernel changes. Both live services remained
active during the lifecycle tests. SPIR-V files are test fixtures, not model weights.
Cleanup: remove only `/var/tmp/radish` under the device lock when no tests run.

## M3 work in progress

- Pinned ncnn/glslang cross-build succeeds (`build-ncnn.sh`). Tests call a
  Vulkan-only ReLU layer and require recorded dispatches, so CPU fallback cannot
  satisfy the operator check. The init-only mode now passes on hardware through
  the private loader (`ncnn-init-2.log`). No optional features were enabled.
- Added honest unsupported-image/format queries. The lifecycle query regression
  first failed inside the absent hook and now passes (`queries-green.log`).
- Native fill/copy/update commands are implemented with A5XX CP packets and
  ordered typed command recording. Transfer chunks are 4 KiB for bounded work;
  this is a correctness path, not a performance result.
- The first multi-command transfer test exposed heap corruption in command append:
  Mesa's dynarray resize returns the old end pointer, not the allocation base.
  The inherited append arithmetic added the count twice. Replaced it with
  `util_dynarray_grow_bytes`; transfer regression now passes. Existing M2 tests
  used one dispatch per command buffer and did not cover this case.
- New code is not yet included in the M2 patch snapshot. Keep that snapshot as
  the verified M2 checkpoint; export M3 separately once its regressions pass.
- Native transfers pass full-buffer/guard comparison over three repetitions
  (`transfer-append-fixed.log`). Mixed transfer/compute and dependent compute
  pass 48 dispatches (`chain-first.log`).
- Added `VK_KHR_get_physical_device_properties2` queries with subgroup size 32
  but no subgroup operations/stages advertised. Loader lifecycle/query checks
  pass (`props-extension-green.log`). A zero subgroup size had trapped ncnn's
  workgroup selection in a CPU loop; the query capability fixes that path.
- Default Freedreno WC BOs now advertise host coherence, with a CPU ordering
  barrier at queue submission. A dedicated test skips flush/invalidate and
  verifies all outputs over 32 dispatches (`coherent-green.log`). This supports
  the tested memory path; it does not establish every memory-model case.
- ncnn Vulkan-only ReLU passes 128, 508 and 16, 396 scalar elements, checking every
  result against independent arithmetic (`ncnn-relu-coherent.log`). The test
  requires recorded GPU dispatches and disables optional arithmetic/subgroup
  paths. Packed outputs are counted in scalar elements. Both live services
  remained active; no new GPU fault/recovery messages. This is operator coverage,
  not a real model, performance result, or completion of M3/M5.
- Shared-memory reduction test first fails in ir3 on unlowered shared-variable
  dereferences (`shared-red.log`), before GPU submission; no GPU faults. The
  driver had checked `shared_size` before allocating explicit shared addresses,
  so zero there did not mean no shared variables. Adding the standard shared
  layout and explicit-IO lowering is in progress. Test specializes workgroups
  to 32/64/128 and accesses above 16 KiB in a 32 KiB array, with tree barriers,
  partial groups, independent CPU sums, changed inputs and output guards.
- Shared lowering now compiles (`shared-compile.log`): specialization 128,
  9 full/0 half registers, 226 instructions, no spill. First hardware test
  **FAILS** (`shared-128.log`): n=1 leaves sentinel untouched and emits
  `gpu fault ... E40801C3` plus hangcheck recovery. GPU testing stopped for
  inspection; both services remain active. Captured read-only logs and kernel
  devcoredump under `evidence/shared-fault*` on shared scratch. The observed
  register footprint is below the earlier 2048 register×thread budget; the
  shared allocation/address range, barrier path and hardware state need further
  isolation. Do not claim shared-memory support from successful compilation.
- Controlled isolation after inspection: a 512-byte shared array passes 16
  dispatches at both local size 32 and 128 (`shared-small-{32, 128}.log`), with
  full output/guard checks and no new faults. This establishes the tested
  multi-wave barrier path; it narrows the original failure toward larger shared
  addresses/allocation or their associated state, rather than wave count alone.
- Source evidence found in pinned Mesa
  `src/freedreno/tests/reference/compute-a540.log:463–469, 612–618`: the captured
  vendor compute stream writes register `0xe5f9=0x1f` and
  `HLSQ_CS_CNTL_1=0x1f` for a shared-memory shader. Radish/Gallium's initial
  emitter instead left the former zero and wrote the latter as 1. Testing the
  observed pair for shared shaders is in progress. Field meanings and a general
  allocation formula remain hypotheses; do not borrow a6xx field definitions.
- The captured state-pair experiment does **not** fix the 32 KiB case
  (`shared-state-128.log`): another E40801C3/recovery at the first n=1 dispatch.
  GPU testing stopped again, logs captured (`shared-state-fault-inspection.txt`),
  both services active. Removed the speculative state change from source;
  device still contains that experiment's library until the next staged build.
  Small-allocation successes do not establish large-allocation support.
- Further isolation with original packet state: declaring 32 KiB while accessing
  only its first 512 bytes passes (`shared-low-128.log`). Accesses spread below
  16 KiB also pass (`shared-mid-128.log`). The boundary variant uses indices
  `31 + lane*32`, reaching float index 4095 / byte address 16380; it passes
  16 dispatches with workgroup 128 (`shared-lastword-128.log`). All outputs and
  guard regions checked; no new faults; services active.
- Ruling: cap the current driver at **16 KiB shared memory**, reject larger
  allocations before compilation/submission, and keep >16 KiB addressing as an
  unresolved implementation/hardware question. This reflects demonstrated
  address coverage, not a claim that A506 physically has only 16 KiB. It meets
  the core minimum and accommodates ncnn's 1 KiB reduction scratch; no target
  model/performance or broader conformance requirement is waived. Compiler
  `cs.req_local_mem` now records the actual static allocation for constant/LB
  budgeting. Cost if wrong: conservative cap may exclude otherwise feasible
  kernels until the larger-address issue is resolved.
- Rejection regression RED: 32 KiB shader compiled successfully before the cap
  (`shared-reject-red.log`), failing the expected rejection without dispatching.
  GREEN: rejects with FEATURE_NOT_PRESENT/null pipeline before GPU work
  (`shared-reject-green.log`). The 16 KiB last-word reduction remains green
  after the cap/bookkeeping changes (`shared-cap-lastword-green.log`).
- Audit byte-copy regression RED: valid unaligned copies fail EndCommandBuffer
  (`transfer-bytes-red.log`). GREEN: preserves full allocation/guards over three
  repeats including GPU fill → unaligned copies → aligned GPU copy
  (`transfer-bytes-green.log`). Aligned copies retain CP_MEM_TO_MEM. Unaligned
  copies currently use a CPU memmove after all preceding operations have drained,
  with BO CPU preparation and a host ordering barrier; this is an explicit slow
  fallback and depends on current serial queue execution. The regression uses
  separate buffers sharing a BO; a distinct-BO case remains to be covered.

## Kokoro conversion investigation

- Host converter environment is on shared scratch (`model-env`, Python 3.13.17,
  ONNX 1.23.2, ONNX Runtime 1.24.4). No model artifacts enter this repository.
- `scripts/prepare-kokoro.py` extracts the existing fp32 model at saved decoder
  boundaries and validates ONNX structure. Records source/fixture/output hashes
  and operator counts. Fixed fixture is `streams/kokoro-gpu/work/b-en.npz`:
  213 acoustic frames, 127800 waveform samples. This is not a new toy model.
- Converter: PyPI pnnx 20261020 manylinux x86_64 wheel, SHA256
  `d50d921e5897293d616444191c1a2dc5366423c81bdb4bd22a985fe8071cf17c`.
  Extracted standalone binary under `converter/pnnx-20261020/pnnx/pnnx` avoids
  installing a redundant Torch/CUDA stack. Metadata retained in `source.json`.
- Whole decoder ONNX conversion exits zero but leaves unrecognized graph
  operations: `torch.gt`, `Tensor.to`, `pnnx.Expression`, `CumSum`, `STFT`,
  `torch.lt`, `torch.where`, `Gather`, `ScatterND`. A converter success exit is
  therefore NOT model compatibility. Logs/param/bin remain under `kokoro/`.
  Three Deconvolution1D plus one depthwise deconvolution also lack Vulkan
  implementations in pinned ncnn; CPU fallback must be explicitly measured.
- Ruling: evaluate the neural decoder core with harmonic-source construction
  and inverse STFT outside ncnn — the existing decoder already separates those
  operations, and this avoids a speculative framework rewrite. Costs if wrong:
  conversion/integration work may not produce an end-to-end speedup; M5/M6 still
  require waveform/reference, fallback, memory and latency evidence. Do not run
  the 279-GFLOP fixture on the GPU before bounding large dispatches.
- Neural-core extraction/conversion succeeds with **no unknown ncnn layer
  types** (`kokoro-core/convert.log`): 69 Convolution1D, 58 InnerProduct,
  108 Reduction, plus elementwise/layout operations. Two Deconvolution1D and
  one DeconvolutionDepthWise1D lack Vulkan implementations; ExpandDims is also
  not a Vulkan class. Actual fallback behavior/overhead needs graph execution.
  Saved inputs include the original harmonic-source tensor. CPU ONNX Runtime
  reference executes in 1.40 s on this host, output `[1, 22, 25561]`, all finite;
  arrays/raw fp32 reference and input files retained under `kokoro-core/`.
  This does not yet prove ncnn CPU or GPU numerical compatibility.
- Host pinned ncnn build succeeds. Full neural-core CPU comparison **fails**
  (`kokoro-core/ncnn-cpu.log`): 553234/562342 outputs exceed the predeclared
  `0.002 + 0.002*abs(reference)` tolerance, relative RMS error 0.7606, maximum
  absolute error 69.682. Runtime 4.65 s on this host. This is a conversion or
  graph/runtime semantic mismatch, not GPU evidence; localizing the first
  divergent intermediate before attempting the model on device. Input loader
  now reads logical tensor counts rather than ncnn allocation padding.
- Localized the conversion failure: the first style projection (ncnn blob 68)
  agrees with NumPy using the original ONNX weights to max absolute error
  4.77e-7. All 108 pnnx `torch.mean` nodes have scalar `dim=2` rather than a
  dimension list; generated ncnn `Reduction` nodes consequently have empty axes
  (`-23303=0`). Source cause: pnnx
  `src/pass_onnx/fuse_constant_as_attribute.cpp` treats any one-element axes
  tensor as a scalar; `pass_ncnn/torch_mean.cpp` reads only its vector `.ai`.
  ncnn's `Reduction::forward` then reduces no axes for these rank-two tensors.
- `scripts/fix-kokoro-reductions.py` is a narrow, guarded model-conversion
  adaptation: verifies all 108 rank-three, batch-one, time-axis means and the
  exact bad ncnn parameters before replacing the empty list with axis 1 after
  batch removal. Preserves original artifacts and rejects unrelated shapes or
  already-correct reductions. No framework source changes.
- The corrected graph passes **all 562342 outputs** against the original ORT
  reference with the unchanged tolerance: max absolute error 5.72205e-5,
  relative RMS error 3.40072e-7, mismatches zero, host runtime 4.01 s
  (`kokoro-core-fixed/ncnn-cpu.log`). This validates this FP32 neural-core
  fixture's conversion only; it is not GPU/model-waveform/performance evidence.

## ncnn reduction integration in progress

- Pinned ncnn's Reduction hardcodes 256 threads, exceeding the current Radish
  limit 128. Compile inspection shows 16 full plus 12 half registers; even 128
  invocations would exceed the A506's observed combined-register budget. No
  reduction dispatch was attempted in that configuration.
- The first compile harness was too weak: `Reduction::create_pipeline` returns
  zero even when its internal pipeline creation fails. The log contains
  `vkCreateComputePipelines failed -8` despite the harness's PASS message.
  Added actual non-null pipeline-handle checks and backend-error log rejection;
  those messages must never count as successful compilation evidence.
- Ruling: adapt ncnn's non-subgroup reduction to 64 threads, preserve its
  subgroup path at 256, specialize the input stride/tree width together with
  local size, derive dispatcher width from the pipeline and propagate creation
  errors. This is a small isolated backend adaptation, not a higher advertised
  driver limit. Cost if wrong: an upstream upgrade needs re-audit and smaller
  groups may reduce performance; full reference comparison remains required.
- First adaptation tried `gl_WorkGroupSize`, but ncnn's GLSL source assembly
  declares workgroup layout after the body, so glslang rejects that use. Changed
  to an explicit workgroup-size specialization constant. Compilation now passes
  with a non-null pipeline (`ncnn-reduce-specialized-compile.log`): local64,
  16 full/12 half registers, 1150 instructions, no spill. The raw compile-only
  log's earlier PASS-with-error must not be substituted for this check.
- First actual reduction run still **faults** (`ncnn-reduce-first.log`), at
  width1/channels3 before any successful mean result. GPU testing stopped;
  both services remain active. Logs and devcoredump saved under
  `evidence/ncnn-reduce-fault*`. Captured state identifies the long reduction
  program (full16/half12, local64, gridY3, instruction preload disabled because
  length exceeds the a5xx preload limit), not a small upload/packing program.
  Combined register demand fits one 64-thread workgroup under the observed
  budget, but this does not prove safe concurrent workgroups or the long-code
  path. Root cause remains unproven. Do not retry this workload unchanged.
- Reproducible adaptation: `patches/0002-ncnn-reduction-workgroup.patch` against
  the pinned ncnn checkout. `build-ncnn.sh` applies or verifies it without
  overwriting conflicting local changes. No ncnn commits/pushes performed.
- Current driver regression: default32 + chain48 + coherent32 + span-write32 +
  span-copy32 = 176 dispatches pass, loader lifecycle24 pass; logs
  `m3-{vector,chain,coherent,write,copy,loader}-regression.log`. Chain test buffers
  now explicitly include transfer usage, as required for its fill commands.
  No new GPU faults in these runs; both services active. Log-checker tests4/4.
- Current cumulative Mesa source snapshot:
  `patches/0003-radish-m3-cumulative.patch`, SHA256
  `aad5856347d03a8f0e152f5669929709d76f4ad2623bc3817cacfcb39cf8b032`.
  Reverse apply-check passes. This replaces the old M2 patch when reconstructing
  from the pinned base; it is NOT an incremental patch to apply after 0001.
  M3 remains incomplete; known audit gaps are retained in `VULKAN_AUDIT.md`.

## Bounded dispatches and recovery detection

- Task 6 adds at most 64 workgroups per hardware submission, or one group for
  shaders with barriers, with original logical counts and chunk base parameters.
  This limits groups, not arbitrary shader runtime. It is an interim correctness
  policy; the model/performance requirement and hangcheck remain unchanged.
- Split-only regression is RED at vector index 2048 of 4099, the first missing
  chunk (`dispatch-chunk-red.log`), with no GPU fault. Initial base lowering then
  double-added WorkgroupId while GlobalInvocationId was correct; the new 3D
  test catches group 64 becoming 128 (`grid-base-double-offset-red.log`).
  Source cause: `nir_lower_system_values.c:215` already adds the base for the
  API WorkgroupId. Removed private re-addition and retained the intrinsic via
  `has_base_workgroup_id=true`. See ABI version 2 in `ABI.md`.
- Corrected grid tests pass both plain and shared variants: each checks 16 API
  dispatch calls (including six zero-size calls), all records, independent
  group/global/local IDs and original NumWorkgroups, changed salts, partial
  output bounds and guards. X/Y/Z chunk boundaries are covered. Logs
  `grid-base-green.log`, `grid-base-shared-green.log`. Vector/index 32 and 16 KiB
  last-word shared 16 also pass after subdivision (`dispatch-chunk-green.log`,
  `dispatch-chunk-shared-green.log`).
- `FD_CTX_FAULTS` baseline is queried at device creation and checked after each
  successful fence wait. Query failure or count change returns DEVICE_LOST.
  Destinations are zeroed: matching kernel copies a four-byte `int` whereas
  Mesa passes eight bytes. Matching kernel `msm_gpu.c:490` increments the counter
  before forcing the bad fence complete at 552–554. Standalone IOMMU crashstate
  paths do not all increment it; full output and kernel-log checks stay required.
- ncnn one-group attempt **still hangs** at width 1 / channels 3
  (`ncnn-reduce-bounded.log`). This time `vkQueueSubmit` returns **-4 / DEVICE_LOST**
  and execution stops before readback. Both services remained active; testing
  stopped for inspection. Captured `evidence/ncnn-reduce-bounded-fault.devcoredump`
  (devcd22) and `-dmesg.txt` on shared scratch; no captures enter this repository.
- Dump confirms CP_EXEC_CS payload `[0, 1, 1, 1]`, local 64, logical grid 1×3×1,
  base 0, correct push constants and 16-byte descriptors. Shader BOs are now
  marked for dump capture; decoded shader bytes reproduce the compile hash
  `66f7ae7cf6acef1d2852adadd9e1a77f1926a1e80e64271d3ea7360931f0ada8`
  including Mesa's zero padding and thread-size flag. No wrong-shader/upload
  evidence. Constants do not overlap: pushes c0–c7, params c8, immediates c12–c13.
  Seven barriers follow reconvergence points; shared addresses stay below 256 B.
- Independent source review corrects an earlier assumption: this Mesa base
  already combines unmerged full/half banks in `ir3_ra.c:2850–2915`, including
  barrier budget splitting and spilling. Its A506 settings imply a looser 4096
  register×thread budget than the earlier measured 2048. The failed shader fits
  both at local 64: `(16+12/2)*64=1408`. Printed max_waves still ignores a5xx half
  registers. No evidence yet that register footprint explains this failure.
  `E40801C3` is RBBM_STATUS from hardware hang detection, not a decoded CP error.
- ncnn harness now explicitly converts uploaded matrices to scalar packing
  before direct Reduction calls. `record_upload` packs channel 4 regardless of
  `use_packing_layout`; normal Net execution handles layer packing requirements,
  but the direct operator harness must do so itself. This does not explain the
  first failing channel 3 case; the 18-case mean suite still has no passing run.
- Standalone discrimination after fault inspection: short and long uint
  xorshift/shared-tree shaders both compile and pass one-group smoke, then 16
  full boundary dispatches each (`{short,long}-shared-{compile,smoke,full}.log`).
  Both use local 64 and 1 KiB shared. Short: 196 dwords / 6 full / 0 half. Long: 3268
  dwords / 8 full / 0 half, well beyond the 512 encoded-instruction preload threshold,
  no spills. Every output and guard matches independent wrapping-uint arithmetic.
  Thus unpreloaded instruction fetching with this barrier pattern works; ncnn's
  divergent loop, indirect register-array accesses and division lowering remain
  candidates. This does not prove all long shaders correct.
- Final bounded-dispatch regressions: chain 48, coherent 32, span-write 32,
  span-copy 32, byte transfers 3 repetitions, loader 24 lifecycle cycles and ncnn
  ReLU 3 sizes all pass (`bounded-*-regression.log`). No new fault/recovery
  messages; both services active. Kernel-log checker tests 4/4. Independent review
  found no blocker in subdivision/base arithmetic or fault-counter lifecycle.
- Source checkpoint `patches/0004-radish-bounded-dispatch-cumulative.patch`,
  SHA256 `61f39d1e70326e6ceacdf0ab837fdfaa5d54a02b5896875cc26771edc07ac584`;
  reverse apply-check passes. Apply instead of earlier cumulative patches.
  This is selected M3 progress, not M3 completion or Vulkan conformance.

## Incremental build repair

- Mesa `.ninja_deps` contained a corrupt 56-byte encode.c.o path record at
  offset 584120. Ninja 1.13.2 recovery preserved the bad record and discarded
  everything later on every invocation, causing repeated compilation. Read-only
  audit traced the recovery-offset behavior in upstream `src/deps_log.cc`.
  The duplicate path ID is consistent with earlier concurrent writers, although
  their exact original occurrence was not captured. Never invoke two Ninja
  processes against one build directory, including dry runs during a build.
- With builds stopped, verified the original checksum, backed up the entire log
  under shared `evidence/mesa-ninja-deps-before-repair`, and removed just that
  invalid record. Valid subsequent timestamps/paths were retained. No source or
  shared sysroot files changed during this repair.
- Cross C++ dependency paths had a separate problem: Clang used sysroot/lib
  (a symlink to usr/lib); Ninja's lexical `..` normalization generated missing
  sysroot/include paths. Canonical `--gcc-install-dir=.../sysroot/usr/lib/gcc/
  aarch64-linux-gnu/16` in build scripts fixes these paths without changing the
  reused sysroot. Meson's cpp_args must be an actual two-element array; an
  initial comma string became one ignored warning flag and was corrected.
- Verified ncnn incremental dry run: `ninja: no work to do`. Mesa's subsequent
  real build runs only its normal git_sha1 generator and stops via restat;
  no repeated encode or C++ compilation (`long-shared-build.log`).

## Reduction workaround and discriminating probes

- New `control.c` / `control.comp` tests isolate divergent loops, indirect
  temporary arrays, dynamic signed division/remainder, long loop bodies and
  SSBO floating-point loads followed by shared reductions. All six modes pass
  compile inspection, one-group smoke and 24 full dispatches each, with complete
  output/guard comparisons. Widths include 1, 31, 32, 33, 64, 65, 213 and 4099;
  repeated salts/counts include zero iterations. Divisors 1/3 also pass. Logs
  `control-*-{compile,smoke,full}.log` and `control-*-one-three.log`.
- Physical32 ncnn reduction passes all 18 means: widths 1/31/32/33/213/4099,
  channels 3/4/7, scalar-packing conversion and independent double reference.
  The backend default is now 32 for the non-subgroup path. Both the diagnostic
  override and ordinary layer creation pass (`ncnn-reduce-local32.log`,
  `ncnn-reduce-default32.log`); ReLU regression also passes. This supersedes the
  earlier no-passing-means status, without resolving physical64 execution.
- Cumulative ncnn patch `0005-ncnn-reduction-workgroup32-cumulative.patch`
  replaces 0002 when applying to the pinned base. It retains subgroup256,
  specializes input stride/tree/local size together and propagates pipeline
  errors. `build-ncnn.sh` verifies it or safely migrates the earlier64 patch.
  Fresh-base and migration paths were checked in isolated temporary directories.
- The physical64/logical32 diagnostic compiles the exact passing physical32
  shader: hash `96b93ec245227d4c6dc889317b8b959dc81aea7cff82a17a162e82667f497b41`,
  1760 dwords, 16 full/12 half registers. It still hangs at the first width1 /
  channels3 case and returns DEVICE_LOST. Testing stopped for capture/review;
  both services remained active. Logs `ncnn-reduce-physical64-logical32-*.log`,
  dump `evidence/ncnn-physical64-logical32-fault.devcoredump` (devcd23).
- Independent capture review reproduces that shader hash and confirms physical
  local64, one hardware group and the same 176-dword command stream as the
  earlier64/64 fault. Extra reduction-tree stages are therefore not necessary
  for this failure. Physical wave participation matters; the scheduling/resource
  cause remains unknown. No unchanged fault retry was performed.

## Vulkan convolution and projection operators

- `tests/ncnn/operators.cpp` runs actual Vulkan Conv1D/InnerProduct layers,
  validates allocated child/operator pipeline handles, handles packing explicitly,
  and requires forward to record dispatches beyond upload/download. References
  independently accumulate original FP32 values in double; fixed tolerance is
  `2e-5 + 2e-5*abs(expected)` for at most 176 products per output.
- Compile-only inspection covers all 26 shader programs (`operators-compile.log`).
  Conv kernels use local32 (full16/half1), local32 (full48/half1), or local64
  (full16/half2). IP kernels use full8/half1 at local32/64. Padding variants use
  local64, full8, at most9 half. No category7 barriers appear. This scoped resource
  inspection permits bounded tests; it is not proof of arbitrary shader safety.
- Small smoke then all five cases pass (`operators-smoke.log`,
  `operators-full.log`): Conv K3 155 outputs, K7 216 outputs, K11 6496 outputs;
  IP13→7 and IP128→1028. Every logical output matches; largest absolute error
  3.81310e-6 (K11). No new GPU faults/recovery, both live services active.
  Device sessions use MemoryMax384M and RuntimeMax90 s under the lock.
- These are M4 FP32 operator results. Later full-core and waveform evidence is
  recorded below; FP16 and the M6 performance win remain unproven.

## Real graph and waveform reference

- `prepare-style-probe.py` extracts the exact first Crop→InnerProduct path from
  the validated graph, the original 530452-byte weight prefix and style input.
  Independent double dot-products match the saved full CPU graph's blob68 to
  5.34e-7. Metadata/hashes and private fixture are under `kokoro-style/`.
- `model.cpp` retains the CPU/full-core/blob-dump modes and adds Vulkan, style
  and load-only modes. It disables optional arithmetic/storage features, gives
  the 58 known style projections 1D shape hints, validates style pipeline and
  uploaded-weight handles, completes input uploads before extraction, and
  compares all scalar outputs. CPU-only builds remain supported.
- `0006-ncnn-execution-trace.patch` adds opt-in actual CPU/GPU layer paths,
  return values, recorded dispatch groups, fallback transfers and internal
  submissions/resets. Recorded work is not a completion/timing claim. Split
  and other Vulkan paths may record zero groups. The normal backend is unchanged.
- Host CPU regressions pass: all 1028 style outputs and all 562342 core outputs
  (`style-host-cpu.log`, `core-host-cpu-traced.log`). Core max error remains
  5.72205e-5 with unchanged tolerance. Its 8.283 s traced run overlapped the CTS
  host build and is not a performance benchmark.
- Device style load inspection (`style-device-load.log`) shows five compact
  programs, at most 8 full/1 half registers, locals32/64, no category7 barriers.
  Actual graph execution then passes all 1028 outputs, max error4.76837e-7
  (`style-device-gpu.log`): Crop records1 group and InnerProduct38, both GPU.
  Cold load0.751 s, transfer+extraction0.109 s, process peak RSS58016KiB. These are
  single diagnostic timings, not warm performance evidence. No faults; services
  active. Output pulled to `kokoro-style/ncnn-device-vulkan-output.f32`.
- Memory preflight: 995164KiB available, 702708KiB swap free, both services
  active (`model-preflight.log`). Full core weights occupy213042160 bytes.
  Source estimate: about198.4 MiB persistent GPU weight BOs plus11.5 MiB CPU
  deconvolution weights; worst convolution packing transient about38.35 MiB.
  Mappable weight uploads directly copy into GPU memory and release CPU packed
  arrays. The later full-core run records process/cgroup peaks below;
  sustained device-wide memory behavior still needs measurement.
- Waveform reference caught an export-specific normalization difference before
  GPU integration. A conventional centered Hann iSTFT failed at9.54 dB SNR
  (`reference-waveform.log`). The original ONNX tail stores only20 window_sum
  values; Gather/Div/ScatterND normalizes those indices, then scales by4 and
  trims10 samples at each end. Synthesizing a full overlap envelope changes gain.
- `prepare-kokoro.py --neural-core` now also extracts the original waveform
  tail. `validate-kokoro-waveform.py` independently evaluates its stored basis
  and normalization with NumPy. Extracted ORT tail matches original fixture at
 117.38 dB; the NumPy tail on ORT core matches117.20 dB. On the ncnn CPU core it
  passes all 127800 samples, max error4.03158e-6, SNR106.22 dB
  (`ncnn-cpu-waveform.log`). The predeclared gate remains
  `2e-4 + 2e-3*abs(reference)` per sample and SNR≥50 dB. This validates the fixed
  saved-source fixture; later Vulkan-core waveform validation is recorded below.
  Runtime harmonic generation remains outside this check.

## Full-model load and compiler lifetime

- First full-core load-only attempt reaches 2033 compiled programs, then the
  150-second runtime cap ends it before inference (`core-device-load.log`).
  No GPU fault; services active. Its systemd record reports 640 MiB memory and
  600.4 MiB swap peaks (`core-load-service-result.log`). Do not treat the missing
  final model output or lack of GPU fault as successful loading.
- Radish retained each parent `ir3_shader` and its NIR for the pipeline lifetime.
  Turnip instead destroys that parent immediately after standalone variant
  creation (`tu_shader.cc:3950–3974`). Radish now follows this pattern and uses
  the variant's device-owned compiler for queue state. Independent ownership
  review confirms variant names, constants, binary and cached data are separately
  owned. No shader ABI or emitted arithmetic changes are intended.
- Style graph, five operators and 18 ncnn means all pass after the change
  (`style-compiler-free.log`, `operators-compiler-free.log`,
  `reduce-compiler-free.log`), with no faults and both services active.
  The full load repeats with the same model/options/disassembly/640 MiB limit;
  swap is additionally capped at128 MiB. It reaches2187 compiled programs but
  still hits150 s while dumping NIR/ISA. Peak falls to356 MiB with zero swap, and
  all2033 hashes shared with the original attempt are identical. This confirms
  the retention fix's benefit; complete model loading remains unproven at this
  entry. Normal full inference is now attempted without verbose shader dumps.
- The normal full run completes cold loading in166.041 s, with all1368 graph
  layers available, then executes through early generator operations with
  successful intermediate submissions. Its180 s process cap ends it at CPU
  deconvolution layer582 before output (`core-first-full.log`); peak438.9 MiB,
  zero swap, no GPU fault, services active. Full output was unverified at this
  checkpoint. The next attempt allowed 300 s for cold compilation plus inference,
  keeping MemoryMax640 MiB/MemorySwapMax128 MiB and the GPU hangcheck. This is a bounded
  private process beside the live services, not listener downtime.
- The driver checkpoint at this stage was cumulative patch0007, SHA256
  `7ce02f0a61dbcf0d3872b82983b35b14007fa7d976e6b76800844ff6f24e0a54`.
  Reverse apply-check passed; current checkpoint 0016 is documented above.

## Full-core correctness and remaining performance work

- ncnn's Qualcomm API<1.0.66 heuristic disables pipeline artifact reuse for
  Radish's experimental 1.0 API. Patch 0008 adds an opt-in exception requiring
  vendor `0x5143`, device `506`, exact Radish device name and
  `RADISH_NCNN_PIPELINE_CACHE=1`. Full cache keys, ownership and API reporting
  are unchanged. It is independent of patches 0005/0006; `build-ncnn.sh` applies
  all three. SHA256 is in `patches/README.md`.
- `scripts/adapt-kokoro-layouts.py` replaces this fixed graph's 60 vector
  ExpandDims operations with equivalent Reshape operations. It guards source
  hash, topology, axes and shapes; original weights/inputs stay unchanged and
  private. Adapted param SHA256:
  `8dd76d964aaf563cdf619f1f749db588f9b3694fef55b0e75d4e17cf2f414e43`.
  All 562342 CPU outputs remain bit-identical to the validated baseline
  (`kokoro-core-layouts/layout-cpu-validation.json`).
- `core-cache-layouts.log` records the completed device run: cold load 9.762 s;
  extraction plus input/output transfers 192.444 s; all 562342 outputs pass
  `0.002 + 0.002*abs(reference)`, max absolute error 0.000118255615,
  relative RMS 7.813705e-7, zero mismatches. Actual trace has 1360 GPU layer calls,
  including 60 Reshape calls, and three CPU deconvolutions. GPU calls may record
  zero dispatches for layout/alias operations; this is not an all-GPU graph.
- Capped service runtime 202.563 s, memory peak 389.6 MiB and zero swap; process
  peak RSS 100416 KiB excludes separately accounted GPU allocations. No new GPU
  fault/recovery messages; both live services active. These are one fixture's
  diagnostic measurements, not warm or end-to-end performance evidence.
- `ncnn-vulkan-waveform.log`: the Vulkan-core output plus saved harmonic-source
  fixture and CPU waveform tail passes all 127800 samples at 24 kHz; max absolute
  error 1.4829320966330828e-5 and SNR 97.90814527063519 dB. Gates remain
  `2e-4 + 2e-3*abs(reference)` and SNR ≥50 dB. Runtime source generation, live
  waveform integration and a CPU/GLES performance comparison remain open.

## CTS preparation

- Headless AArch64 `deqp-vk` built at source revision
  `5a9cb32498158eb60774cf8a3d4e0b8c68f7fe9e`; binary SHA256
  `0e715f9c53ec1dfd742d93a7a45740cbf4a8caa9193fe761e71dd6347c2a6660`.
  `build-cts/DEPENDENCIES.json` pins dependencies; `BINARY.json` records build
  success and runtime linkage. Existing GLES build/sysroots are preserved.
- `build-cts/CASELIST_REVIEW.json`, `api-memory-first.txt`, `compute-first.txt`
  and `known-gaps.txt` record reviewed bounded cases and exclusions.
  `FIRST_RUN_PLAN.json` prepares a capped queue-properties smoke run. No device
  CTS case has executed; build/caselist preparation is not conformance evidence.

## Dilation extension and integrated runtime checkpoint

Incremental 0018 appends dilation as specialization ID4 and changes only the
input tap offset, retaining the existing FP32/packing/stride/output gates,
K/weight order, shared layout and unconditional barriers. Dilation 3/5 compile
at local 16x8, 16 full/0 half registers, 950 instructions, 1156 dwords and four
loops. Each operator uses six tiles and passes every output at the unchanged
2e-5 absolute/relative gate: 23424 values/max 7.09755113e-6 for dilation 3 and
20864/max 1.641165e-5 for dilation 5. Math dispatch wall times are 8.648155/8.673573 ms;
these single fixtures do not establish model latency. Logs are
`operators-tiled-dilation{3,5}-{compile,execute}0018.log`.

`core-instancenorm-tiled-dilation-repeat2.log` records cold load 9.357 s,
69.862470 s first and 69.678044 s warm. Both 562342-value comparisons pass,
max error 0.000129699707, relative RMS 7.72155564e-7, zero mismatches.
The process uses 416.7 MiB peak and zero swap; both services stay active and no
new GPU fault is recorded. Relative to 0017's 74.317956 s warm sample, this is
6.24% less time; warm sample count remains one for each variant. The same fused
CPU graph's 32.079044 s warm mean remains faster. These are core timings.

The resulting waveform also passes all 127800 samples in 0.103231 s, max error
1.21463973e-5, SNR 98.036838 dB, zero mismatches, 6.5 MiB peak/zero swap and no faults
(`core-instancenorm-tiled-dilation-waveform-device.log`).

`kokoro_shapes.cpp` propagates metadata without running tensors. Original
1739 shapes and fused 1307 shapes match CPU captures byte-for-byte at both T78
and T213, while preserving every non-hint parameter and layer/blob order.
`runtime-integrated-host-short-first.log` exercises the complete new runtime
with fresh host frontend/source, inferred shapes, packed CPU core and portable
tail. All eight boundary files match the existing host T78 reference exactly;
all 205942 core values pass (max 3.81469727e-5), and all 46800 waveform samples pass
(max 1.07027590e-5, SNR 102.704341 dB). This is host correctness evidence, not tablet
performance. The runtime stages no intermediate tensors to disk before core
execution and destroys ORT before loading ncnn weights. Generation and accuracy
validation are separate; an output file alone is not an accuracy pass.

### First complete tablet synthesis

`runtime-single-short-first.log` runs the fresh frontend/source, metadata-only
shape propagation, GPU core and waveform in one private process. It accepts
T78 inputs and emits 46800 samples without host work between inference phases.
Timings: frontend including ORT destruction 8.364384 s; all 1307 shape hints
0.038182 s; core load 9.727086 s; core extraction/transfers 25.190135 s; core
scope including load/cleanup 35.187278 s; tail 0.038283 s. Total synthesis is
43.652206 s, through publication 43.656357 s, service wall 43.711 s. Peak memory
is 343.6 MiB with zero swap, no new GPU faults and both live services active.

`runtime-single-short-first-validation.json` binds the generated output hashes,
run manifest and device evidence to a reference using all eight exact captured
boundary files. All 205942 core outputs pass (max 0.000105142593, relative RMS
9.27539488e-7), as do all 46800 audio samples (max 1.47223473e-5, SNR 96.917732 dB).
The tablet frontend uses ORT 1.28.2; post-run CPU references use ORT 1.31.1 only
for decoder/tail, without recomputing frontend/source. Neither tolerance changed.
This is one complete startup run, not a resident warm or sustained measurement.

The private runner explicitly stages and hash-checks its Vulkan loader as well
as the selected ICD. A review caught the initially missing loader before device
execution. It also records immutable generated-output identities; changing even
one captured core byte is rejected before the accuracy comparison can run.

### Full-length synthesis and equivalent CPU baseline

`runtime-single-full-first.log` extends the same executable to fresh T213
inputs. Frontend including ORT release takes 6.130650 s; metadata 0.031212 s;
core load 9.669170 s; GPU extraction/transfers 67.889310 s. Complete synthesis
is 84.223461 s, through publication 84.231595 s and service wall 84.317 s.
Peak memory is 457.2 MiB with zero swap, both services active and no new faults.
`runtime-single-full-first-validation.json` passes all 562342 core values
(max 0.000102996826, relative RMS 7.81135939e-7) and all 127800 waveform samples
(max 9.19587910e-6, SNR 98.355163 dB), with unchanged gates. This uses fresh
frontend/source values, so its first core timing does not replace the fixed
fixture's 69.678044 s warm benchmark.

`runtime-single-full-cpu-first.log` runs the same executable with packed FP32
CPU and six ncnn threads, retaining four frontend threads. Frontend 5.736043 s,
metadata 0.031603 s, core load 1.144992 s, core extraction 32.626909 s, tail
0.102624 s; synthesis 39.789900 s, through publication 39.807908 s, service
40.049 s. Peak memory 461.2 MiB, zero swap, both services active, no faults.
All eight generated frontend boundary files match the GPU run byte-for-byte
(`runtime-single-full-cpu-gpu-boundary-identity.json`). These are one startup
run per backend, not controlled repeated thermal or resident-runtime results.
GPU full synthesis is about 2.12 times slower; the performance goal stays open.

`runtime-single-full-cpu-first-validation.json` also passes all 562342 CPU core
values (max 0.000362396240, relative RMS 2.51363812e-6) and all 127800 waveform
samples (max 5.05000353e-5, SNR 88.225028 dB), with zero mismatches. Both backend
measurements therefore have complete same-boundary reference gates. Warm
resident synthesis, cancellation and sustained thermal measurements remain open.

### Next measured optimization

Private `warm-profile-alias-20261101-zcy9kch2/{REPORT.md,DESIGN.md,comparison.json}`
records complete deferred-command attribution for the 0017/0018 warm samples.
The 60 general convolution math commands fall from 49.092575 to 43.458908 s;
53 tiled commands account for 40.111435 s. The graph has 69 Conv1D layers in
all: the other nine simple convolutions were not part of that math-command
category. All convolution owners, including helper commands, total 45.813589 s.
The 48 exact five-operation Snake chains total 6.978474 s. These are command
wall timers including submission/wait overhead, not isolated GPU/fence timings.

The immediate candidate is an opt-in metadata alias for 49 FP32 pack4
normalization reshape pairs. Their 98 copies total 3.384351 s and about 1.55 GB
of logical reads/writes. Require actual dimensions, identical scalar order,
valid buffer capacity/offsets and unchanged ownership. Keep five scalar pairs
and all incompatible reshapes on the copy path; preserve downstream in-place
clone protection for Split residuals. Those clones may replace some removed
copies, so the attributed copy time is not a promised latency saving. No result
is accepted before the operator/full-model/waveform gates. The preserved source
checkpoint is public commit `96de5c7f0695f25af645cf9cc857378a0a79138f`.

### Reshape alias candidate 0019

`RADISH_NCNN_RESHAPE_ALIAS=1` enables a narrow exact-Radish FP32 pack4 alias
between [C,W] and [C,1,W]. All other paths remain unchanged. Runtime checks
cover dimensions, packing/storage, cstep/nstep, allocator, offsets, descriptor
capacity and allocation bounds; normal VkMat assignment preserves shared
ownership and access state. Existing InstanceNorm clone-on-shared-input behavior
remains intact. Both cross builds and the integrated runtime build pass.

`operators-reshape0019-compile.log` compiles all three focused cases before
upload/submission; 30 compiled shaders use at most 8 full and 5 half registers.
Both `operators-reshape0019-{alias,default}-execute.log` runs pass: widths 33
and 213 with eight channels check both pack4 alias directions, full independent
normalization/residual-sum references and bit-exact retained Split input. Width
213 with three channels checks the unchanged padded scalar-copy fallback.
Maximum errors are 3.62247091e-7, 3.00622287e-7 and 2.80124335e-7 respectively,
with unchanged 2e-5 absolute/relative gates. No new faults; services active.
The full-model experiment rejects this bidirectional candidate for performance:
`core-instancenorm-reshape-alias-repeat2.log` takes 109.290526 s first and
109.107981 s warm after 9.432 s load, versus the accepted 69.678044 s warm.
All 562342 values pass, and the frozen output is byte-identical to 0018.
Peak memory is 420.5 MiB, zero swap, no faults, both services active. The
normalization inputs retain Split aliases, causing protective transfer clones.
The current driver copies those through one CP_MEM_TO_MEM packet per four bytes
and a submit/wait every 4 KiB; one 13 MB clone takes about 3.3 s. This candidate
stays default-off and is not an accepted optimization. Narrowing the fast path
to post-normalization rank3→rank2 aliases is the next experiment.

Device-model snapshots now include readonly devfreq residency counters. Across
this rejected run they account for 212.572 s, of which 210.232 s (98.9%) is at
650 MHz. These are the counters' accounted times between wrapper snapshots,
not a per-kernel or complete-wall-clock frequency measurement. No power or
clock settings were changed.

The rejected bidirectional run removes 98 reshape commands but adds 20 large
clones: 174494720 bytes and 42610 extra DRM submits, costing 43.602329 s.
Total transfer wall rises from 0.057461 to 43.664125 s. The remaining 1941
command signatures match the accepted 0018 sequence exactly. See private
`warm-reshape-alias-result-0019-roigw0rw/REPORT.md` and public rejected source
commit `bf4d98f10f46d849de9d5ee97014eca7178afcf4`.

The revised 0019 admits only rank3(h=1)→rank2 output aliases, retaining the
original input-copy path. Its patch SHA is
`3f38da1822f50b5b46857e091e2366c96baf350136dd585615c7e663ec72ce57`.
Both builds pass. `operators-reshape0019-to2d-execute.log` passes all three
focused cases and verifies normalization reuses its copied input without
an extra clone, plus complete numeric and retained-residual checks. No faults.
The revised full-model repeat passes in 67.138187 s first and 66.919494 s warm,
all 562342 values, identical max/RMS errors to 0018 and bit-identical frozen
output SHA256 `0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4`.
Peak memory is 417.6 MiB, zero swap, no faults and both services active
(`core-instancenorm-reshape-to2d-repeat2.log`). This is 4.0% less wall time than
0018's 69.678044 s warm sample, with one warm observation per variant; do not
attribute the whole timing difference to the removed copies. The validated
waveform for the byte-identical core output remains applicable. Across the
wrapper snapshots, devfreq accounts for 128.264 s, 126.152 s (98.4%) at650MHz;
this is not a complete wall-time partition or per-kernel frequency measurement.

Completed runtime captures retain all their device paths and bytes. To recover
scratch space, hash-verified identical frontend graphs, driver/loader, parameter,
tail and executable files in the three completed single-process directories now
share hardlinks. No captured output or evidence file was removed. The bounded
locked maintenance log is `dedupe-completed-runtime-assets.log`; free space rose
from 364276 to 697432 KiB, and both services remained active.

The narrow candidate's exact command comparison confirms49 copy dispatches and
1516 submits removed, with no new transfers or large clones. All1990 retained
command signatures match0018 in order. Existing transfers remain96 small copies
(73728 total bytes). Removed copies account for1.230514 s of the baseline;
retained commands also ran1.373294 s faster, so the observed2.758550 s warm
improvement includes variation beyond this change. Private full evidence:
`warm-reshape-to2d-result-0019-2evqfkor/REPORT.md`.

The accepted output-only alias also passes the complete T78 runtime
(`runtime-single-short-to2d-first.log` and matching validation JSON): frontend
including cleanup 4.168688 s, metadata 0.032164 s, core load 9.847798 s,
core extraction 25.451257 s, synthesis 39.846221 s, through publication
39.850121 s, service wall41.872 s. Peak330.8 MiB, zero swap, no new faults,
both services active. All205942 core values and46800 audio samples pass, and
both output files are byte-identical to the earlier T78 run (SNR96.917732 dB).
The earlier first core was25.190135 s, so do not infer a T78 core speedup or
attribute the lower total to aliases; frontend startup fell from8.364384 s.
The private runtime wrapper now defaults this accepted alias control to1;
model/operator comparison wrappers retain explicit opt-in selection.

### Local32 tile64x32 candidate 0020

`0020-ncnn-conv1d-tiled32.patch` is incremental after 0018; the independent 0019
output-reshape alias remains available. SHA256:
`bf61ab2af644ddecb632c7c3d8e48ef261085f3a7c1ad68d657c33abf7cda3df`.
With the stack ending at 0020, `RADISH_NCNN_TILED32=1` enables this variant even
without `RADISH_NCNN_TILED`
and takes precedence when both are enabled. It remains disabled by default.
The previous validated checkpoint is `bccdcd3265dfbbc5b7c09f21377f2f38a1054820`.

The separate shader uses local8x4x1, a tile of 64 output channels by 32 positions,
BK32, 16 vec4 accumulators per thread and 12 KiB shared memory (A8 KiB+B4 KiB).
It preserves row-major weights, pack4 addressing/output transposition, bias,
K accumulation order and dilation1/3/5. Spatial tails retain both barriers.
Exact Radish/FP32/packing/shape gates still apply; disabling the new flag retains
the existing local16x8, 64x64 tiled path. Independent source review and ordered
reconstruction pass. The operator wrapper validates/forwards the new flag and
keeps distinct evidence tags; compile-only still returns before input/model
upload or operator submission. Existing full independent references are unchanged.

Canonical, SIMPLEOMP and integrated runtime builds pass
(`build-ncnn-tiled32-0020.log`, `build-runtime-tiled32-0020.log`). The focused
dilation1 compile records 32 full/0 half registers, 1476 instructions, 1766
dwords and four loops (`operators-tiled32-0020-compile.log`). The later model
load-only gate creates 167 artifacts, including 20 tiled32 programs; all 20 have
local8x4x1, 12288 shared bytes and 32 full/0 half registers. This satisfies the
declared F+H/2<=32 execution gate. Independent compiler audit matches all 20
expected specialization tuples covering 53 model layers, with no NIR scratch or
native LDP/STP spills. Accepted pipelines also imply zero private memory because
the pinned driver rejects nonzero private allocation. Private audit evidence is
`tiled32-resource-audit-0020-7_g20rz2/`. Model load/weight
upload completes in 17.360 s, peak 262.9 MiB/zero swap, no faults and both services
active (`core-tiled32-0020-load-allvariants.log`). This run executes no inference.

All 25984 dilation1 operator outputs pass the unchanged gate at shared-group
budgets 1/2/4/8, with max absolute error 3.42226122e-5. The logical math grid is
7x2x1, with local8x4x1. Command-wall measurements include submission and waits:

| Shared groups | Math time (ms) | Submits |
| --- | ---: | ---: |
| 1 | 28.872531 | 14 |
| 2 | 25.032780 | 8 |
| 4 | 23.930798 | 4 |
| 8 | 21.885115 | 2 |

Logs: `operators-tiled32-0020-d1-groups{1,2,4,8}.log`. All runs preserve both
services and report no new faults. Even the fastest is slower than the existing
0017 tile's recorded 10.868408 ms/eight submits. Reducing submissions has not
produced an operator speed win. Dilation3/5 at groups8 pass all 23424/20864
outputs, max errors 7.09755113e-6/1.641165e-5, with math times 19.058804/18.831616 ms
and two submits each; no faults, both services active. Full fixed-core repeat,
described below, also passes accuracy. These checks do not establish a general
resource-safety claim.

`core-instancenorm-tiled32-0020-repeat2.log` completes after 11.098 s cold load:
101.354178 s first extraction and 100.467084 s warm (one warm observation),
including input/output transfers. Both pass all 562342 outputs, max absolute
error 0.000129699707, relative RMS 7.72155564e-7, zero mismatches. The frozen
output SHA256 is
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4`,
identical to the accepted 0019/0018 output. No new waveform run was needed:
the existing 127800-sample waveform pass for those exact core bytes remains
applicable. This is not a new end-to-end runtime measurement.

Service wall is 3 min 33.367 s, peak memory 414.8 MiB plus 52 MiB swap, no new
GPU fault/recovery messages and both services active. The swap use differs from
the accepted 0019 run's zero swap and limits attributing all of the slowdown to
the shader. Available memory before/after is 1051232/1057644 KiB. The measured
warm time is 50.13% longer than 66.919494 s; reject 0020 for performance, retain
the old tile as the accepted path, and leave `RADISH_NCNN_TILED32=0` by default.
Warm command attribution contains the same 1990 commands: the 53 changed tiled
math commands rise from 39.290606493 to 73.200805943 s while their submits fall
from 25944 to 6512. The other 1937 commands take 23.822304502/23.456005835 s,
with unchanged 30523 submits; transfers remain 96. Total new submissions are
37035. These command-wall timers include submission/waits and any intervening
host delays; the result does not isolate instruction cost or swap effects.
The retained 1937 command signatures and all 1093 layer-path identities match.
Private comparison evidence is
`warm-tiled32-result-0020-nve2rvjz/comparison-to2d-tiled32.json`.
No budget or accuracy gate was relaxed.

### Local32 tile32x32 candidate 0021

`0021-ncnn-conv1d-tile32x32.patch` is incremental after 0020, SHA256
`a29dbbe0c3edf8912e4548cc873beca0ba3f1ba3197b6f1ee5f386bfd4f57dc0`.
The rejected 0020 source/results remain in published checkpoint
`b70b0a8c498dd8fabc4409acbface875de0c5e33`; private checkpoint
`pre-tile32x32-0021-s4vldxw9/` also preserves its source and seven binaries.
With 0021 applied, the existing `RADISH_NCNN_TILED32=1` control selects a tile
of 32 output channels by 32 positions, local8x4x1, BK32, eight vec4 accumulators
and 8 KiB shared memory (A4 KiB+B4 KiB). It still works alone or takes precedence
over `RADISH_NCNN_TILED=1`. Both absent, or TILED alone, behave as before.

The host Y grid changes from Cout/64 to Cout/32. Eligibility still requires
Cout divisible by 64; exact Radish, FP32 pack4, stride1, dilation1/3/5, K32
divisibility and all other restrictions remain unchanged. Weight ownership,
five specialization constants, sequential K accumulation, pack4 transpose,
spatial guards and both unconditional barriers are preserved. The accepted
64x64 shader and the Conv1D header are byte-identical to the prior checkpoint.

Independent review verifies unique coverage of all 1024 shared-A, shared-B
and output scalars, guarded dilation addressing and exact ordered patch
reconstruction. Private Vulkan1.0 GLSL/SPIR-V validation and AArch64 syntax-only
checks with warnings as errors pass; these are not device compiler results.
The existing width213, Cin=Cout128, K11 operator cases now expect grids
7x4 for dilation1 and 6x4 for dilation3/5, retaining the full independent
references and original tolerances. No new test matrix or API feature is added.

Canonical, SIMPLEOMP and integrated runtime builds pass
(`build-ncnn-tile32x32-0021.log`, `build-runtime-tile32x32-0021.log`). The focused
actual compile records 16 full/0 half registers, 950 instructions (541 NOPs,
409 non-NOPs), 1152 dwords, four loops, eight compiler max_waves and single
thread size, local8x4 and 8192 shared bytes
(`operators-tile32x32-0021-compile.log`). The private audit passes the zero
private/spill and F+H/2<=32 gate. This halves 0020's register count; reported
max_waves is a compiler model, not measured occupancy.

All 25984 dilation1 outputs pass at shared-group budgets 1/2/4/8 with unchanged
max absolute error 3.42226122e-5, no faults and both services active. The math
grid is 7x4; command-wall measurements include submission and waits:

| Shared groups | Math time (ms) | Submits |
| --- | ---: | ---: |
| 1 | 45.268188 | 28 |
| 2 | 39.411195 | 16 |
| 4 | 35.586862 | 8 |
| 8 | 33.229460 | 4 |

Logs: `operators-tile32x32-0021-d1-groups{1,2,4,8}.log`. Even the best focused
result is slower than 0020's 21.885115 ms and the accepted tile's 10.868408 ms.
Lower register use has not produced an operator speed win. Dilation3/5 at
groups8 also pass all 23424/20864 outputs, max errors
7.09755113e-6/1.641165e-5, with math times 29.094190/29.227679 ms and four
submits each (`operators-tile32x32-0021-d{3,5}-groups8.log`). No faults,
both services active. The separate full-model measurement is recorded below.

The model load-only gate passes all 20 specialization tuples covering the
unchanged 53 eligible model layers. Every program uses 16 full/0 half
registers, local8x4 and 8192 shared bytes, with no private/spill evidence.
The private parser `tile32x32-resource-audit-0021-acapgipl/audit.py` and its
`core-tile32x32-0021-load-allvariants-resources.json` record complete coverage;
all 0020 reports are preserved separately. Debug load takes 15.992 s, service
wall 16.262 s, peak 284.3 MiB/zero swap, no faults and both services active
(`core-tile32x32-0021-load-allvariants.log`). This includes weight upload but
executes no inference. The intended compiler register target is met, although
compiler max_waves is not a measured occupancy guarantee. At shared-group budget8,
the source grids project 13024 math submissions, twice 0020's 6512 but below
the accepted path's 25944. Extra input gathers and barriers can offset lower
register pressure; fewer registers alone did not predict performance.

`core-instancenorm-tile32x32-0021-first.log` completes one first extraction in
138.633996 s after 9.791 s cold load. All 562342 values pass, max absolute error
0.000129699707, relative RMS 7.72155564e-7, zero mismatches. Its output SHA256 is
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4`,
byte-identical to the accepted 0019/0018 core. Existing exact-byte waveform
evidence therefore applies; no new waveform or integrated runtime measurement
was performed. Service wall is 2 min 28.844 s, peak 425.2 MiB/zero swap, no new
GPU faults/recoveries and both services active. The log records the tested
0016 ICD hash at startup; later driver candidates are not part of this result.

The candidate first extraction is 2.065 times the same-boundary accepted
67.138187 s first extraction (106.49% longer). Reject 0021 for performance,
retain the accepted local128 tile, and keep `RADISH_NCNN_TILED32=0` by default.
No warm repeat was performed or is warranted by this result. The accepted
66.919494 s warm remains a separate baseline, not the denominator for this
first-extraction comparison. No resource or accuracy gate was relaxed.

### Sequential tiled submissions: accepted checkpoint 0022

Cumulative Mesa patch `0022-radish-tiled-batching-cumulative.patch` replaces
0016 at the same pinned base. Its only driver behavior change is an opt-in
`RADISH_TILED_BATCH_CHUNKS=2|4` gate for the measured A506 tiled resource class:
local16x8x1, barrier, four loops, 16 KiB shared, no private allocation, single
thread size, at most 16 full registers/no half registers, four descriptors and
instruction length 36 or 37. Driver default is one chunk. Each hardware EXEC
still contains one workgroup; each complete restore/program/descriptor/base,
EXEC, WFI and cache-flush/wait sequence is retained. The existing submission
loop groups two or four sequential sequences, including trailing batches.
Other shaders and existing short-shader batching are unchanged.

The gate identifies a resource class, not a shader identity or a general
watchdog guarantee. The pinned ncnn selector bounds K at 3584 for this workload.
Per-chunk private timestamps do not reset the kernel hangcheck fence; no wait,
flush, register limit or fault check was removed. Independent review proves the
11-line insertion is the only change to `rd_queue.c`; all 14 patch files
reconstruct exactly. Build log: `build-tiled-submit-0022.log`. Patch SHA256:
`0c03e76309ae835fa32946a61626614136dc5ea173419383258904933ec7514a`.
ICD SHA256: `828094bb27e30a0d97c41caa8714073b500ebcd0de40201426c520ded58293e4`.

Focused dilation 1 passes all 25984 outputs at batch 1/2/4, with unchanged max
error 3.42226122e-5. Math command times are 11.472062/9.396640/8.675388 ms;
eight chunks produce 8/4/2 submits. Dilation3/5 at batch 4 pass all 23424/20864
values, errors 7.09755113e-6/1.641165e-5, in6.921529/6.628924 ms; six chunks
produce two submits, exercising a trailing partial batch. All runs have no new
faults and both services active. Logs: `operators-tiled-submit-0022-d*-batch*.log`.

The fixed full-core `core-instancenorm-tiled-submit-0022-repeat2.log` records
9.884 s load, **56.853055 s first / 56.013717 s warm** (one warm observation),
including input/output transfer. Both compare all 562342 outputs successfully:
max 0.000129699707, relative RMS 7.72155564e-7, zero mismatches. Output SHA256
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4` matches
0019 exactly, so its full127800-sample waveform evidence remains applicable.
Service wall 2 min 3.193 s, peak 428.5 MiB, zero swap, no new faults; both services
remain active. Warm core latency falls16.30% from 66.919494 s, but remains slower
than the packed six-thread CPU mean 32.079044 s (two warm observations).

The independently mapped warm window preserves all 1990 command signatures
except the53 tiled submit counts; all 1093 layer paths/groups/returns match.
Those 53 commands fall39.290606493→28.279931903 s and 25944→6486 submits, with
25944 chunks unchanged. Other 1937 commands take 23.822304502→23.819569629 s,
with 30523 submits and 96 small transfers unchanged. These are command wall
times including host work/submission/waits, not isolated GPU instruction times.
Evidence: `warm-tiled-submit-result-0022-ie_nqmik/` and
`warm-tiled-submit-review-0022-r4v8wqm4/`.

Fresh T78 runtime with explicit batch 4 also passes all 205942 core values and
46800 waveform samples against ORT references from its exact tablet boundary.
Frontend 4.553038 s, metadata 0.031497 s, load 9.530217 s, core 20.403266 s,
tail 0.038096 s, synthesis 34.863887 s (through publication34.868013 s); service
38.856 s, peak 341.8 MiB/zero swap, no faults, services active. Waveform SNR is
96.917732 dB. Core/waveform hashes remain respectively
`7b91c412aa771e91c1976233fddde9d138610b8f56dd8157829110f0adba670c` and
`1baae4fc1a45bb276bff219d3b9b9c7af8352e5d889798482e6fcfd4114c7353`.
Logs/report: `runtime-single-short-tiled-submit-0022-first.log` and
`runtime-single-short-tiled-submit-0022-validation.json`. The runtime wrapper
now defaults batch 4 for this pinned workload; model/operator wrappers retain
explicit opt-in and driver default remains1. TILED32 remains disabled.
Full-length fresh InstanceNorm-only synthesis has not been rerun with 0022;
the later full-length Snake run below uses the same driver checkpoint.

### Guarded Snake fusion: full core and fresh T78/T213 runtime pass

The project-local `tests/ncnn/radish_snake.{h,cpp}` fuses 48 exact five-operation
chains into `RadishSnake` layers. The strict rewrite preserves each original
alpha and independently stored reciprocal tensor, weighted-row order, output
blob identity and the live input alias. It removes 192 arithmetic layers and
dead Split outputs, producing **906 layers / 1067 blobs** from the accepted
InstanceNorm graph's 1098/1307. The 213042160-byte weights are unchanged at
SHA256 `0f77cf77ff79a7724662f2e79a4d50284eb498abb2e519469a188e33f6c051ae`;
the new parameter SHA256 is
`e01c56a4f23d6d3c8f92dbecb0c3fc37f6f74e9f305f58b82d5196278a450d38`.
Metadata propagation recognizes the exact new inventory and derives Snake
shapes from its three inputs; it does not execute tensors to obtain hints.

Each Net registers a context before loading parameters; that context outlives
its layers and is destroyed before the Vulkan device. The layer has no weights
or in-place writes. Its FP32 pack4 shader requires the exact Radish device,
supported options, rank2 C128/C256 shapes, valid buffer bounds and a fresh
output allocation. It evaluates `x + stored_reciprocal * square(sin(alpha*x))`
with the original operand order. CPU execution delegates to the original five
BinaryOp/UnaryOp layers. The default graph remains InstanceNorm; `--snake`
explicitly selects the separately validated private fixture.

`snake-host-full-cpu-first.log` passes all **562342 outputs**, max error
6.65187836e-5, relative RMS 3.38237918e-7, zero mismatches at unchanged 0.002
absolute/relative gates. Its **2.504136 s is host CPU time, not tablet
performance**. `snake-host-cpu-first.log` and `snake-device-cpu-first.log`
also pass C128/W33 and C256/W35 in pack1 and pack4: all 4224/8960 outputs are
bit-identical to the original delegated chain, max independent-reference error
1.28855663e-7, and all inputs are preserved. The tablet probe has no new faults
and both services remain active.

The first actual compile, `snake-compile-first.log`, stopped at the strict push
layout guard before execution. ncnn reflects push members only for the struct
name `parameter`; renaming `Parameters` accordingly retained the two-member /
eight-byte guard without changing arithmetic. The corrected
`snake-compile-push-layout.log` passes compile-only before any upload or command
submission. The live gate checks the actual ncnn-generated SPIR-V for three
OpFMul and one OpFAdd, all NoContraction. Raw generated SPIR-V was not saved;
the evidence is the live gate plus actual NIR/native output, separately from
the earlier standalone shader inspection.

The final NIR's outer `fmad` is explicitly **unfused**, preserving intermediate
product rounding; it is distinct from `ffma`. NIR's late rewrite requires
contraction permission only for the latter. Two internal sine range-reduction
MADs also occur in the original UnaryOp path. Their presence is not evidence of
a NoContraction violation, and no driver change was needed. Actual dataflow and
resource audit: `snake-actual-compile-audit-btxi8gc0/REPORT.md`.

Both compiled probe variants use local32x1x1, four SSBOs, eight push bytes,
8 full/1 half registers, 106 instructions/102 dwords, no loops/shared memory
and no scratch/private allocation or spill instructions. Diagnostics confirm
one GLSL/SPIR-V compile (729 words) per context across both cases. The log still
contains two identical native programs; this does not prove native pipeline
cache reuse.

`snake-device-first.log` passes all **13184 GPU outputs** across C128/W33 and
C256/W35, with zero bit mismatches against the original five GPU operations,
zero numeric mismatches and max error 4.51237448e-7 at unchanged 2e-5
absolute/relative gates. Signed zero, small/subnormal values, independent stored
coefficients and odd tails are covered; all three inputs remain unchanged.
Fused dispatch groups are 64/128. No new faults, both services active.

The separate `core-snake-first-repeat2.log` now records **51.936026 s first /
51.188057 s warm**, with 9.365 s cold load and one warm observation. Both
extractions pass all 562342 outputs at unchanged gates, max 0.000129699707,
relative RMS 7.72155564e-7, zero mismatches. Output SHA256
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4` matches
accepted 0022 exactly. Its existing full 127800-sample waveform evidence applies
to these identical core bytes; no new waveform run is claimed. Service wall
1 min 52.994 s, peak 416.8 MiB/zero swap, no new faults and both services active.
Warm latency is **8.615% lower** than 56.013717 s; this is saved-boundary core
evidence, not fresh synthesis latency.

The complete warm mapping replaces exactly 240 original arithmetic commands
with 48 Snake commands: their wall time falls 6.654548391→2.459730388 s,
submits 11328→3648 and chunks 45216→14592. All 1750 retained commands match
resource/grid/chunk/submit signatures by layer, normalized boundary or transfer
inventory; no helper or transfer commands are added. Transfers remain 96,
totalling 73728 bytes. Whole-window counts fall 1990→1798 commands,
37009→29329 submits and 144245→113621 chunks. The trace now has 898 GPU and
three CPU layer calls, with all 853 retained non-Snake layer paths/groups
unchanged. Driver hash and controls match 0022.

The 4.825660 s warm reduction comprises 4.194818003 s less Snake command wall,
0.491149607 s retained-command timing variation and 0.139692390 s less residual
time. These timers include host work, submission/waits and GPU execution;
the residual is not pure CPU time and this decomposition is not an isolated
instruction-cost model. Evidence:
`warm-snake-result-mrqc_tau/comparison-0022-snake.json` and `snake-warm-map.json`.

The model/runtime modules and explicit `--snake` wrapper selection are reviewed;
the wrapper pins the candidate graph and freezes its host validation marker and
assets before/after execution. The transformer emits only a provisional graph;
the wrapper additionally requires `snake-validation.json`, a complete host CPU
pass log/output and matching graph/input/reference hashes. The read-only fixture
validator rechecks all values. Proof inputs and binary checkpoint pins are
documented in [RUNTIME_INTEGRATION.md](RUNTIME_INTEGRATION.md); transformation
alone does not make a fixture runnable through `--snake`.

Fresh T78 `--snake` runtime now passes all
205942 core values and 46800 waveform samples against original decoder/tail
references from its exact tablet boundary. Frontend 4.424653 s, metadata
0.026029 s for 1067 blobs, load 9.398850 s, extraction 19.507355 s, tail
0.037999 s, synthesis **33.764051 s**, through publication 33.767987 s and
service 33.852 s. Peak 383.3 MiB/zero swap, no faults, both services active.
Core/waveform hashes equal the original T78 GPU results, respectively
`7b91c412aa771e91c1976233fddde9d138610b8f56dd8157829110f0adba670c` and
`1baae4fc1a45bb276bff219d3b9b9c7af8352e5d889798482e6fcfd4114c7353`;
max errors 1.05142593e-4/1.47223473e-5, waveform SNR 96.917732 dB, zero
mismatches. All eight boundary files match the reference inputs exactly.
Verified immutable frontend/source hardlink reuse succeeds. Evidence:
`runtime-single-short-snake-first.log` and matching `-validation.json`.

Fresh T213 `--snake` also passes all **562342 core values / 127800 waveform
samples** in `runtime-single-full-snake-first-validation.json`. Frontend
5.772051 s, metadata 0.026095 s, load 9.314241 s, extraction 52.428375 s,
tail 0.103515 s, synthesis **68.158274 s**, through publication 68.178252 s and
service 68.279 s. Peak memory is **445.9 MiB plus 22.9 MiB swap**; keep this
distinct from the zero-swap fixed-core and T78 runs. No faults, both services
active. Core max error 1.02996826e-4/RMS 7.81135939e-7; waveform max error
9.19587910e-6/SNR 98.355163 dB; zero mismatches at unchanged gates.
Core/waveform SHA256s are
`7a5dbb9410a91e30bdfb9db12426b653d21b09786310c8c0cc623618efa174bb` and
`4d2ff1622bb83ad0ff02a603c6f2974fbd19685a099e45b5f0cd13a2928b7e8d`, identical
to the earlier full GPU runtime. All eight boundary files also match both the
earlier CPU and GPU captures exactly, recorded in
`runtime-single-full-snake-cpu-gpu-boundary-identity.json`. The earlier same-input
six-thread CPU synthesis remains faster at 39.789900 s; this is not a new CPU
Snake measurement or a sustained comparison.

The accepted 0022 InstanceNorm graph remains the default and Snake requires
explicit `--snake`. Resident reuse, cancellation, sustained performance and
Vulkan conformance status are unchanged.

### Accepted checkpoint 0023: eight tiled chunks per submit

The bounded candidate adds only value 8 to the existing 0022 tiled-batch
condition, under the same local128/resource gate. It preserves single-group
hardware EXECs, restore/descriptor/base setup, every per-chunk wait/flush,
register/private-memory guards and submission lifetimes. The pinned ncnn K<=3584
bound is unchanged; the resource gate alone does not prove arbitrary shader K
or watchdog safety. Driver/operator/model defaults remain 1; runtime default
changed from 4 to 8 only after the fresh full-runtime validation below.
The three wrappers accept explicit
1/2/4/8 and reject 16. Previous published checkpoint `f78917a` remains preserved.

The targeted ICD build passes; candidate driver SHA256:
`9e8274ca701163e442237e78c7ff965a4de745c97bfb16cfa54a563655f23ff2`.
The cumulative patch applies to the pinned Mesa base and reconstructs all
14 changed files exactly. Patch SHA256:
`5516b6823e67f70c989939a95e16d853fd00af43d3a280b13cefc4b60da40bfd`;
proof: `tiled-batch8-0023-reconstruction-qinlp2gg/reconstruction.json`.
`operators-tiled-batch8-0023-d{1,3,5}.log` passes all 25984/23424/20864 outputs
with unchanged max errors 3.42226122e-5/7.09755113e-6/1.641165e-5. Math command
wall times are 8.485493/6.473352/6.448820 ms; 8/6/6 chunks each fit one submit,
including trailing partial batches. No faults; both services remain active.
This is bounded operator evidence, not a general watchdog guarantee.

`core-snake-batch8-0023-repeat2.log` now passes both full extractions:
**49.266772 s first / 49.429406 s warm**, cold load 9.315 s, with one warm
observation. All 562342 outputs pass unchanged gates, max 0.000129699707,
relative RMS 7.72155564e-7, zero mismatches. Output SHA256
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4` matches the
Snake batch4 and earlier accepted core exactly; existing full-waveform evidence
applies without claiming a new tail run. Peak 419 MiB/zero swap, no faults,
both services active. Warm latency is **3.4357% lower** than 51.188057 s.
Accept the fixed-core checkpoint. The complete warm comparison preserves all
1798 resource/grid/chunk signatures and 901 layer paths; only the 53 tiled
submit counts change, 6486→3255, each rounded up from its chunk count at batch8. Total submits
fall 29329→26098 with work/chunk counts unchanged. Tiled command wall falls
28.093623943→25.551525088 s, saving 2.542098855 s; the other 1745 commands rise
19.319909979→20.030949589 s, with unchanged work and submits. Residual wall
increases 0.072408245 s, yielding the measured 1.758651 s extraction reduction.
These are wall-inclusive timers, one warm sample each; no individual batch's
maximum execution time was measured. Evidence:
`warm-snake-batch8-review-0023-aocwevkg/REPORT.md`, `comparison.json` and per-layer CSVs.

Earlier 33.764051/68.158274 s runtime results use batch4. No general watchdog or
sustained-performance claim follows from these bounded observations.

The first fresh batch8 attempt stopped before device writes/inference at disk
preflight: 217568 KiB available versus the unchanged 262144 KiB minimum.
Under the shared lock with no active runtime, byte-identical immutable frontend
and source graphs in two earlier private runtime directories were deduplicated
through verified hardlinks. Paths, content, modes, owners and prior evidence
were preserved; both services stayed active. This freed 218664 KiB, leaving
436232 KiB available, without lowering the reserve. Evidence:
`device-runtime-immutable-graph-dedup-0023.log`. The retry uses the new label
`snake-batch8-0023-retry` and log `runtime-single-full-snake-batch8-0023-retry.log`;
no inference result is claimed for the failed preflight.

The fresh T213 retry now passes all **562342 core values / 127800 waveform
samples** in `runtime-single-full-snake-batch8-0023-validation.json`. Frontend
5.705182 s, metadata 0.026177 s, load 9.426758 s, core 47.854719 s, core scope
57.630407 s, tail 0.103455 s, synthesis **63.528396 s**, publication 63.535909 s,
service 63.611 s. Peak **458 MiB/zero swap**, no faults, both services active.
Core max error 1.02996826e-4/RMS 7.81135939e-7; waveform max error
9.19587910e-6/SNR 98.355163 dB; zero mismatches. Core/waveform bytes match the
batch4 fresh result exactly, and all eight raw boundary files match both batch4
and the earlier CPU capture. Evidence:
`runtime-single-full-batch8-cpu-gpu-boundary-identity.json`.

The observed synthesis reduction from 68.158274 s is 4.629878 s, but the prior
run used 22.9 MiB swap and this run uses none; these separate single observations
do not isolate a batch8 effect. Following full validation, only the private
runtime wrapper's tiled-batch default changes from 4 to 8. Driver/operator/model
defaults stay 1, 16 remains rejected, and Snake still requires `--snake`.

### Candidates 0024/0025: channel-major padded Conv input

Incremental ncnn patch `0024-ncnn-conv1d-planar-input.patch` adds explicit
`RADISH_NCNN_PLANAR=1`, default 0. It requires the existing exact-device FP32
tiled64 path, excludes TILED32, SAME and no-padding cases, and accepts explicit
nonnegative padding with positive total and bitwise +0 fill. All 53 currently
tiled model layers satisfy that gate. Unsupported static cases retain their
original path; selected invalid shapes/options/buffers fail before commands.

One pack4-to-planar padding dispatch replaces the existing padding dispatch;
it initializes the complete physical halo in an independent rank2 pack1
workspace `(LP,Cin)`. Logical input/bordered/output widths remain separate from
the physical pitch. Checked integer products, allocator/device/buffer bounds
and nonoverlap protect inputs, weights, bias and output. The local8x4 prepass
writes one spatial vec4 per scalar channel; math keeps local16x8, original
weights/bias, scalar K order, barriers and pack4 output. `--padded-tiled-only`
adds a focused existing-width213/C128/K11 padded fixture; old no-pad fixtures
keep their original behavior. Compile-only still returns before upload/submit.
Canonical, SIMPLEOMP and integrated runtime builds pass in
`build-planar-0024-first.log`, `build-simpleomp-planar-0024-first.log` and
`build-runtime-planar-0024-first.log`. Integrated padded compile-only passes in
`operators-planar-0024-compile-first.log` before upload/submission: math remains
16 full/0 half/instrlen31; the local8x4 pad shader uses 8 full/3 half registers,
no loops and instrlen9. No new faults; both services remain active.

Actual C128/K11/d1 compilation in `channel-major-compile-first.log` changes the
binding1 input gather from four scalar load sites to one four-component load.
Instructions fall 950→788 and allocated dwords 1152→992; register use remains
16 full/0 half, four loops and 16 KiB shared. The complete all20 run,
`channel-major-compile-all20-first.log`, covers the exact 20 specialization
tuples used by all 53 layers: each has local16x8, native instrlen31, 16 full/0
half, four loops, one binding1 vec4 load and preserved barriers/accumulation.
No scratch/LDP/STP is observed, and accepted pipelines pass the driver's private
memory rejection. These helpers compile pipelines without tensor execution.
Private audits: `channel-major-compiler-34cx94fy/actual-compile-audit/REPORT.md`
and `channel-major-compiler-34cx94fy/all20-actual-audit/REPORT.md`. Fewer
instructions alone do not establish a speedup.

The padded width213/C128/K11 baseline and planar dilation1/3/5 runs each compare
all **27264 outputs** against the independent reference at unchanged gates.
Candidate max errors are 3.42226122e-5/7.09755113e-6/1.641165e-5, matching the
corresponding existing errors. No faults, services active. These padded cases
retain output width213, unlike the historical unpadded203/183/163 fixtures.

| Padded operator | Pad command | Math command |
| --- | ---: | ---: |
| Original pack4, dilation1 | 2.946675 ms | 8.272940 ms |
| PLANAR, dilation1 | 2.126673 ms | 7.977001 ms |
| PLANAR, dilation3 | 1.939901 ms | 7.875804 ms |
| PLANAR, dilation5 | 2.465007 ms | 7.922783 ms |

Each math command has eight chunks in one batch8 submit. Candidate pad commands
use 5/5/6 chunks/submits; the baseline pad uses eight. Logs:
`operators-planar-0024-d1-baseline.log` and `operators-planar-0024-d{1,3,5}-first.log`.
These are single operator observations; no equivalent padded dilation3/5
baseline timing or full-model speedup is inferred from this table.

The separate cumulative Mesa patch `0025-radish-planar-batching-cumulative.patch`
adds only native instrlen31 beside 36/37 in the existing tiled-batch gate.
All other resource conditions, K<=3584 in the ncnn selector, chunk waits/flushes
and defaults remain unchanged. Without it the new math shader misses the
batch8 gate. Instrlen31 also uses the driver's existing <=32 instruction-preload
path; that is another execution difference, not a demonstrated benefit.
The targeted ICD build passes, SHA256
`22a34efba7622363225c14600366fe05488fbc38bd452248e43717d40d239227`.

0024 SHA256 `23b9bafe92a6ad41ae393bde9d50576d7e2e6a258036234411e198f0f7c7cce8`
reconstructs all seven tracked Conv files in the ordered stack;
`planar-0024-reconstruction-4oq6y46v/reconstruction.json` records the match.
0025 SHA256 `fd0a0bb37e562535488406e183c46a233edf616939ad416efb6b09070bc0c17b`
reconstructs the pinned Mesa base separately, with proof in
`planar-0025-reconstruction-xcjenuma/reconstruction.json`.
The full fixed-boundary model passes in `core-snake-planar-0024-repeat2.log`:
9.030 s cold load, 48.738814 s first extraction and 48.863090 s warm (one warm
sample), 420.1 MiB/zero swap. All 562342 values pass unchanged gates, max error
0.000129699707 and RMS 7.72155564e-7; services remain active with no new GPU faults.
The pulled 2249368-byte output is identical to accepted 0023, SHA256
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4`.
Existing fixed-output waveform evidence therefore applies to these exact bytes;
this run did not repeat the waveform stage or exercise a fresh frontend boundary.

The warm comparison improves 49.429406→48.863090 s, a modest 0.566316 s/1.1457%.
The 53 tiled math commands improve 25.551525088→24.107147904 s, while their
53 padding commands worsen 0.993677872→1.933040488 s and increase submissions
1654→3696. The targeted net saving is 0.505014568 s. All 1692 remaining command
signatures and all 901 layer paths match; transfers remain 96/73728 bytes, with
no added clone/helper commands. Whole-profile submissions rise 26098→28140.
The private report `warm-planar-result-0024-mdia5brz/REPORT.md` and comparison
artifacts preserve exact ownership, geometry and per-layer attribution. These
are wall-inclusive command measurements and one warm sample per mode, not an
isolated shader-cost or sustained-performance result. PLANAR stays off; preserve
0023's accepted 49.429406 s baseline. Fresh-runtime validation below uses the
later0026 local8x8 prepass, not this historical local8x4 version.

### Candidate 0026: planar padding local8x8

Ordered patch `0026-ncnn-planar-pad-local64.patch` follows 0024 and changes only
the prepass local size8x4→8x8, its two pipeline guards and checked group-count
formula. The math shader, padding body, layouts and PLANAR default0 are unchanged.
This targets the measured 0024 padding penalty above. For the same 53 layers,
geometry initially projected padding submits3696→1848 (1848 fewer); actual
execution confirms that count below. The reviewed four host changes reconstruct all seven
Conv files (`planar-pad64-reconstruction-uoqs78zq/reconstruction.json`). Patch
SHA256 `3bbc418cf86b74ef32c1789b4bdc88c2b88ba4f1368c19ab1d62ee9103996a5c`.
Canonical/SIMPLEOMP builds pass and the runtime target links. Compile-only
`operators-planar-pad64-0026-compile.log` passes before upload/submit: pad
local8x8, 8 full/3 half, 228 instructions (125 NOP), 288 dwords/instrlen9,
zero loops and accepted zero private storage; math remains 788 instructions
(432 NOP), 992 dwords, 16 full/0 half/four loops. Focused dilation1 in
`operators-planar-pad64-0026-d1.log` passes all 27264 outputs with unchanged
max error3.42226122e-5, 144 padding groups plus8 math groups, pad1.770161 ms/math
7.917470 ms. No faults, services active. Full-model repeat2 passes in
`core-snake-planar-pad64-0026-repeat2.log`: cold8.848 s, first48.370236 s and
warm46.932926 s (one warm), 418.4 MiB/zero swap/no faults, services active.
All562342 outputs pass unchanged gates (max0.000129699707/RMS7.72155564e-7).
The output SHA256 is again
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4`, so prior
fixed-output waveform evidence applies by exact byte identity, without a new
tail run. Warm latency is1.930164 s/3.9501% below0024 local8x4, and
2.496480 s/5.0506% below accepted0023. These are single warm observations.
Actual padding submits fall3696→1848 and padding command wall falls
1.933040488→1.093877034 s (saving0.839163454 s); math is nearly unchanged at
24.107147904→24.073201017 s (saving0.033946887 s). All1692 retained signatures
and901 layer paths match, but retained command time also falls0.794779154 s;
the entire1.930164 s improvement cannot be assigned to the local-size change.
Against0023, the target math+padding net saves1.378124909 s of the2.496480 s
whole-wall reduction. Reproducible comparisons are in
`warm-planar-pad64-result-0026-ooc8sgac/{vs0024,vs0023}/comparison.json`.
Fresh integrated runtime also passes exact-boundary ORT validation:

| Capture | Frontend s | Core load s | Core s | Synthesis s | Peak MiB / swap |
| --- | ---: | ---: | ---: | ---: | --- |
| T78 | 4.163551 | 9.191366 | 18.148241 | 31.890578 | 329.1 / 0 |
| T213 | 5.827658 | 9.038404 | 47.887015 | 63.320730 | 429.7 / 0 |

All205942/562342 core values and46800/127800 waveform samples pass unchanged
gates; waveform SNR is96.917732/98.355163 dB. Both runs have no faults and active
services. Logs and independent validation are
`runtime-single-{short,full}-planar-pad64-0026-first.log` and
`runtime-single-{short,full}-planar-pad64-0026-validation.json`. The full run's
eight raw boundary files match earlier CPU/0023 captures, and core/waveform bytes
match prior GPU output (`runtime-full-planar-pad64-0026-boundary-identity.json`).
Full synthesis63.320730 s is close to0023's63.528396 s; this is not a broad startup
latency win. PLANAR stays off; preserve earlier checkpoints and single-observation
limits. Sustained/resident execution and cancellation remain open.

### Candidate 0027: two tiled X groups per EXEC

`0027-radish-tiled-x2-cumulative.patch` adds explicit
`RADISH_TILED_GROUPS_X=2`, default1. The gate requires A506, local16x8x1,
barrier/four loops, 16 KiB shared, no private storage, single threadsize,
at most16 full/no half registers, four descriptors, native instrlen31 and
gridX>=2. It is a resource fingerprint, not shader identity. Only X groups pair;
Y/Z remain single, odd X tails execute once, and each submit is capped at four
EXEC chunks. Existing1/2 chunk requests stay1/2; tiled-batch8 becomes at most4
paired chunks. Per-EXEC WFI/flush, fence waits and fault checks are unchanged.

Targeted driver build and all14-file reconstruction pass
(`tiled-x2-0027-reconstruction-i5z4h0kg/reconstruction.json`). Patch SHA256
`adc9ae8506e91abbacea3674a416b1e907672fecf60eb7c62dbe7c319367f6a1`;
ICD `36666edbfb9759d6e53b5389103f7ccee2e907444b85fa630aca968c8d8af0f8`.
Padded dilation1/3/5 with groups2/batch1 pass all27264 outputs each, expected four
math chunks/submits, unchanged numeric gates and no faults/services active.
Dilation5 math is9.147232 ms; dilation1/batch4 also passes at7.991756 ms with
four math chunks in one submit. Logs: `operators-planar-x2-0027-first.log`,
`operators-planar-x2-0027-d{3,5}-batch1.log` and
`operators-planar-x2-0027-d1-batch4.log`.

Full repeat2 passes in `core-snake-planar-x2-0027-repeat2.log`: cold9.022 s,
first45.621586/warm47.975705 s, all562342 outputs at unchanged gates
(max0.000129699707/RMS7.72155564e-7), no faults/services active. Output SHA256
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4` matches the
prior fixed core exactly; existing waveform evidence applies without a new tail
run. Peak417.4 MiB plus25.3 MiB swap, versus0026's zero swap. Warm latency
is1.042779 s/2.2218% above0026's46.932926 s; this single comparison is confounded
by swap. The53 tiled math commands show no measured win:24.073201017→24.095025469 s
(+0.021824452 s), despite chunks25944→13024. Their submissions remain essentially
unchanged3255→3256 because paired EXEC batches are capped at4; the extra submit
comes from the C512 odd-X case. All1745 retained
command signatures match; their wall time grows19.118974956→19.942358953 s
(+0.823383997 s), including0.120398485 s more padding. Most of the whole-wall
increase is outside the changed math, so this is not an isolated X2 cost result.
The faster first extraction is also mainly retained timing variation: math saves
only0.087728326 s while the other1745 commands save2.656098631 s. First/warm
maps and source-backed ownership are retained in
`warm-planar-x2-result-0027-nf2vcmi7/REPORT.md`.
Keep0026/groups1 as the performance recommendation;0027 remains an opt-in narrow
correctness capability. No fresh-runtime repeat is required for this non-winning
candidate, and no performance acceptance/default change is claimed.

### Candidate 0028: fused InstanceNorm variance-first reduction

`0028-ncnn-instancenorm-sum4.patch` adds default-off
`RADISH_NCNN_NORM_SUM4=1` for exact Radish FP32 pack4, rank3/height1/batch1,
known C128/256/512/1024, affine0/epsilon1e-5 and matching checked shape/range.
It fuses subtraction/square with the first sum4 variance reduction, removing
one full-width workspace and dispatch. Mean computation, later variance tree,
coefficients and final normalization remain unchanged; unsupported cases retain
the original path. The local16x1x4 shader preserves subtraction→square and
left-associated sums with guarded tails and NoContraction. Source review,
private syntax/SPIR-V checks and probe/wrapper integration review pass; these
are not actual IR3 or tensor-execution evidence.

The initial integrated public bundle was SHA159de90a…a1fd5; initial0028 SHA256 was
`97b28ffe25c693db4e3f57398556a593d6c92adccb075ff24c5f6d4a46995bea`.
Canonical/SIMPLEOMP builds pass (`build-norm-sum4-0028.log` for canonical).
Initial actual compile-only fails the expected fused-selection assertion before
VkCompute/upload (`operators-norm-sum4-0028-compile.log`), with no faults. The
static gate incorrectly expected logical pack1 hints; ncnn `net.cpp:1951–1977`
already supplies packed Vulkan top_shapes. The correction requires
pack4/16-byte elements and shape.c=channels/4, preserving all other guards.
Revised0028 SHA256 is
`4aa4d0983dd680492a35db3993bce02030e6700e3add4bc22369f03239a3af33`, with all three
files reconstructed exactly in `norm-sum4-0028-reconstruction-cpwcmz6a/reconstruction.json`.
Both corrected builds pass. Failed evidence remains archived under
`norm-sum4-0028-integration/failed-hint-evidence/`.
`--norm-sum4-only` checks C128/W33..36 plus pack1
fallback C3/W213, original/fused selection and exact dispatch count, preserving
full independent numeric/residual checks. Compile-only returns before upload;
optional output publishes36606 values only after all five cases pass. Wrappers
validate0|1/default0 and preserve X2/provenance; runtime appends norm at argv31.

Corrected actual compile-only passes all five cases in
`operators-norm-sum4-0028-compile-packed-hint.log`: packed hints C32/pack4 select
the fused pipeline for W33..36, while C3 stays original. Four new compiled
programs each use179 repeat-expanded instructions (85 NOP/94 non-NOP),198 padded dwords,
instrlen6,
8 full/1 half registers, zero loops, local16x1x4 and accepted zero private
storage. Actual NIR uses precise fsub/fmul and unfused fmad, whose separate
rounding is specified in `nir_opcodes.py:1138`; native mad.f32 alone does not
establish contraction, as in the earlier Snake audit.

Both actual original/fused probe runs pass all independent numeric/residual
checks (`operators-norm-sum4-0028-{original,fused}.log`). All36606 dumped values
are byte-identical, SHA256
`78e80ff3b454f44f7fbb4e15818e5ba0c4ee29ba89a9ccb147f38f10809c54ff`, with evidence in
`norm-sum4-0028-integration/focused-identity.json`. No faults, services active.
The runtime build passes; frozen binaries and hashes are preserved in
`norm-sum4-0028-integration/bin/` and `binary-checkpoint.json`. Full repeat2 passes
in `core-snake-planar-norm-sum4-0028-repeat2.log`, with PLANAR1/NORM_SUM4=1,
X groups1/generic batch4/tiled batch8: cold9.694 s, first47.302535/warm46.983200 s,
all562342 outputs at unchanged gates (max0.000129699707/RMS7.72155564e-7),
418.5 MiB/zero swap/no faults/services active. Pulled output SHA256 is
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4`, identical to
0026, so prior fixed-waveform evidence applies without a new tail run. Warm wall
is essentially flat versus46.932926 s (+0.050274 s/0.1071%, one warm each).
Actual attribution replaces49 square commands (1.778057033 s) and49 first
variance reductions (0.368136867 s) with49 fused first reductions (0.513706515 s),
saving1.632487385 s in targeted work. All1700 remaining command signatures and
901 layer paths match, but retained command timing adds1.547037747 s. This
explains the nearly flat whole wall without assigning variation to isolated GPU
cost. Evidence: `norm0028-compare-prepared-l4fxn44o/comparison.json`.
The earlier focused-instrlen7 report was a counting error, not a different model
program length: `ir3.c:396` computes ceil(95 encoded instructions/16)=6 before
adding four trailing NOPs, giving198 padded dwords. Actual focused and model
execution both report instrlen6. `ir3_compiler.c:359` supplies alignment16;
`ir3.c:498–517` explains the printed repeat-expanded179 count. Frozen native
excerpts, source/log pins and independent regeneration of the full profile are
in `norm0028-result-audit-zm_1noi9/REPORT.md` and `audit.json`. The padded allocation
size must not be used to infer the instruction-length field. This comparison
uses the later0027 driver with X2 disabled, not the same driver binary as0026;
retained signatures independently confirm unchanged observed scheduling.

Fresh T78/T213 runtimes both pass exact-boundary ORT validation:

| Capture | Core s | Synthesis s | Peak MiB / swap MiB |
| --- | ---: | ---: | ---: |
| T78 | 17.777928 | 31.783290 | 328.6 / 0 |
| T213 | 46.628144 | 65.988211 | 436.9 / 46.9 |

All205942/562342 core values and46800/127800 waveform samples pass, waveform
SNR96.917732/98.355163 dB; no faults/services active. Evidence:
`runtime-single-{short,full}-norm-sum4-0028-{first.log,validation.json}`. The full
run's eight inputs match prior CPU/0026 boundaries, and core/waveform bytes match
0026 (`runtime-full-norm-sum4-0028-boundary-identity.json`). There is no established
overall latency win; T213 uses swap unlike0026's zero-swap run. NORM_SUM4 stays0,
and0026/groups1 remains the performance recommendation.

Before this candidate, immutable runtime assets were deduplicated under the
shared device lock:14 byte-identical path replacements with matched hashes,
byte comparisons, modes and owners; all paths remain present. This recovered
219876 KiB, leaving393784 KiB free; services stayed active. Evidence:
`dedup-immutable-runtime-assets-pre-norm-{plan,summary}.json` and the corresponding
`.log`. This housekeeping supplies no inference or performance evidence.

### Candidate 0029: generic short-dispatch batch8 and submit timing

Cumulative `0029-radish-short-batch8-cumulative.patch` adds8 to existing generic
batch1..4 only for shaders without loops/barriers and instrlen<=32. The64-group
hardware budget, X2 behavior, tiled batching and per-EXEC WFI/flush/wait/fault
checks are unchanged. Driver/grid/compute/model defaults remain1, runtime4.
Wrappers accept exactly1/2/3/4/8; existing controls/provenance remain intact.
Profile-only `submit_ns`/`submit_max_ns` measure submit-through-fence/fault-check
wall intervals, excluding cleanup; profile-off adds no clocks.

Targeted build/all14-file reconstruction and16 grid cases pass, no faults/services
active (`grid-short-batch8-0029-first.log`). Patch SHA256
`4d46236f83dfd943080241d05822479aaae6b2fa70e5fa8584b9a557f509995c`, ICD
`c7ec53be69dbe249112c3e6aeae01532175390fbcf4718e2da1ff54cb4badad5`; reconstruction
is in `short-batch8-0029-integration-bot0hl5l/checkpoint.json`.

The paired runs use the same frozen0026 model and0029 driver, norm0/X1/tiled8:

| Generic batch | Cold s | First s | Warm s | Peak MiB / swap MiB |
| --- | ---: | ---: | ---: | ---: |
| 4 | 8.991 | 47.715981 | 47.413913 | 419.7 / 7.7 |
| 8 | 8.998 | 45.766135 | 45.339510 | 417.5 / 0 |

All562342 values pass unchanged gates, no faults/services active. Saved outputs
are byte-identical SHA0550fe07…f18e4, retaining prior fixed-waveform evidence
without a new tail run. Warm improves2.074403 s/4.3751%, one warm each; baseline
swap differs from candidate. All1798 command signatures/901 paths and114393
hardware chunks match. Only669 command submit counts change;1467 qualify for the
gate, and total submits26292→15398. The669 changed commands save2.072993161 s;
other1129 command timing varies by−0.029432263 s. Exact ownership/count/output
and submit-timing invariants pass; report:
`short-batch8-result-0029-p9a9QvGK/REPORT.md`.

Eligible observed submit maxima are34.062768→33.911360 ms first and
5.011371→6.193822 ms warm. These include CPU scheduling and waits, not GPU-only
time or a watchdog guarantee; much larger whole-window maxima belong to
ineligible loop1 Conv math.

Fresh runtimes with norm0/batch8/X1 both pass exact-boundary ORT references:

| Capture | Core load s | Core s | Synthesis s | Peak MiB / swap MiB |
| --- | ---: | ---: | ---: | ---: |
| T78 | 10.769532 | 17.289251 | 32.622528 | 330.7 / 0 |
| T213 | 9.301445 | 45.667104 | 61.429138 | 431.3 / 16.7 |

All205942/562342 core values and46800/127800 waveform samples pass; waveform
SNR96.917732/98.355163 dB. Core/waveform bytes match0026 exactly; eight-boundary
identity reports are `runtime-{short,full}-short-batch8-0029-boundary-identity.json`.
Logs and reference results are `runtime-single-{short,full}-short-batch8-0029-first.log`
and `runtime-single-{short,full}-short-batch8-0029-validation.json`; no faults and
services active. T213 total is lower than0026's63.320730 s but has16.7 MiB swap
versus zero. T78 total regresses from31.890578 s despite core18.148241→17.289251 s:
load9.191366→10.769532 s masks that gain. Keep single-run limits and this short
regression explicit. Recommend batch8 opt-in for the measured fixed path;
norm0/X1 and all defaults remain unchanged.
Before that run, four verified immutable hardlink replacements preserved files
and hashes while free space increased271396→329336 KiB
(`dedup-immutable-runtime-assets-pre-0029.log`).

### Candidate 0030: positive planar integer indices

Ordered `0030-ncnn-planar-positive-indices.patch` follows0026 and changes only
six integer expressions in the PLANAR math shader. Nonnegative k/kernel_w uses
unsigned division; K/4, k0/4, i/8, i/16 and co/4 use equivalent shifts. Existing
host gates bound positive K (multiple32, <=3584), channels and buffer indices;
independent review confirms no floating-point, memory-layout, tail or barrier
change within that domain. PLANAR remains default0.

Actual compile-only `planar-index-compile-0030-first.log` passes C128/K11/d1
with no tensors/compute objects, no faults/services active. Baseline ISA matches
the prior accepted shader. Candidate431 encoded instructions through END yield
instrlen27 (four padding NOPs/870 allocated dwords); repeat-expanded count is717
versus788. Both retain16 full/0 half, four loops,16 KiB shared and accepted
zero private storage. The47-slot inner K body,32 MADs, accumulator registers,
global/shared access widths and barrier pairs are unchanged. Subsequent all20
compiler and focused tensor evidence follows below.
Frozen audit: `planar-index-compiler-38ya7qvg/actual-compile/REPORT.md`.

Candidate source SHA256 is
`d130779e24fa84875adcf63ceb964337a22f402df5779b8eb0ba962b9740aa64`;
0030 patch SHA256 `d706738bce01ab6fc5d2dc45776c843c3fa18e7c40157c4d9fe1627b94c4c65f`.
Canonical and SIMPLEOMP builds pass. Equal tiled-batch1 d1 baseline/candidate
checks pass all27,264 outputs at unchanged3.42226122e-5 max error; math wall
10.858472/10.782274 ms is a small single-pair difference, not a speed claim.
Candidate d3/d5 also pass all27,264 values at7.09755113e-6/1.641165e-5 max
error, with no faults/services active (`operators-planar-index-0030-*batch1.log`).

Actual `planar-index-compile-0030-all20.log` passes all20 specializations covering
53 layers: six native27 and14 native28, all16 full/0 half, four loops,16 KiB,
local16x8, four SSBOs, single-thread and accepted zero private storage. Counts
come from encoded instructions through END, excluding metadata/labels/trailing
padding. Independent frozen proof:
`planar-index-compiler-38ya7qvg/independent-all20-resource-check.json`.

Cumulative0031 adds only27/28 to the existing tiled-batch31/36/37 guard; all
resource checks, chunks, packet sequence, synchronization/fault checks and
defaults remain intact. X2 still requires31. Build/all14-file reconstruction
pass (`planar-index-0031-integration-1fgdngh5/checkpoint.json`); patch SHA256
`24090b6d37daf2533f6f5557e1c634cbd362603721d3fdf4b6852587b918be96`, ICD
`05ea3444c6b1e38a524ffe59133dd6097634412ffc0a25356b2fbf14b312365e`.
Focused batch8 passes eight math chunks/one submit at7.711066 ms, no faults.
Full original-Snake repeat2 passes all562,342 values with unchanged max/RMS
errors, no faults/services active: cold9.015 s, first45.312337 s, warm44.956105 s
(one warm),400.6 MiB plus30.6 MiB swap (`core-snake-planar-index-0031-repeat2.log`).
Saved full output is byte-identical SHA0550fe07…f18e4. Compared with0029 warm
45.339510 s, the0.383405 s/0.8456% difference is modest and confounded by swap
and one pair. Exactly53 math commands change only native length31→27/28:
24.162254532→23.788746683 s saves0.373507849 s. All1745 retained signatures/
901 paths and15398 submits/114393 chunks match; retained time adds0.025433019 s,
residual falls0.035330170 s. Every integer submit-timing invariant passes.
Frozen first/warm attribution: `planar-index-result-0031-cp_rznjt/REPORT.md`.
No fresh runtime or default change is claimed.

### AffineSnake: preserve style arithmetic while removing intermediate passes

The separate opt-in `RadishAffineSnake` graph fuses48 style-affine/Snake chains:
714 layers/875 blobs, same226 weighted rows in original order and unchanged
weights. Inputs retain normalized x, original gamma/beta, alpha and stored
reciprocal. CPU delegates the original eight operations; GPU uses an FP32
pack4/local32 kernel with precise operations, six SSBOs/eight push bytes, no
weights or inplace writes. Original Snake and InstanceNorm graphs/defaults remain
available. Parameter SHA256:
`dbd9fbfe67e6053382f1e1734b6480c3a42f0bc59e57b85627fb16a6e9783d5a`.

Independent source/metadata/registration/wrapper review and isolated host CPU
focused/full checks pass. Actual compile `affine-snake-compile-first.log`
programs1/10 share native d24a3ad0…106dbd:8 full/1 half, zero loops/shared/private,
53 encoded instructions through END→length4,128 allocated dwords;117 expanded
instructions/48 NOPs. One context compile produces931 SPIR-V words. Final NIR
keeps gamma+1 exact fadd, style/final residual exact unfused fmad, alpha multiply
and sine square exact fmul. Two further fmad operations are internal sine range
reduction; there is no final ffma. Native MAD does not imply contraction.
Frozen audit: `affine-snake-actual-compile-bir0qvav/REPORT.md`.
Focused GPU C128/W33 andC256/W35 pass all4224+8960 values with zero bit/numeric
mismatches against the original chain, full independent2e-5 gates and all five
inputs preserved, max3.04405713e-7 (`affine-snake-gpu-first.log`); no faults.

Full fixed `core-affine-snake-index-0031-repeat2.log` passes all562,342 outputs
with unchanged max/RMS, exact historical SHA0550fe07…f18e4, services active/no
faults: cold8.880 s, first43.525012 s, warm43.124072 s,416.0 MiB plus7.4 MiB swap.
Against same-model/driver original Snake44.956105 s,48 chains replace192→48
commands and save1.822263171 s (4.043802893→2.221539722); total warm saves1.832033 s/
4.0752%. All1606 retained per-owner signatures/661 paths match; global recursive
traversal changes because fused inputs visit x before gamma/beta. Transfers
remain96 with identical inventory. Whole submits15398→13718, chunks114393→101289.
Retained time adds0.013458526 s; residual falls0.023228355 s. One warm each and
swap30.6→7.4 MiB limit broad claims. Exact fixed output bytes allow prior fixed
waveform evidence to apply; fresh integrated-runtime evidence follows below.
Attribution: `affine-snake-result-0031-nb319sa9/REPORT.md`.

Fixture reproduction requires CPU evidence. `fuse-kokoro-affine-snake.py` alone
creates a provisional graph. `prepare-kokoro-affine-snake-fixture.py SOURCE
EVIDENCE RUNNER NEW_OUTPUT` imports completed same-runner/same-thread CPU logs,
reference-gated outputs and exact original/Affine byte identity, regenerates the
pinned graph, validates all assets, and publishes exclusively. Its evidence
contains `results.json` plus `original/` and `affine/`; RUNNER must hash-match
that checkpoint. `validate-kokoro-affine-snake-fixture.py` checks the resulting
marker/assets/proof before device use and afterward. Fixed wrapper kind is
`core-shapes-affine-snake`; runtime requires explicit `--affine-snake` and rejects
wrong714/875 selection before frontend. Default graph/controls are unchanged.

Fresh explicit `--affine-snake` integrated runs pass full same-boundary ORT
core/waveform gates, with no host work between frontend/core/tail:

| Input | Synthesis | Core | Load | Peak/swap |
|---|---:|---:|---:|---:|
| T78 |32.105121 s|16.577948 s|10.965538 s|330.3 MiB/0 |
| T213 |59.184846 s|43.655035 s|8.887679 s|415.6 MiB/74.2 MiB |

All205,942/562,342 core values and46,800/127,800 waveform samples pass unchanged
thresholds, with zero mismatches and exact0026 core/wave bytes; waveform SNR is
96.917731811/98.355162707 dB. Reports:
`runtime-single-{short,full}-affine-index-0031-validation.json`; captured inputs
are used exactly for the references, without recomputing frontend/source. Both
runs leave services active and no new faults. Fresh T78 core improves versus
0029 (17.289251→16.577948 s), but load10.965538 s keeps total32.105121 s above
0026's31.890578 s. Fresh T21359.184846 s is a lower single observation than
0029's61.429138 s, with much greater swap74.2 versus16.7 MiB; this is not a
sustained or isolated speedup proof. Warm residency/reuse and cancellation remain
open; no live-service integration is claimed.

Before these runs,16 older private executable paths were archived with verified
host backup hashes and a restore manifest, freeing186956→367704 KiB. Weights,
captures, outputs and current0026/norm/batch8 runtime directories were retained.
Evidence: `device-runtime-executable-archive-before-affine/`; no reserve reduction.

### Private deconvolution route: all neural-core layers on GPU

A separate private912-layer/1073-blob graph converts exactly three1D
Deconvolutions to existing height-one2D Vulkan operators, adding six reshapes
while preserving all weights/weighted-row order. It starts from original Snake,
not AffineSnake. Full stream/weight slices, metadata, bounded runner and actual
23-program compiler audits pass; math kernels need no new driver guard, shared
memory or barriers. This first private result preceded the explicit selectors integrated below.

The first fixed extraction passes all562,342 values at44.561071 s versus the
original-Snake first45.312337 s; no warm measurement. Cold9.697 s/service54.645 s,
350.1 MiB plus66.2 MiB swap, services active/no new faults. All907 non-Input
layers execute on GPU, eliminating exactly the three CPU deconvolution fallbacks.
The original898 remaining paths/1792 ordered command signatures match. Six
boundary conversions disappear (0.124015 s), while twelve GPU math/crop/reshape
commands cost3.103440 s; two dense output reshapes alias without dispatch.
Submits15398→16390 and chunks114393→115927 increase. Residual3.889209→0.277082 s
falls3.612127 s but includes more than CPU fallback time. The0.751266 s first-only
improvement is one pair with differing swap; not a sustained performance claim.

Output changes numerically but passes unchanged0.002 absolute/relative gates:
max0.000110626221/RMS7.58608718e-7, SHA
`d11801ba74bedbf4772b446b2d703bbfdf1d4f901a7fb7f730ed3a75f997240f`.
A new saved-output host CPU inverse-STFT gate passes all127,800 samples,
max1.17148933e-5/SNR98.0811949451 dB. This is the fixed saved boundary with a host
tail, not fresh on-device synthesis. Largest observed submit wall is299.498362 ms
in dense GEMM; with66.2 MiB swap this is not an isolated GPU/watchdog bound.
Logs: `deconv2d-core-first.log`, `deconv2d-first-host-waveform.log`; published
private evidence: `evidence/deconv2d-private-first/result.json`. Exact attribution
and allocation-cost details: `deconv2d-result-first-_b1qx777/REPORT.md`.


### Integrated AffineSnake with GPU deconvolutions

Both exact graph variants now have structural rewriters, completed-CPU-evidence
importers and validators, metadata shape propagation, and explicit model/runtime
selectors. Snake/deconv is 912 layers/1073 blobs; AffineSnake/deconv is 720/881.
The latter preserves all 226 weighted rows and produces the same CPU output as
the independently validated Snake/deconv graph. The existing graph defaults,
FP32 tolerances, driver and ncnn shaders remain unchanged. Source preparation
and invocation are documented in `RUNTIME_INTEGRATION.md`.

The combined graph passes first/warm extractions in 43.051203/42.469886 s,
with 9.346 s cold loading, 402.1 MiB peak, zero swap and no GPU faults. Each
extraction traces all 715 non-Input layers through Vulkan. All 562,342 values
pass unchanged gates; output SHA is d11801ba74bedbf4772b446b2d703bbfdf1d4f901a7fb7f730ed3a75f997240f,
identical to the validated Snake/deconv output and therefore the saved host
waveform input. Warm time improves from 43.124072 s in the preceding AffineSnake
sample, but remains above the CPU mean of 32.079044 s. There is one warm sample
for each GPU checkpoint; sustained/resident behavior remains open.

The runtime build passes. Frozen runtime SHA is
`ab4ed3163e2e6b1a1773e61dafc1fc83a72ed30894a787c4242858dddc6ca587`;
model 7a78db6e and driver 05ea3444 remain unchanged. Evidence under WORK:
`deconv2d-integrated-0031-checkpoint/binaries.json`,
`build-runtime-deconv2d-0031.log`, `core-affine-deconv2d-0031-repeat2.log`,
`affine-deconv2d-fixed-validation.json`, and the completed same-runner CPU proof
in `affine-deconv2d-private-3db5592j/host-validation/`.

### Rejected two-step planar reduction loop

The private K2 candidate advances the inner reduction loop by two, preserving
the original accumulator order. All 20 actual compiler variants pass at 16 full
registers, zero half registers, 16 KiB shared memory and native length 31. The
same uploaded inputs and weights produce bit-identical full operator outputs
for dilation 1, 3 and 5, passing the unchanged independent double reference.
Nevertheless, affected math-command times regress from 7.744450/7.772575/
7.847836 ms to 8.275129/8.337265/8.554088 ms. Each uses eight chunks and one
submission. These are single paired observations, consistently 7–9% slower;
there is no full-model K2 run and no canonical shader change.

Evidence: `planar-k2-compile-all20.log`,
`k2-operator-execute-d{1,3,5}-first.log`, and
`planar-k2-operator-q5tg7bik/REPORT.md`. The smaller dynamic instruction count
does not establish faster execution; additional moves/stalls and larger native
code remain possible explanations, not measured causal attribution.

Before the new runtime runs, nine byte-identical immutable library/executable
paths were deduplicated without changing their contents or names. Two old norm
ICD paths were archived with verified host backups and restore instructions;
weights and captures were retained. After the full run, three additional
identical ICD paths were deduplicated. Logs and manifests are
`dedup-runtime-assets-before-deconv*`,
`device-runtime-executable-archive-norm-driver-before-deconv/`, and
`dedup-runtime-assets-after-deconv-full*`. No memory or disk reserve was reduced.

The integrated all-GPU core also passes fresh tablet synthesis at both lengths.
T213 takes 42.943278 s core / 63.130064 s total, with 459.3 MiB peak and 38.1 MiB
swap; T78 takes 15.873927 s core / 30.156883 s total, with 385.0 MiB peak and zero
swap. All 715 executed core layers use Vulkan in each run. Every core value and
waveform sample passes the unchanged reference gates, with waveform SNR
98.377722/97.073984 dB for full/short input. All eight frontend boundary files
match the preceding AffineSnake captures exactly; output values change slightly
because of GPU deconvolution accumulation. No faults occur and services stay
active. Full startup regresses from 59.184846 s: frontend time rises from
5.935016 to 10.326545 s despite a faster GPU core. Short synthesis improves from
32.105121 to 30.156883 s, including a lower 9.564430 s core load. These remain
single startup observations with varying memory pressure, not sustained results.

Evidence under WORK: `runtime-single-{full,short}-affine-deconv2d-0031-first.log`,
matching `runtime-single-{full,short}-affine-deconv2d-0031-validation.json`, and
`runtime-affine-deconv2d-0031-summary.json`. Captures are
`kokoro-runtime-{full,short}-single-vulkan-affine-deconv2d-0031-first-affine-snake-deconv2d-planar/`.

### Rejected private K2 inner-loop unroll

The factor-two inner-K candidate is not integrated. All20 compiled variants
fit16full/0half,16KiB shared,local128,4loops and native length31 (through END,
not trailing padding); existing batching covers them. Focused padded K11/C128
runs for dilation1/3/5 pass all27,264 output pairs bit-identically, unchanged
2e-5 reference gates and input preservation, with no faults/zero swap. Math
command wall nevertheless rises7.744450→8.275129 ms,7.772575→8.337265 ms and
7.847836→8.554088 ms (+6.85%,+7.27%,+9.00%). All have the same8 chunks/1 submit
under batch8/X1. These are three single bounded A/B observations, not pure GPU
timestamps. K2 adds17 wholeprogram MOV sites and increases native length27/28→31;
static instruction/stall counts do not isolate the cause. Keep the accepted
single-K kernel; no full-model K2 run. Exact attribution and frozen log hashes:
`planar-k2-operator-q5tg7bik/EXECUTION-REJECTION.md` and `execution-rejection.json`;
logs `k2-operator-execute-d{1,3,5}-first.log`.


## Combined graph with norm sum4; core indirect dispatch 0032

The existing opt-in 0028 normalization path on AffineSnake/deconv reduces fixed
warm extraction from 42.469886 s to 40.723725 s (4.11% in this single pair).
First extraction is 41.036178 s and cold load 9.683 s; peak memory 385.1 MiB,
swap 71.7 MiB. Both 562342-value extractions pass unchanged reference gates,
all 715 non-Input layers execute on Vulkan, and output SHA-256 remains
`d11801ba74bedbf4772b446b2d703bbfdf1d4f901a7fb7f730ed3a75f997240f`.
No new GPU fault occurred and both live services remained active. Warm core
remains 8.644681 s (26.95%) slower than the 32.079044 s CPU mean. This result
uses driver 0031 and `RADISH_NCNN_NORM_SUM4=1`; the preceding fresh synthesis
measurements use norm 0. No fresh norm-1 or sustained result is claimed.
Evidence: `core-affine-deconv2d-norm-0031-repeat2.log` and
`affine-deconv2d-norm-fixed-validation.json` under WORK.

Cumulative Mesa patch 0032 implements `vkCmdDispatchIndirect`. Recording retains
pipeline, push constants, descriptors, argument buffer and offset. Queue execution
waits for preceding work and reads the three argument words from the buffer;
it does not capture argument values while recording. A local dispatch copy keeps
recorded state unchanged. The existing bounded direct executor handles the resolved
grid, preserving chunk limits, waits, cache operations and fault reporting.
The argument read is synchronous on the CPU; no native GPU-indirect packet or
performance improvement is claimed. Public `vkCmdDispatchBase` remains absent,
and API advertisement, optional features and conformance status are unchanged.

Build and all 14-file reconstruction checks pass. Patch SHA-256:
`923c05c1fc31b6d690e5d93af4c1d3ced3cb48262004ace3ff8ab8d4ff347eef`.
ICD SHA-256:
`c8fee96723942fca768a9026987a5d609071d2c60079058064377635155cb44c`.
The base remains `ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230`.
Frozen source comparison, reconstruction and binaries are under
`indirect-dispatch-0032/`; build log is `build-indirect-0032.log`.

`RADISH_BATCH_CHUNKS=8 radish/scripts/device-compute.sh --indirect` passes
36 dispatches across index/vector-add, boundary sizes and a zero grid. One
iteration writes arguments on the host after command recording; the other uses
`vkCmdUpdateBuffer` and a transfer-to-indirect barrier. A nonzero buffer binding
offset and nonzero argument offset are exercised. Full outputs and guard regions
pass. Shader-written arguments, 3D indirect grids and command-buffer replay have
not been tested. The 16-case direct-grid regression passes all builtins, partial
bounds, zero dimensions and guards. Both runs report no new GPU faults and both
live services active. Logs: `compute-indirect-0032-first.log` and
`grid-direct-indirect-0032-first.log`. Optional `RADISH_COMPUTE_RUN` and
`RADISH_GRID_RUN` labels preserve separate evidence for later invocations.
The private device ICD `/var/tmp/radish/libvulkan_radish.so` now contains 0032;
immutable 0031 performance captures remain unchanged. No system Mesa or service
configuration changed, and no device CTS case ran.


## Fresh full synthesis with combined graph and norm sum4

The frozen 0031 ICD and integrated runtime pass fresh T213 synthesis with
`RADISH_NCNN_NORM_SUM4=1`: frontend 5.889140 s, metadata 0.022015 s, core load
9.424234 s, core extraction 41.411426 s, complete core scope 51.209836 s,
waveform tail 0.103896 s, total synthesis 57.293048 s. Service wall is 57.373 s;
peak memory 418.9 MiB and swap 12.6 MiB. All 715 non-Input layer traces use GPU,
zero CPU fallbacks, no new faults, and both appliance services remain active.

Every one of the 562342 core values and 127800 waveform samples passes the
unchanged independent reference gates. Waveform SNR is 98.377722 dB. All eight
boundary inputs and complete core/waveform outputs are byte-identical to the
preceding norm-disabled full capture. Compared with that 63.130064 s run, core
extraction saves 1.531852 s while frontend startup independently saves 4.437405 s;
memory/swap also vary. This is one startup comparison, not isolated attribution
or sustained behavior. CPU full synthesis remains faster at 39.789900 s.
The earlier 30.156883 s short capture still uses norm disabled.

Logs and references under WORK: `runtime-single-full-affine-deconv2d-norm-0031-first.log`,
`runtime-single-full-affine-deconv2d-norm-0031-{reference,validation}.log`,
`runtime-single-full-affine-deconv2d-norm-0031-validation.json`, and
`runtime-affine-deconv2d-norm-0031-summary.json`. The exact capture directory is
`kokoro-runtime-full-single-vulkan-affine-deconv2d-norm-0031-first-affine-snake-deconv2d-planar-norm-sum4/`.
Before this run, three identical immutable runtime/library paths were hardlinked
to recover disk space, preserving every path and byte. Inventory and checks are
in `dedup-runtime-assets-before-norm-full*`; no reserve or memory cap changed.


## Bounded resident Kokoro worker

`--request-loop --next-input DIR ...` adds two to four complete requests to the
existing runtime. The positional input is request zero; each appended directory
supplies fresh ids/style/speed. Each request runs frontend/source, exact metadata,
one new Extractor and the waveform tail. Only one core remains resident, keyed
by exact inferred parameter text including actual T. A shape change destroys the
old Net/custom contexts/device before loading its replacement. One-shot defaults
and `--repeat N` retain their original meaning.

GPU request objects die after completed download and before blob/staging allocator
budgets are cleared. Persistent weights and pipelines survive a cache hit. This
releases request-owned workspace; Freedreno BO caches and libc/ORT may retain
physical pages. CPU loop mode disables the Net's local activation pool. Timing
includes phase memory sampling/progress-manifest I/O; extraction includes the
reported workspace trim. Request latency excludes capture publication and final
session teardown; the session duration includes both.

The first tablet T78/T78 session passes at 29.764615/20.055962 s synthesis,
15.524156/15.492647 s core, and 9.717544/0 s core load. Peak 429.2 MiB / 532 KiB
swap. The T78/T213/T213 session passes at 29.680679/57.301229/46.984077 s total,
15.909938/41.569559/40.930395 s core, and 9.455682/9.330206/0 s load. Cache
pattern is miss/miss/hit, with one model at a time; peak 517 MiB / 19.1 MiB swap.
All five requests execute 715 GPU / zero CPU layer paths. Every boundary file,
core output and waveform matches the corresponding prior validated capture.
Original ORT comparisons pass all 205942 or 562342 core values and 46800 or
127800 waveform samples; waveform SNR is 97.073984 dB short / 98.377722 dB full.
Both sessions report no faults and both live services active. Full resident
latency still exceeds the historical 39.789900 s fresh CPU baseline; the later
same-graph resident CPU measurement is recorded below. These bounded sessions are not sustained
thermal evidence or live application integration.

Per-request atomic output preserves the existing v1 boundary/core/waveform/runtime
layout under request-000/, request-001/, etc. Atomic session.json records terminal
complete/cancelled/failed status, counts, cache decisions, timings and phase memory.
Optional `--cancel-file /absolute/path` is polled between completed phases. It
suppresses unpublished output and drains work before cleanup; it cannot promptly
interrupt an active GPU extraction. Host preexisting/mid-core cancellation and
subsequent one-shot repeat2 checks pass. The later tablet phase-boundary probe is
recorded below; this initial evidence was host-only.

Canonical integration and build pass (`build-runtime-resident-first.log`) and
reproduce the tested binary exactly:
`0541e68cbb9e2f04862ff6cf36c73a70036118ec2f6d415716bf658b0270bda9`.
Runtime source SHA `9bcdf73f7a1501923864956ee41d0a542710481246657033097d4315a95f2523`;
helper SHA `1e970df28e997cd5d67e2c573074067511592aa448399544f2a6f0564fafc220`.
Measured driver remains 0031 (`05ea3444c6b1e38a524ffe59133dd6097634412ffc0a25356b2fbf14b312365e`),
combined graph e211a573...fc807d, FP32/PLANAR1/NORM_SUM4=1/batch8/tiled8/X1.
The later 0032 indirect API change is separate from these measurements.

Evidence under WORK: `resident-runtime-private-9heh18vd/{source-checks.json,cross-build/build.json,host/summary.json}`,
`resident-runtime-independent-9ofxqpn8/REPORT.md`,
`resident-first-validation-ovkogiqt/{REPORT.md,summary.json}`,
`resident-shape-validation-c5w7tg1v/{REPORT.md,summary.json}`,
`resident-loop-first-request-{000,001}-validation.json`, and
`resident-loop-shape-first-request-{000,001,002}-validation.json`.
Captures: `kokoro-resident-loop-first` and `kokoro-resident-loop-shape-first`;
guarded execution evidence under `evidence/resident-loop-{first,shape-first}/`.
The private reviewed harness in `resident-device-loop-wqyh11ze/` used the shared
lock, 640 MiB memory / 128 MiB swap / 300 s runtime, 15 s stop timeout and existing
RAM/disk reserves. Failures drain only their own unique transient unit. Its
source pins describe the pre-integration checkout; those historical runs are
complete. The later public wrapper integration and driver 0032 measurement are
recorded below; these original five requests used the private harness.

Before the first resident run, three byte-identical immutable runtime/library
paths were hardlinked without changing their bytes or names. Inventory and
checks: `dedup-runtime-assets-before-resident-first*`. No system library, kernel,
service, memory limit or reserve changed.

The accumulator-orientation shader experiment is rejected before tensor execution:
actual A506 hot loops retain 47 encoded slots, 32 scalar MADs, three vector4 shared
loads and no repeat MADs. Both programs use 16 full / zero half registers, four
loops and native length 27. The proposed instruction reduction did not occur.
Exact report: `planar-accumulator-private-cavc_tnj/actual-first/REPORT.md`;
compile log `planar-accumulator-compile-first.log`, no faults. Canonical shader
remains unchanged. Historical X2 targeted math was also essentially flat, so
extending X2 admission is deferred. The next private candidate targets five
nonaligned-input-channel convolutions; measured math exposure is 2.736140 s,
not a predicted saving.

## Public resident staging with driver 0032

`device-runtime.sh` now accepts `--request-loop` with one to three
`--next-input runtime-short|runtime-full` arguments, for two to four requests
including the positional fixture. Loop mode requires explicit
`--affine-snake-deconv2d` and `--repeat 1`; existing one-shot options/defaults are
unchanged. The measured command was:

```sh
RADISH_NCNN_PLANAR=1 RADISH_NCNN_NORM_SUM4=1 RADISH_BATCH_CHUNKS=8 \
  radish/scripts/device-runtime.sh runtime-short public-resident-0032-first \
  --affine-snake-deconv2d --vulkan --request-loop --next-input runtime-short \
  --threads 4 --ort-threads 4 --max-frames 512 --repeat 1
```

Use a new run label when reproducing it. The wrapper retains the shared device
lock, 640 MiB memory / 128 MiB swap / 300 s runtime, 15 s stop timeout and
existing RAM/disk reserves. On failure it drains only its unique unit while
holding the lock. `kokoro-runtime-capture.py` freezes actual runtime/driver/ICD,
graph/input and observed source hashes, checks host and remote assets plus the
installed ORT library before/after, and verifies every pulled output hash.
Completed `request-NNN/` directories retain v1 `device-runtime.json` and
`boundary/device-capture.json`; `device-session.json` records the ordered complete
session with accuracy pending. Original-ORT references and full accuracy checks
still run separately for each exact captured boundary after synthesis.

Both T78 requests pass all 205942 core values and 46800 waveform samples:
max core error 0.000104188919, relative RMS 9.032957511e-7, waveform max
1.387670636e-5/SNR97.073984074 dB, zero mismatches. All eight raw boundaries,
core and waveform bytes match each other, the prior validated short capture and
the earlier resident session. Each has 715 GPU / zero CPU layers; all 1430
ordered trace rows match that prior session. Fresh/resident synthesis is
29.521695246/20.089174476 s, core 15.717958607/15.750590681 s and
load 9.475816580/0 s. The 9.432520770 s reduction mainly avoids loading the Net;
the extraction itself is slightly slower. One warm observation is not sustained
performance or an equivalent resident CPU comparison. Later CPU and tablet
cancellation evidence follows below; live application integration remains untested.

`session.json` records 49.928546931 s including final cleanup 0.254041777 s;
the service reports 50.038 s, 432.1 MiB peak, zero swap, no new GPU faults and
both services active. Runtime SHA remains
`0541e68cbb9e2f04862ff6cf36c73a70036118ec2f6d415716bf658b0270bda9`;
actual 0032 ICD SHA is
`c8fee96723942fca768a9026987a5d609071d2c60079058064377635155cb44c`.
Wrapper/helper review and offline historical-capture checks passed before the
device run: `public-resident-wrapper-eduzj417/`, patch SHA
`18575e76bbae0da5cf1ff1e9aa954fabd6bc8ea2e19aa45cc36a5a213a075f2b`.

Evidence: `resident-public-0032-validation-mcnh4qu7/{REPORT.md,summary.json}` and
`public-resident-0032-request-{000,001}-validation.json`. Capture:
`kokoro-runtime-short-single-vulkan-public-resident-0032-first-affine-snake-deconv2d-planar-norm-sum4-loop/`.
The complete wrapper log and hashes use the corresponding
`evidence/runtime-runtime-short-vulkan-public-resident-0032-first-affine-snake-deconv2d-planar-norm-sum4-loop*` prefix.

Before this run, two hardlinked private 0026 ICD paths were archived to one
hash-verified host backup, recovering 45384 KiB (free 234880→280264 KiB).
The old shader/ICD bytes and original path/mode/owner records remain in
`device-runtime-executable-archive-pad64-before-public-resident/manifest.json`;
log: `archive-pad64-driver-before-public-resident.log`. Model weights, captures,
outputs and live services were retained; no memory cap or reserve changed.

Subsequent staging housekeeping preserved three identical immutable runtime/library
paths via verified hardlinks (hashes, modes and owners unchanged), increasing
free space 218852→242020 KiB: `dedup-runtime-assets-before-resident-cpu*`.
Exactly two older 0029 private ICD paths were then archived to a verified host
backup with original path/mode/owner records, increasing free space to 287408 KiB:
`device-runtime-executable-archive-batch8-before-resident-cpu/manifest.json` and
`archive-batch8-driver-before-resident-cpu.log`. No model, capture or output was
removed; both services remained active. These are disk-maintenance records,
not resident CPU benchmark evidence.

## Same-graph resident CPU baseline

The public wrapper also passes a packed six-thread CPU short/short/full/full
session with ORT4, the same combined graph and runtime binary. The measured
command was (choose a new run label for another execution):

```sh
RADISH_NCNN_PLANAR=1 RADISH_NCNN_NORM_SUM4=1 RADISH_BATCH_CHUNKS=8 \
  radish/scripts/device-runtime.sh runtime-short resident-cpu-0032-first \
  --affine-snake-deconv2d --cpu-packed --request-loop \
  --next-input runtime-short --next-input runtime-full --next-input runtime-full \
  --threads 6 --ort-threads 4 --max-frames 512 --repeat 1
```

| Request | T | Core cache | Core seconds | Synthesis seconds |
| --- | ---: | --- | ---: | ---: |
| 0 | 78 | miss | 11.325414444 | 16.998107487 |
| 1 | 78 | hit | 11.072058253 | 15.411270994 |
| 2 | 213 | miss | 30.849667695 | 38.496352537 |
| 3 | 213 | hit | 30.703000662 | 36.802922440 |

Each request executes all 715 CPU / zero GPU layers; all 2860 successful ordered
trace rows have the same layer inventory. Cache hits report zero core load.
Every request passes the unchanged original-ORT decoder/tail gates from its exact
saved boundary, with zero mismatches. T78 checks 205942 core values / 46800 samples:
core max 0.000358581543, relative RMS 2.541598061e-6, waveform max 2.557039261e-5 /
SNR 92.417480395 dB. T213 checks 562342 core values / 127800 samples:
core max 0.000347137451, relative RMS 2.504017408e-6, waveform max 5.042552948e-5 /
SNR 88.221183364 dB. All eight boundary files match the corresponding validated
GPU captures exactly. Same-T CPU boundaries/core/waveform are byte-identical;
CPU and GPU outputs are independently checked, not asserted byte-identical.

CPU resident short 15.411270994 s is 4.677903482 s below GPU 20.089174476 s; full
36.802922440 s is 10.181154123 s below GPU 46.984076563 s. CPU remains faster in
these observations. The compared graph, runtime binary, ORT thread count and
boundary bytes match; CPU uses six inference threads and GPU four host threads.
CPU ran short/short/full/full, short GPU ran short/short with driver 0032, and
full GPU ran short/full/full with driver 0031. There is one resident observation
per length/backend with different session histories and memory pressure, not
controlled sustained evidence. The later tablet cancellation probe follows below;
live application work remains open.

Session 108.114303525 s including final cleanup 0.002831208 s; service 112.503 s
wall / 430.462 s CPU time, 404.8 MiB peak and zero swap. No new GPU fault occurred
and both live services remained active. Runtime SHA 0541e68c...70bda9 and combined
graph SHA e211a573...fc807d are unchanged; the CPU path makes no GPU dispatches.
Evidence: `resident-cpu-0032-validation-ch3gouod/{REPORT.md,summary.json}`,
`resident-cpu-0032-request-{000,001,002,003}-validation.json`, and
`runtime-resident-cpu-0032-first.log`. Capture:
`kokoro-runtime-short-single-cpu-packed-resident-cpu-0032-first-affine-snake-deconv2d-planar-norm-sum4-loop/`.

## Tablet phase-boundary cancellation

A private guarded probe reuses the frozen public 0032 driver/runtime assets
(ICD c8fee967...55cb44c, runtime 0541e68c...70bda9) and requests two T78 syntheses.
After request 0 publishes, the harness observes request 1 at `after_core_load`
with at least one completed GPU dispatch in its profile, then creates the regular
`--cancel-file` marker. The active core finishes before the runtime observes
cancellation at `after_core`. Exit code 2 and terminal `status=cancelled` record
one completed of two requested inputs, active index 1, with no request 1 directory
or temporary active output. No completed-session marker or v1 device provenance
was fabricated for this cancelled diagnostic run.

Request 0's eight raw boundary files, core and waveform are byte-identical to the
independently validated public short capture. The remote output inventory/hashes
match the pulled files; all reused asset and installed ORT hashes match before
and after. This preserves the earlier complete-output accuracy evidence by byte
identity; it does not claim a newly generated original-ORT reference run.

Host-observed marker-call return to process exit is 15.793738505 s, including
host polling/transport effects; final core cleanup is 0.263654331 s. This is
phase-boundary cancellation, not prompt interruption of an active GPU extraction
or an upper response-time bound. Session 51.683906729 s, service 51.789 s,
485.7 MiB peak /zero swap, no new faults, both live services active. The probe
uses 640 MiB / 128 MiB swap / 120 s runtime and 15 s stop timeout. No new tablet
request was executed after cancellation; recovery/restart and broader phase
coverage remain open.

Evidence under WORK: `resident-cancel-0032-private/{run.py,launch.json,marker-observed-session.json,marker.json,run.log,validation.json,output/}`,
its before/after asset and remote-output hash records, and
`resident-cancel-0032-first.log`. Independent read-only rechecks of terminal
state, retained bytes, asset/output hashes and fault log pass:
`resident-cancel-independent-kw7685ve/audit.json`. The public staging wrapper
still accepts completed sessions only and does not expose `--cancel-file`.

## Padded-channel convolution 0033

Incremental ncnn patch `0033-ncnn-planar-padded-channels.patch` adds default-off
`RADISH_NCNN_PAD_CHANNELS=1`. It requires the existing exact-Radish FP32 tiled
and planar path, TILED32 off, K3/stride1/dilation1, explicit pad1 on both sides,
positive zero padding, bias and no activation. Only 514→1024, 1090→1024 and
1090→512 channel tuples qualify: five graph layers, alongside the existing 53
tiled layers. Channels are padded to 544/1120; graph and stored model weights
remain unchanged, with 1,658,880 additional persistent GPU weight bytes.
Other configurations retain the original route. Physical rank2 input alignment
is checked separately from logical row stride; the prepass initializes every
halo, rounded spatial tail and added channel. Accepted planar math is unchanged.

All three focused classes pass their complete 218112-value independent double
references and workspace/weight/input guards. Observed pad+math seconds are
0.302640→0.076974 (514→1024/W213), 0.628814→0.149949 (1090→1024/W213) and
0.570616→0.134817 (1090→512/W426). These single operator pairs exclude common
download/reference work. All report zero swap, no faults and active services.
Integrated builds and actual full load-only compiler audit pass: 23 tiled
specializations, 16 full/0 half registers, 16 KiB shared, four loops and native
length 27/28; the new pad uses local8x8, 8 full/3 half, native8 and no loops/shared.
Accepted pipelines reject private allocation. Driver 0032 and its gates are
unchanged; load-only setup may submit transfers, so it is not a no-submit proof.

Fixed core: cold 9.007 s, first 38.788999 s and warm 38.556751 s, 405.9 MiB/zero swap,
service 86.954 s, no faults/services active. Both full 562342-output checks and
715 GPU/zero CPU traces pass. Independent saved warm validation gives max
0.000110626221, relative RMS 7.47965004e-7 and zero mismatches at unchanged
0.002 absolute/relative tolerances. Output SHA is
`e128ce6329b06837670fbea51f1fa9510659865d7719a52b696ebd2f68d4d287`.
It differs in 502869 values from the earlier norm-enabled core, maximum delta
6.86049461e-5; both pass the original reference. Do not inherit earlier waveform
byte-identity evidence for this result.

Exact profile mapping preserves 1601 command signatures and replaces five
padding/math pairs. Their warm wall time is 2.773070719→0.682642419 s, saving
2.090428300 s; retained commands are 37.748006386→37.690152513 s. Whole warm
40.723725→38.556751 s is 5.32% lower in this pair. Total commands remain 1611,
submits 13194→13179 and chunks 90695→90715. The intended five pairs account for
most observed savings, while the prior 71.7 MiB swap, different model binary
and driver 0031→0032 prevent an isolated whole-wall claim.

The public full/full/short/short runtime loop independently passes all four
original-ORT decoder/waveform references from exact captured boundaries. It
uses 715 ordered GPU/zero CPU paths per request and cache miss/hit/miss/hit.
Fresh full synthesis 54.928111002 s/core 39.689168303 s becomes resident
45.500714193 s/core 39.181154535 s. Short after shape reload takes28.964790620 s/
core 14.378352951 s, then resident18.767524719 s/core 14.370964223 s. Load is
9.071568673/0/9.328510884/0 s. Full waveform SNR is 97.912821255 dB; short is
97.411696248 dB. All eight boundary files match validated CPU and GPU captures;
new same-T outputs match each other but differ from previous GPU output bytes.
Session 149.175802788 s includes final cleanup; service 153.529 s, peak 525.8 MiB
plus 80.1 MiB swap, no faults/services active. Those memory peaks cover all four
requests. CPU remains faster at 36.802922440/15.411270994 s full/short; single
resident observations and different session sequences limit comparisons.
Exact invocation and per-request reference commands are in
[RUNTIME_INTEGRATION.md](RUNTIME_INTEGRATION.md#padded-channel-runtime-0033).

Frozen patch SHA `ee30ca55756e7a67283353803621a78a8cb6e78ba11a37606ab0c5981dba8878`;
model `794aefa840c277ae58a133b5470f4dd2059178e0af2de9fff7c2680feee2eab1`;
runtime `5057a939f068bfcd96590067bf2874427df29ab346996b5057f5696bf9949c60`;
unchanged driver `c8fee96723942fca768a9026987a5d609071d2c60079058064377635155cb44c`.
Evidence under WORK: `padded-channel-integration-0033-checkpoint/after.json`,
`planar-pad-channels-integration-tynxtu63/model-load-audit/REPORT.md`,
`padded0033-fixed-independent-vgiws1ob/validation.json`,
`padded-channels-result-0033-t90dqoh4/REPORT.md`,
`padded0033-runtime-validation-i_6pkny1/{REPORT.md,summary.json}` and
`padded0033-runtime-request-{000,001,002,003}-validation.json`. Logs are
`core-padded-channels-0033-repeat2.log` and `runtime-padded-channels-0033-first.log`.
Focused reports are in `padded-channel-classes-m66voh3d/remaining-classes-actual-pwf2x31p/`
and `planar-padded-channels-private-8dpxowcq/tensor/focused-first-result/`.

Before runtime staging, five byte-identical immutable executable/library paths
were deduplicated with hashes, mode and ownership preserved; all paths remain
(`dedup-runtime-assets-before-padded-runtime*`). Exactly two obsolete private
probe executables were then archived with verified host backups and a restore
manifest in `device-probe-executable-archive-before-padded-runtime/`; log
`archive-probes-before-padded-runtime.log` records free 249408→270756 KiB.
Models, captures and outputs were retained; both services stayed active.

## Vector-bias shader experiment: rejected

The private candidate changes scalar bias reads to two vec4 reads, leaving the
planar accumulation body unchanged. Actual A506 compilation reduces whole-program
native length 27→21, but both hot loops still contain 32 scalar MADs, 47 encoded
instructions and no repeat MAD. Both retain 16 full/zero half registers, 16 KiB
shared memory, four loops and no private allocation. The original repeat-MAD
hypothesis fails. A separate observation—two shared-load/wait phases becoming
one—motivated the bounded timing comparison; compiler estimates are not measured
A506 cycle counts.

The first two ordered pairs are confounded by execution position: accepted then
candidate math takes 10.594/35.536 ms; reversing the order gives 11.789/66.611 ms.
Both pass accuracy. A separate warmed run uses four ABBA warmups followed by
eight ABBAABBA measured calls, where A is accepted and B is candidate. Full CPU
reference checking occurs afterward, preserving all twelve downloaded outputs.

Measured accepted/candidate math means are 11.850724/12.704529 ms, medians
11.927482/12.627380 ms; the candidate is slower in all four adjacent pairs
(18.91%, 4.10%, 5.14%, 2.30%). Whole-forward means are 14.984522/15.771816 ms.
Each math call uses eight chunks/eight submits at batch1, grid4×2/local16×8;
native21 is not admitted to production batch8. All 327168 output values pass
the unchanged independent double reference and are bit-identical to accepted
run0, with maximum absolute error 3.42226122e-5. Every input scalar is preserved.
These checks do not include a full workspace readback. Peak 61.7 MiB/zero swap,
service 2.019 s, no faults and both live services active. Aggregate devfreq
counters cannot assign clocks to individual kernels; these are command wall
times, not GPU timestamps or an isolated hardware-cause finding.

Reject the candidate for promotion. Keep accepted math
`d130779e24fa84875adcf63ceb964337a22f402df5779b8eb0ba962b9740aa64`,
driver 0032 and its allowlist unchanged. Candidate SHA is
`09cc6e74665c8f9c5c8c11b97f9548704a97b731d5f8e40d0e7fef2d496e3b4b`.
No full-model run or broader compiler sweep follows this failed timing gate.

Evidence under WORK: `vector-bias-warmed-result-hwj1yfn4/{REPORT.md,audit.json}`,
`vector-bias-order-result-pl1grsi9/REPORT.md`,
`vector-bias-batch1-result-68fn5tom/REPORT.md` and
`vector-bias-warmed-review-9_yqsnbo/review.json`. The frozen helper is in
`planar-vector-bias-warmed-7kxtypbk/`; its source SHA is
`5373abc9cf51d104fa072a182bf02ad3f2570afff7d951426a86b94571f48aaa`, binary
`4831b55f5e991cc20c2f1921d07fb0b94a1140334715e3b14a69e87e222f202c`.
The terminal log is `vector-bias-warmed-first.log`. New private files left on
the tablet are under `/var/tmp/radish/vector-bias-batch1-first/` and
`/var/tmp/radish/vector-bias-warmed-first/` (one helper and two shaders each),
plus two shaders in `/var/tmp/radish/planar-vector-bias-compile-first/`.

Before the next runtime experiment, three identical immutable runtime library/
executable paths were deduplicated; every path remains. Four completed probe
executables were then archived: the two channel-major compile helpers and the
two vector-bias tensor helpers listed above. Those four device executables are
now absent; their shader/evidence directories remain. Byte-verified host backups,
original metadata and a restore script are in
`WORK/device-probe-executable-archive-before-pipeline-cache/`. Logs and exact
inventories are `dedup-runtime-assets-before-pipeline-cache*` and
`archive-probes-before-pipeline-cache.log`. Free storage changed
177196→223276→265920 KiB; models, captures and outputs were retained and both
live services stayed active.
