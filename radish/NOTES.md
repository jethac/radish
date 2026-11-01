# Radish status

## 2026-11-01

Overall goal active. M0–M2 bring-up is demonstrated; selected M3 paths now pass,
including 16 KiB shared reductions, transfers, dependent compute and bounded
3D dispatch subdivision. Recovery now propagates DEVICE_LOST. ncnn ReLU,
32-thread means, Conv1D K3/K7/K11 and InnerProduct pass full GPU output checks.
The fixed Kokoro neural core passes all 562342 outputs. A guarded FP32
InstanceNorm+tiled graph now passes first/warm extractions in 69.862/69.678 s,
including input/output transfer, and its device waveform passes all 127800
samples. The same fused graph's packed six-thread CPU warm mean is 32.079 s:
the GPU remains about 2.17 times slower, despite a 31.2% reduction from the prior
graph's 101.275 s warm extraction. GPU evidence has one warm iteration; CPU has
two. These are core measurements. The new single-process runtime completes
fresh tablet T=78 synthesis in 43.652 s including frontend startup/release,
metadata-only shapes, GPU core load/extraction and waveform tail. All 205942
core values and 46800 audio samples pass against references from its exact
captured boundary; peak 343.6 MiB, zero swap, no new faults. No host calibration
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

Continue from the passing FP32 InstanceNorm+tiled+dilation core and waveform:
69.678044 s GPU warm versus 32.079044 s packed CPU warm mean. The GPU has one
warm sample and CPU two; sustained end-to-end evidence remains open. Incremental
0018 now covers the 16 dilation3/5 convolutions, with unchanged operator/model
accuracy gates and no new faults. Profile the remaining convolution and
pointwise/normalization costs before choosing the next kernel change.

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
