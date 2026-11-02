# Source checkpoints

These are local experimental checkpoints, not upstream submissions or Vulkan
conformance evidence. See `../NOTES.md` and `../VULKAN_AUDIT.md` for failures,
limits and validation scope. Published checkpoints remain experimental; publication is not an upstream submission.

| Patch | Base | Use |
| --- | --- | --- |
| `0001-radish-compute-bringup.patch` | Mesa `ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230` | Historical M2 checkpoint |
| `0002-ncnn-reduction-workgroup.patch` | ncnn `f18f3ffc94dbc76a277e94556c588ab2c2d0e1d9` | Historical 64-thread backend adaptation; hardware execution hangs |
| `0003-radish-m3-cumulative.patch` | Same Mesa base as 0001 | Historical M3 partial checkpoint |
| `0004-radish-bounded-dispatch-cumulative.patch` | Same Mesa base as 0001 | Historical bounded-dispatch checkpoint |
| `0005-ncnn-reduction-workgroup32-cumulative.patch` | Same ncnn base as 0002 | Current 32-thread backend adaptation; use instead of 0002 |
| `0006-ncnn-execution-trace.patch` | Same ncnn base; independent of 0005 | Opt-in actual backend/transfer/submit trace; apply alongside 0005 |
| `0007-radish-compiler-lifetime-cumulative.patch` | Same Mesa base as 0001 | Full-model baseline before profiling and batching; releases compiler NIR after variant creation |
| `0008-ncnn-radish-pipeline-cache.patch` | Same ncnn base; independent of 0005/0006 | Opt-in exact Radish A506 exception to the old Qualcomm pipeline-reuse heuristic; apply alongside 0005/0006 |
| `0009-radish-profile-batching-cumulative.patch` | Same Mesa base as 0001 | Earlier driver with command profiling and opt-in bounded short-shader batching; grid/shared/dependency/full-core checks pass; default one chunk per submission |
| `0010-ncnn-conv1d-padding-shape-hints.patch` | Same ncnn base; independent of earlier patches | Propagates known packed shapes to Conv1D's Padding helper; full-core device output remains bit-identical |
| `0011-ncnn-conv1d-gemm-induction.patch` | Same ncnn base; independent of earlier patches | FP32 nonshared GEMM tap/channel induction preserves accumulation order; K3/K7/K11 and bit-identical full-core device output pass |
| `0012-radish-shared32-groups-cumulative.patch` | Same Mesa base as 0001 | Historical shared32 group-budget checkpoint; replaced by cumulative 0016 |
| `0013-ncnn-conv1d-shared32.patch` | Current ncnn stack including 0010/0011 | Optional exact Radish FP32 shared32 Conv1D GEMM; both builds and K3/K7/K11 device checks pass; full model passes bit-identically but slows to 198.453 s; opt-in candidate rejected for performance |
| `0014-ncnn-conv1d-a5xx-tiled.patch` | Current ncnn stack including 0010/0011/0013 | Opt-in FP32 64x64 tile, local 16x8, 16 KiB shared; revised 16-full/0-half shader passes all 25984 operator outputs; no accepted model speed win |
| `0015-ncnn-radish-mixed-storage.patch` | Same ncnn base plus 0006 | Opt-in GPU storage conversions; FP32 norm islands remove diagnosed NaNs, but packed full core and waveform accuracy still fail |
| `0016-radish-register-budget-cumulative.patch` | Same Mesa base as 0001 | Historical register-budget driver, retained in 0022; includes0012 and rejects combined full/half register footprints beyond the measured A506 budget before shader upload |
| `0017-ncnn-conv1d-shared-a-transpose.patch` | After 0014 in the ordered Conv1D stack | Both builds/operator/full fused FP32 core/waveform pass; 76.534312 s first/74.317956 s warm (one warm), versus same-graph CPU 32.079044 s (two warm); GPU remains slower |
| `0018-ncnn-conv1d-tiled-dilation.patch` | After 0017 in the ordered Conv1D stack | Extends only dilation1/3/5; operator/full fused FP32 core/waveform pass; 69.862470 s first/69.678044 s warm, 416.7 MiB/zero swap, no faults |
| `0019-ncnn-radish-reshape-alias.patch` | Same ncnn base; independent Reshape file | Default-off exact Radish FP32 pack4 rank3→rank2 output alias; original input copies retained; builds/operator/full-core gates pass, output bit-identical, 67.138187 s first/66.919494 s warm. Earlier bidirectional variant in bf4d98f is rejected at 109.107981 s warm |
| `0020-ncnn-conv1d-tiled32.patch` | After 0018 in the ordered Conv1D stack | Default-off local 8x4, tile64x32, 12 KiB shared; builds/compiler/operator/full-core accuracy pass, output byte-identical; 101.354178 s first/100.467084 s warm versus accepted 66.919494 s warm; 414.8 MiB plus 52 MiB swap; rejected for performance |
| `0021-ncnn-conv1d-tile32x32.patch` | After 0020 in the ordered Conv1D stack | Revises the same default-off TILED32 path to tile32x32, eight accumulators, 8 KiB shared; builds/all 20 variants/operator/full-core accuracy pass; output byte-identical, first 138.633996 s versus accepted first 67.138187 s, peak 425.2 MiB/zero swap/no faults; rejected for performance, no warm repeat; accepted64x64 path unchanged |
| `0022-radish-tiled-batching-cumulative.patch` | Same Mesa base as 0001; replaces 0016 | Prior accepted driver; optional 2/4 sequential single-group tiled chunks per submit, unchanged waits/flushes and register guard; full core 56.013717 s warm, byte-identical, 428.5 MiB/zero swap/no faults; fresh T78 runtime passes |
| `0023-radish-tiled-batch8-cumulative.patch` | Same Mesa base as 0001; replaces 0022 | Current driver; adds batch8 under unchanged 0022 resource gate; build/reconstruction/operator/full-core checks pass, Snake 49.266772/49.429406 s first/warm, byte-identical, 419 MiB/zero swap/no faults; fresh T213 synthesis 63.528396 s, all core/waveform values pass, 458 MiB/zero swap; runtime default8 after validation, driver/operator/model 1, no16 |
| `0024-ncnn-conv1d-planar-input.patch` | After 0021 in the ordered Conv1D stack | Default-off PLANAR layout for eligible tiled64; one initialized padding/transpose workspace replaces existing padding, original math order/output retained; builds/seven-file reconstruction/all 20 compiler variants/padded dilation1/3/5 pass. Fixed core48.738814/48.863090 s first/warm, all 562342 outputs byte-identical, 420.1 MiB/zero swap/no faults; single warm pair1.1457% faster, fresh runtime pending |
| `0025-radish-planar-batching-cumulative.patch` | Same Mesa base as 0001; alternative to 0023 | Candidate adds only native instrlen31 beside36/37 in existing tiled-batch resource gate; build/all 14-file reconstruction, padded operators and full fixed core pass; all 53 math commands retain batch8 schedule. Remaining conditions/defaults and ncnn K bound unchanged; PLANAR remains opt-in |
| `0026-ncnn-planar-pad-local 64.patch` | After0024 in the ordered Conv1D stack | Four reviewed host changes: planar prepass local 8x4→8x8, two pipeline guards and group-count formula; shader bodies/defaults unchanged. Reconstruction/builds/compile/focused/full-core gates pass, first48.370236/warm46.932926 s, identical outputs,418.4 MiB/zero swap; single warm3.9501% below0024. Fresh T78/T213 passes31.890578/63.320730 s, all core/waveform values, zero swap/no faults; PLANAR default 0 |
| `0027-radish-tiled-x2-cumulative.patch` | Same Mesa base as0001; alternative to0025 | Default groups1, opt-in 2 for narrow A506/instrlen31 tiled resources; X pairs/odd tails, cap4 EXEC chunks per submit, synchronization unchanged. Build/reconstruction/operators/full core pass, all 562342 outputs byte-identical,45.621586/47.975705 s first/warm,no faults;417.4 MiB plus 25.3 MiB swap. Warm2.2218% slower than 0026 in confounded single pair; no performance acceptance |
| `0028-ncnn-instancenorm-sum4.patch` | Independent InstanceNorm files on current ncnn stack | Default-off FP32 pack4 variance-first fusion; initial hint-gate failure corrected. Builds/compiler/focused/full core pass byte-identically; first47.302535/warm46.983200 s,418.5 MiB/zero swap.49 targeted stages save 1.632487 s but whole warm stays flat. Fresh T78/T213 core/wave outputs pass31.783290/65.988211 s,0/46.9 MiB swap,no faults; no overall latency win/default change |
| `0029-radish-short-batch8-cumulative.patch` | Same Mesa base as0001; alternative to0027 | Adds generic batch8 under unchanged short/no-loop/no-barrier gate and profile-only submit sum/max timers; geometry/synchronization/defaults unchanged. Build/reconstruction/grid/paired full core pass, warm47.413913→45.339510 s, identical outputs/no faults; submits26292→15398. Fresh T78/T213 references pass32.622528/61.429138 s,0/16.7 MiB swap; T78 total regresses. Opt-in recommendation/norm0/X1, defaults unchanged |
| `0030-ncnn-planar-positive-indices.patch` | After0026 in ordered Conv1D stack | Six equivalent positive-domain integer substitutions, float/layout/barriers unchanged. Builds/focused d1,d3,d5 references pass; all 20 programs/53 layers compile16 full/0 half/no private,16 KiB/four loops, lengths 27/28. Full fixed core passes identical outputs, warm44.956105 s/current 30.6 MiB swap; modest single-pair improvement |
| `0031-radish-planar-index-batching-cumulative.patch` | Same Mesa base as0001; alternative to0029 | Adds only 27/28 to existing tiled batch length guard; all resource/packet/fence/fault/default controls preserved, X2 still31. Build/all 14 reconstruction and focused batch8 pass; full fixed core passes identical bytes, warm44.956105 s/current 30.6 MiB swap |
| `0032-radish-indirect-dispatch-cumulative.patch` | Same Mesa base as 0001; alternative to 0031 | Core indirect dispatch reads arguments at execution time, then uses the bounded direct executor. Build/reconstruction, 36 indirect checks and 16 direct-grid checks pass; public DispatchBase and conformance remain pending |
| `0033-ncnn-planar-padded-channels.patch` | After 0030 in the ordered Conv1D stack | Default-off channel padding moves five convolutions onto the tiled kernel; full core/wave references pass, fixed warm 38.556751 s, 715 GPU layers and zero CPU fallbacks |
| `0034-ncnn-instancenorm-outplace.patch` | After 0019 and 0028 in the normalization stack | Default-off named pack4 input alias plus disjoint normalization output removes 49 copies; same-binary warm 38.467278→37.058738 s, exact output bytes, all four fresh core/wave references pass |
| `0035-ncnn-instancenorm-coefficients.patch` | After 0034 in the normalization stack | Guarded coefficient helper for opt-in project NormAffineSnake; 42 fused chains, exact output bytes, fixed warm 35.730754 s and four full core/wave references pass |
| `0036-radish-gpu-elapsed-cumulative.patch` | Same Mesa base as 0001; alternative to 0032 | Default-off bounded GPU timestamp diagnostic for one accepted eight-chunk convolution; six valid samples, unchanged output bytes and counts, paired perturbation budget passes; no public query support or model performance change |
| `0037-radish-gpu-calibration-cumulative.patch` | Same Mesa base as 0001; alternative to 0036 | Default-off fixed MAD/vec4-copy elapsed calibration; ten valid samples and full exact output/guard/input checks; deferred-readback medians 21.3185 GFLOP/s and 4.3011 logical read+write GB/s, without a ceiling or model-speedup claim |
| `0038-radish-tiled-state-reuse.patch` | Incremental over 0037 in a separate experimental checkout only | **Rejected performance experiment:** full poisoned-output correctness passes, but 0.417119% mean GPU reduction fails the prospective 5% gate. Optional reproduction/history only; not part of the recommended driver stack. |
| `0039-ir3-cold-output-collect.patch` | Incremental over 0037 in a separate experimental checkout; 0038 not needed | **Rejected before tensors:** 13 repeat MADs form, but 2 hot register moves and outer-boundary shuffling fail compiler admission; native 27→29 is outside batching/timing gates. Compile-only reproduction/history; no tensor or performance claim. |
| `0040-radish-mad-unroll-calibration-cumulative.patch` | Same Mesa base as 0001; alternative to 0037, without 0038/0039 | Default-off elapsed class for fourfold MAD-loop calibration; full exact output/guard/input checks pass and balanced mean GPU interval falls 5.407433% (12.597682→11.916471 ms), yielding 22.526423 GFLOP/s. No model dispatch or public-feature change. |
| `0041-radish-shared-hot-calibration-cumulative.patch` | Same Mesa base as 0001; alternative to 0040 | Default-off diagnostic class for the original shared-hot implementation; full exact outputs/guards/inputs pass and balanced mean GPU interval falls 24.890379%. Both strict no-hot-MOV rejections remain failures; revised exploratory scope includes the extra moves. No model optimization or conformance change. |
| `0042-ncnn-planar-packed-a.patch` | Incremental after 0033 in the ordered Conv1D stack; now applied/verified by build-ncnn.sh | Default-off packed constant-weight layout; focused GPU time falls 9.10%, same-binary fixed warm core 4.20%, exact outputs. All four fresh runtime references pass; resident hits improve, cold/shape misses regress. Default0. Requires fresh consistent shader tables. |
| `0042-radish-packed-a-timer.patch` | Incremental over cumulative 0041 | Adds only a default-off 25-line packed-A timestamp class. All 23 changed model variants already use existing native 28 batching; no model scheduling change. |

| `0044-radish-core-api-foundation.patch` | Incremental over cumulative 0041 plus the 0042 Radish timer patch, isolated WIP checkout | API5 functional probe passes; image transfer foundation builds but is not device-qualified here. Events/query/resource consumers and complete compute behavior remain in progress; API version stays 1.0. |

| `0044-radish-compute-resources-wip.patch` | Incremental after 0044 foundation | Experimental combined resource/UBO/dynamic/event/query/subgroup implementation. Host build passes; qualification properties are not yet device-proven. Not part of the accepted inference stack. |

0042 is now part of the standard verified ncnn patch stack, with its selector
default off in all device wrappers. The runtime serializer/capture verifier use
the same environment key. Focused, fixed-core and all four fresh runtime
references pass. Resident synthesis improves, but cold/shape misses regress and
the mixed request sum saves only 0.394555 s. See `../GPU_PACKED_A.md` and the compact
runtime result for the measured tradeoff; no CPU win or default recommendation.

0041 adds one narrowly gated 28-line timing/batching block to 0040. All fourteen
files reconstruct exactly from the pinned Mesa base. SHA256
`416045c8ef3b572abf128ad21b432673f8f35bc37989507b0fcd70e18577d2d5`.
The normal model batching allowlist and existing diagnostics are unchanged;
0038/0039 remain excluded. See `../GPU_SHARED_HOT.md` for the revised diagnostic
scope, paired results and reproduction.

0040 is cumulative from the same pinned Mesa base, preserving all 0037 policies
and adding only three timer-selection lines. SHA256
`f627d0af11ea9ac77e0bccdc2d8e8aaff972ec500ef8698381004eb3700ee030`.
All fourteen source files reconstruct exactly; 0038/0039 are excluded. See
`../GPU_MAD_UNROLL.md` for paired results, exact checks and reproduction.

Standard diagnostic reconstruction uses 0041; the 0042 experiment adds its
incremental timer patch. The 0042 measured private driver is a9be6b5c…12a4e; separately linked canonical
ICD 6ae15421…7813f has identical reviewed queue source and host-build evidence.
The global device ICD remains 0032; prior end-to-end results used 0032. Canonical 0041 ICD f9610658…bb096 is host-built from the same
reviewed queue source as measured private 49d84db0…76833. Its optional helper
is byte-identical to the measured binary. Earlier 0040 calibration used private
30e19d89…8758d; canonical 0040 was 366948ba…51a8e.

0039 is not cumulative and is not in the recommended stack. SHA256
`9633ded58bf108c543d536582f795adbc40bafab0c591457bdfeb89ac4b9eaac`.
The compiler option is default-off; actual OFF/ON compilation used private
ICD `8189ca1f…a0633` with the unchanged optional 2510 helper. That experiment
left canonical Mesa 0037 unchanged at that checkpoint. See `../GPU_REPEAT_COALESCING.md` for source/native findings and
the host-checked portable compile-only runner.

0038 is not cumulative. SHA256 `0d3a34785a96f50aabba75529372d83838a71e22ae5613bc350a33cd4d37a72e`; its tested driver was private `bd2a672a…e1a0f`. The experiment left canonical 0037 and the global device 0032 unchanged at that checkpoint. See `../GPU_STATE_REUSE.md` for scope, paired results and optional reproduction.

0037 is cumulative and preserves 0036's convolution class and all model dispatch
policies. Apply it instead of another cumulative Mesa patch. SHA256
`56273f3da2569c3c3c95d877fac5f81fc2bb876cb9f800699efb9659d17f60b6`.
Canonical builds pass; model measurements/global device ICD remain on 0032.
Calibration used private ICD `393383ce…a2900`; canonical ICD
`2f4fde0e…1b1682` has only host-build evidence. See `../GPU_CALIBRATION.md`.

To reconstruct the accepted driver checkpoint, make a separate checkout/worktree at the
Mesa base, read its `AGENTS.md`, and apply 0041 with `git apply` instead of
earlier cumulative Mesa patches. Set
`RADISH_MESA` to that checkout and run `../scripts/build.sh`. Preserve the AC8
checkout and use the shared scratch disk for source/build outputs. The existing
sysroot/build environment paths and private loader procedure are in `NOTES.md`.
The 0035 model measurements used driver 0032; 0036's private timing qualification
and exact build/capture pins are in `../GPU_TIMING.md`. Its diagnostic is disabled
by default and changes no existing shader, dispatch geometry or public feature.

Checkpoint 0023 is cumulative from the same Mesa base: apply it instead of 0022,
not on top. Its only behavior change from 0022 is allowing explicit
`RADISH_TILED_BATCH_CHUNKS=8` under the existing resource condition. Per-chunk
setup, waits/flushes and single-group hardware execution are retained; driver,
operator and model defaults stay 1; runtime changes from 4 to 8 only after the
fresh full-runtime validation below. The pinned ncnn K<=3584 bound and resource
gate are unchanged. Wrapper syntax/guard
checks pass for exactly 1/2/4/8; 16 remains rejected. The targeted ICD build
passes with SHA256
`9e8274ca701163e442237e78c7ff965a4de745c97bfb16cfa54a563655f23ff2`.
Complete dilation1/3/5 checks pass all 25984/23424/20864 values at unchanged
gates, with 8.485493/6.473352/6.448820 ms math commands and 8/6/6 chunks each
using one submit. No faults, services active. Logs:
`operators-tiled-batch8-0023-d{1,3,5}.log`. The fixed Snake repeat in
`core-snake-batch8-0023-repeat2.log` passes all 562342 outputs in both extractions,
49.266772/49.429406 s first/warm after 9.315 s load. Max error 0.000129699707,
relative RMS 7.72155564e-7, zero mismatches and exact batch4 output identity;
the existing full-waveform evidence applies without a new tail run. Peak
419 MiB/zero swap, no faults and services active. Warm latency is 3.4357% below
batch4's 51.188057 s (one warm each); accept the fixed-core checkpoint. Complete
warm attribution preserves all 1798 resource/grid/chunk signatures and 901
layer paths; only
53 tiled submit counts change, 6486→3255, lowering their command wall
28.093624→25.551525 s. Retained 1745 commands take 0.711040 s longer with
unchanged work/submits; timers include host work and waits, and individual batch
maximum duration is unmeasured. Fresh batch8 T213 runtime also passes all
562342 core values and 127800 waveform samples at unchanged gates and exact
batch4 output identity: synthesis 63.528396 s, core 47.854719 s,
458 MiB/zero swap/no faults, services active. The boundary matches the earlier
CPU and GPU captures. Evidence: `runtime-single-full-snake-batch8-0023-validation.json`.
Earlier fresh 68.158274 s used batch4 and 22.9 MiB swap; the 4.629878 s difference
does not isolate batching across these single observations. The runtime wrapper
now defaults to 8 after validation; Snake remains explicit opt-in. Preserve published checkpoint
`f78917a`; no general watchdog or sustained-performance guarantee is established.
The cumulative patch reconstructs all 14 changed files exactly on the pinned
Mesa base, recorded in
`tiled-batch8-0023-reconstruction-qinlp2gg/reconstruction.json`.

The pinned ncnn/glslang checkout is separate. `../scripts/build-ncnn.sh` applies
0005 if applicable, accepts an already-applied patch, migrates the exact earlier
0002 adaptation, then applies/verifies 0006, 0008 and the ordered Conv1D stack through 0033/0042,
plus the independent net/reshape/normalization stack through 0035.
The first candidate build stopped at overlapping earlier-patch verification.
The build script accepts exact ordered 0010/0011/0013/0014/0017/0018/0020/0021/0024/0026/0030/0033/0042 Conv1D prefixes
and 0006/0015 net.cpp prefixes, rejecting unrelated edits. This preserves the
source check despite overlapping patch contexts. Canonical and SIMPLEOMP builds
pass with revised 0013/0014, 0015 and incremental 0017; both builds and the
integrated runtime also pass with 0020 and 0021. Set
`RADISH_NCNN_TRACE=1` for execution evidence; tracing does not use timestamp
queries or change backend selection. The 32-thread path passes all 18 mean
cases; the physical64 failure remains unresolved. It does not enable optional
subgroup or arithmetic features. Kokoro's converter-axis repair is a separate
guarded script, `../scripts/fix-kokoro-reductions.py`, applied to generated model
parameters; it does not modify the original ONNX model or weights.

Patch 0008 requires `RADISH_NCNN_PIPELINE_CACHE=1`, vendor `0x5143`, device `506`
and the exact Radish device name. It restores ncnn artifact reuse disabled by
the Qualcomm API<1.0.66 heuristic; complete cache keys, object ownership and
reported Vulkan API remain unchanged. This is not persistent Vulkan-cache
conformance evidence. The fixed full-core run passes with this option.
`../scripts/adapt-kokoro-layouts.py` separately replaces 60 verified ExpandDims
with equivalent Reshape operations; full CPU output is bit-identical before
device execution. Generated parameters, fixtures and weights remain private.

`../scripts/hint-kokoro-shapes.py` adds standard ncnn shape metadata to a new
fixed-fixture copy after CPU capture and full output validation. The hinted GPU
result is bit-identical to the unhinted result, with fewer dispatches. Hints are
exact runtime dimensions, not generic promises about variable-length inputs.

Build the optional CPU threading variant separately with
`RADISH_NCNN_SIMPLEOMP=ON RADISH_NCNN_BUILD_DIR=/path/to/build-ncnn-simpleomp`.
Select its model executable with `RADISH_MODEL_BINARY` in `device-model.sh`.
Default threading build remains OFF; model default is four requested threads.
`device-model.sh` accepts `--cpu-packed`, `--threads 1..6`, and `--repeat 1..10`;
repeated validated runs exclude the first extraction from the warm mean.
`RADISH_PROFILE=0|1` and `RADISH_NCNN_TRACE=0|1` control instrumentation;
`RADISH_BATCH_CHUNKS=1..4` defaults to 1. Patch 0011's K11 program drops from 377
to 254 instructions and 151 to 75 NOPs, with 16 full registers/eight waves unchanged.
Patch 0012's `RADISH_SHARED_GROUPS=1..8` defaults to 1 and only changes barrier
kernels with at most 32 physical invocations. Existing shared checks, all 18
ncnn means and two complete model extractions pass at 8. That GPU run takes
103.192 s first / 101.275 s warm versus a 32.052 s packed six-thread CPU warm mean;
the performance goal remains open. The GPU warm result contains one iteration.
The later FP32 InstanceNorm variant passes its first extraction in 88.315 s;
different rounding and missing warm repeats preclude treating it as a controlled
comparison. Its complete core and device waveform pass the unchanged gates.
With 0017 tiled convolution the same fused core passes repeated first/warm
extractions in 76.534312/74.317956 s, 417.8 MiB peak/zero swap, and its device
waveform passes. Same-graph packed CPU warm mean 32.079044 s remains faster:
GPU elapsed time is 2.317 times CPU, with one GPU warm sample and two CPU.
These are core measurements, not a live end-to-end comparison.

Candidate 0013 requires `RADISH_NCNN_SHARED32=1` and exact Radish A506 FP32 mode.
It uses local8x4 with 8 KiB shared arrays only for Conv1D GEMM, preserving other
operator options and guarding tail loaders while retaining all barriers.
The initial compile-only run failed before GPU execution because GLSL read
`gl_WorkGroupSize` before a fixed local-size declaration. Revised 0013 uses
explicit specialization ID 15 for the shared32 selector, with matching host
entries only for shared-memory shaders. Corrected device compilation passes:
K7 uses 16 full/1 half register and K11 uses 21 full/12 half registers, both
local8x4. Existing K3/K7/K11 output checks pass with shared-group budgets 1 and 8;
K11's maximum absolute error is `3.81309655e-06`, and neither run reports a new
GPU fault. Evidence is in the scratch-disk logs
`operators-shared32-specialization-compile.log`, `operators-shared32-execute.log`
and `operators-shared32-groups8-execute.log`. `core-shared32-first.log` passes
all model outputs bit-identically with no faults, but extraction slows to
198.453 s. The option remains disabled by default and is rejected for performance.
Packed-FP16 conversion/ReLU is separately validated by the local32 helper probe;
the full model's packed mode fails the existing accuracy gate with NaN outputs
(`core-fp16-packed-first.log`), so its output remains an unvalidated candidate.

Candidate 0014 is disabled unless `RADISH_NCNN_TILED=1`. Its exact Radish,
FP32, packing and shape gates preserve existing paths elsewhere. Both ncnn
builds pass. Revised scalar-A code uses 16 full/0 half registers, 954 instructions,
four loops and 16 KiB shared memory at local128. All 25984 operator outputs pass,
max absolute error 3.422e-5, no faults. Its 17.919664 ms/eight submits versus the
default 18.809093 ms/one submit is a single-case command-wall comparison, not an
accepted model win. Original 21-full local128 code was never executed.

The register guard introduced in 0016 and retained in 0022 uses `ir3_shader_halfregs` to combine full/half footprints
against a measured 2048-full-vec4 budget. It rounds barrier workgroups to whole
resident waves and requires one wave for non-barrier shaders. The old 21-full
local128 pipeline now rejects with -8 before shader upload/execution
(`operators-tiled-register-reject.log`); revised 16-full passes. General
occupancy safety and the root cause of every earlier hang remain unproven.

Incremental 0017 only transposes shared A, making the eight A scalars consumed
by each thread contiguous. Loader/read mapping, K order, arithmetic, memory
size and both barriers are preserved. Both builds and source review pass.
Compilation gives 16 full/0 half registers, 950 instructions, 1152 dwords and
four loops; the inner reduction uses three vec4 local loads instead of nine
shared loads. All 25984 outputs pass with max error 3.42226122e-5 and no faults.
Command wall time is 10.868408 ms/eight submits versus scalar 0014's 17.919664 ms/eight
and the default18.809093 ms/one. The subsequent full fused FP32 core also passes
(`core-instancenorm-tiled-transpose-first.log`): all 562342 values, max error
1.06811523e-4, rms 7.80397212e-7, 75.743194 s first extraction, cold 9.673 s,
400.6 MiB/zero swap, no faults. Actual trace records 1090 GPU layer calls and
three CPU deconvolutions; the profile contains 37 tiled math commands. This
first-run result is not a controlled warm comparison. The subsequent repeated
run (`core-instancenorm-tiled-transpose-repeat2.log`) passes 76.534312 s first
and 74.317956 s warm after 9.379 s cold load, 417.8 MiB/zero swap, no faults. Frozen
first/warm outputs share SHA256
`324de97d2c224635b1f54dcb8bf89c73efab2e09a8538691dfe68d4b9c2c0b37`.
The tiled waveform passes all 127800 samples in 0.104488 s, SNR 97.967904 dB,
max error 1.12576771e-5, zero mismatches, 4.7 MiB/zero swap. Same-graph six-thread
packed CPU warm mean is 32.079044 s (two samples), versus one GPU warm sample;
the performance goal remains unmet despite 26.6% less GPU time than the old
101.275010 s warm result.

Candidate 0015 is disabled unless `RADISH_NCNN_MIXED_STORAGE=1` and the exact
Radish device uses packed FP16 with FP32 arithmetic. It honors each consumer's
storage mask with explicit GPU casts, including equal-pack transitions, and
uploads CPU inputs with the consumer's masked options. This supports verified
FP32 normalization regions; it does not establish correct full-model FP16
inference. `../scripts/keep-kokoro-norms-fp32.py` creates the guarded fixed-fixture
variant: 54 normalization chains, 486 masked layers, and CPU FP32 output
bit-identical to baseline. GPU blob 437 now has 128 positive values (min 4.52838685e-5)
and blob 439 all 3271808 finite. The complete model is finite but still fails
168167 outputs (max 0.368797302, rms 0.00359251526), and waveform SNR 40.421776 dB
fails the unchanged gate. See `../NOTES.md` for complete evidence.

`../scripts/fuse-kokoro-norms.py` separately creates a pinned FP32 InstanceNorm
variant: 54 chains become rank2→rank3→InstanceNorm(affine0,eps1e-5)→rank2,
retaining aliases, all weights and downstream style affine operations. Its
1098-layer/1307-blob graph passes host shape/core/waveform gates, then all 562342
GPU core values in 88.314593 s and all 127800 device waveform samples at 98.017571 dB.
This is first-extraction saved-source evidence, not warm or live end-to-end
performance. New layers use 31=2; packed mode would require 0015 and its opt-in.

With the stack ending at 0020, that candidate requires `RADISH_NCNN_TILED32=1`,
works without `RADISH_NCNN_TILED`,
and takes precedence when both controls are enabled. It retains the exact
Radish/FP32/packing/shape gates and original row-major weights. Its separate
local8x4 shader computes 64 channels by 32 positions with BK32 and 12 KiB shared
memory; spatial tails still reach both barriers. The existing local16x8,
64x64 path remains the default tiled variant. Independent source review and
ordered reconstruction pass. Canonical, SIMPLEOMP and integrated runtime builds
pass (`build-ncnn-tiled32-0020.log`, `build-runtime-tiled32-0020.log`).

`operators-tiled32-0020-compile.log` records 32 full/0 half registers, 1476
instructions, 1766 dwords and four loops for dilation1. Model load-only
compilation also passes: all 20 tiled32 programs have local8x4, 12288 shared bytes
and 32 full/0 half registers, satisfying the declared F+H/2<=32 gate. Accepted
pipelines imply zero private memory because the pinned driver rejects nonzero
private allocation; independent NIR/ISA audit also finds no scratch or LDP/STP
spills and matches all 20 expected tuples covering 53 model layers. Load-only
performs weight upload but no model inference.
All 25984 dilation1 outputs pass unchanged tolerances at shared-group budgets
1/2/4/8, max error 3.42226122e-5, no faults and both services active. Math command
wall times are 28.872531/25.032780/23.930798/21.885115 ms with 14/8/4/2 submits.
Even the fastest is slower than 0017's recorded 10.868408 ms; fewer submissions
have not produced an operator speed win. Dilation3/5 pass all 23424/20864 outputs
at groups8, with 19.058804/18.831616 ms math commands and two submits each.
The full-model repeat passes all 562342 values, max error 0.000129699707, RMS
7.72155564e-7, with output byte-identical to the accepted 0019/0018 core. Its
existing waveform pass therefore remains applicable; no new waveform run was
performed. First/warm times are 101.354178/100.467084 s after 11.098 s load,
peak memory 414.8 MiB plus 52 MiB swap, no faults and both services active.
The measured warm time is 50.13% longer than the accepted 66.919494 s, although
new swap use limits causal attribution. Reject 0020 for performance and keep it
disabled by default. Preserve the accepted checkpoint
`bccdcd3265dfbbc5b7c09f21377f2f38a1054820`; full evidence is in
`core-instancenorm-tiled32-0020-repeat2.log` and `../NOTES.md`.

Incremental 0021 changes the current TILED32 selection to 32 channels by 32
positions, keeping local8x4, BK32, sequential K order, all five specializations,
row-major weights and existing exact Radish/FP32/shape gates including Cout%64.
Eight vec4 accumulators and shared A4 KiB replace 16 accumulators and A8 KiB;
shared B remains 4 KiB. Host Y groups become Cout/32. The accepted local16x8
64x64 shader is unchanged. The same control works alone or takes precedence
over TILED; it remains default-off. Reconstruct 0020 from the prefix ending
there, preserved in published checkpoint `b70b0a8c498dd8fabc4409acbface875de0c5e33`.

Independent source review and exact ordered reconstruction pass, as do private
Vulkan 1.0 GLSL/SPIR-V, AArch64 syntax and canonical/SIMPLEOMP/runtime builds.
The focused actual compile uses 16 full/0 half registers, 950 instructions,
1152 dwords, four loops and eight compiler max_waves, with no private/spill
evidence (`operators-tile32x32-0021-compile.log`). Dilation1 passes all 25984
outputs at groups1/2/4/8, no faults; best math 33.229460 ms is slower than 0020's
21.885115 ms and the accepted 10.868408 ms. All 20 model compiler variants pass
at 16 full/0 half and 8 KiB shared, satisfying zero-private/spill and F+H/2<=32
gates. Debug load 15.992 s, peak 284.3 MiB/zero swap, no faults. Dilation3/5 also
pass all 23424/20864 outputs at groups8, taking 29.094190/29.227679 ms with four
submits each. The full first extraction passes all 562342 values byte-identically
to the accepted core, max error 0.000129699707, relative RMS 7.72155564e-7, but takes
138.633996 s versus accepted 67.138187 s first (106.49% longer). Cold load 9.791 s,
peak 425.2 MiB/zero swap, no faults and both services active
(`core-instancenorm-tile32x32-0021-first.log`). Existing exact-byte waveform
evidence applies; no new waveform/runtime measurement was performed. Reject 0021
for performance and keep TILED32 default 0. No warm repeat was performed;
accepted 66.919494 s remains a separate warm baseline.

Candidate 0024 requires explicit `RADISH_NCNN_PLANAR=1` (default0) and the
existing tiled64 FP32 selector, excluding TILED32/SAME/no-pad/nonzero fill.
It replaces the selected layer's padding command with a local8x4 out-of-place
pack4-to-planar pass, fully initializing a checked LP×Cin scalar workspace.
The math shader keeps the original weights, K order, barriers and output layout;
only the input gather uses contiguous spatial scalars. Unsupported static cases
retain their original path. Selected malformed/overflow/buffer conditions fail
before recording. The source and wrappers/probe are reviewed; canonical,
SIMPLEOMP and integrated runtime builds pass. Seven-file ordered reconstruction is recorded
in `planar-0024-reconstruction-4oq6y46v/reconstruction.json`.

`channel-major-compile-first.log` shows binding1 four scalar loads becoming one
vec4 for C128/K11/d1, 950→788 instructions at unchanged 16 full/0 half/four loops/
16 KiB shared. `channel-major-compile-all20-first.log` and
`channel-major-compiler-34cx94fy/all20-actual-audit/REPORT.md` prove all 20 tuples
covering 53 layers use instrlen31, one binding1 vec4 load and no scratch/private
spill evidence. These are pipeline-only results, not tensor accuracy or timing.
Integrated compile-only also passes before upload/submit: the pad shader is
local8x4,8 full/3 half,no loops,instrlen9. The separate padded dilation1/3/5
execution logs pass all27264 outputs each at unchanged gates, no faults and
services active. Candidate math takes7.977001/7.875804/7.922783 ms and padding
2.126673/1.939901/2.465007 ms. The matching padded dilation1 baseline takes
8.272940 ms math/2.946675 ms pad; these single observations do not establish a
model win or equivalent padded dilation3/5 baseline timings. Full log names,
max errors and build records are in `../NOTES.md`.

Separate cumulative 0025 applies to the pinned Mesa base instead of 0023,
adding only instrlen31 alongside36/37 in the existing tiled-batch gate. Other
resource checks, ncnn K<=3584, waits/flushes and defaults are unchanged. Without
this gate the new shader uses one submit per chunk; instrlen31 also follows the
existing <=32 instruction-preload path. The targeted ICD build and all14-file
reconstruction pass (`planar-0025-reconstruction-xcjenuma/reconstruction.json`),
ICD SHA256 `22a34efba7622363225c14600366fe05488fbc38bd452248e43717d40d239227`.
The full fixed-boundary repeat passes in `core-snake-planar-0024-repeat2.log`:
cold9.030 s, first48.738814/warm48.863090 s, all562342 values within unchanged
gates (max0.000129699707/RMS7.72155564e-7), 420.1 MiB/zero swap/no faults and
active services. The output SHA256 remains
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4`;
existing fixed-output waveform evidence applies by exact byte identity, without
a new waveform run. Against accepted49.429406 s, this single warm pair improves
0.566316 s/1.1457%. The53 math commands save1.444377184 s but their padding adds
0.939362616 s and increases submits1654→3696; all1692 other command signatures
and901 layer paths match. Full attribution is in
`warm-planar-result-0024-mdia5brz/REPORT.md`. Command times include host work and
waits. Keep PLANAR off and the accepted baseline distinct; fresh-runtime and
sustained-performance evidence remain pending.

Ordered 0026 follows 0024 without changing its patch or shader bodies. It changes
only the planar prepass local8x4→8x8, two guards and its group-count formula to
address the measured padding overhead. The same53 owners project3696→1848
padding submissions. All seven Conv files
reconstruct exactly (`planar-pad64-reconstruction-uoqs78zq/reconstruction.json`).
Canonical/SIMPLEOMP builds pass and runtime links. Compile-only confirms pad
local8x8/8 full/3 half/228 instructions/zero loops/no private, unchanged math;
focused dilation1 passes all27264 outputs at unchanged gates, pad1.770161 ms and
math7.917470 ms, 144+8 groups, no faults/services active. Logs:
`operators-planar-pad64-0026-{compile,d1}.log`. Full repeat2 passes in
`core-snake-planar-pad64-0026-repeat2.log`: cold8.848 s, first48.370236/warm
46.932926 s, all562342 values within unchanged gates and exact output SHA
`0550fe07becc684c3dc7b0113bfef0b4e3c87ff8d888acf779e98de4d8af18e4`.
Peak418.4 MiB/zero swap/no faults, services active. Prior fixed-waveform evidence
applies by output identity without a new tail run. Single warm comparisons are
3.9501% faster than0024 local8x4 and5.0506% faster than0023. Actual padding
submissions halve3696→1848; padding saves0.839163454 s, math0.033946887 s.
The1692 retained command signatures/901 paths match, but their timing also
improves0.794779154 s, so the whole1.930164 s reduction is not all padding.
Comparisons: `warm-planar-pad64-result-0026-ooc8sgac/{vs0024,vs0023}/comparison.json`.
Fresh T78/T213 runtime also passes all core/waveform values against exact-boundary
ORT references: synthesis31.890578/63.320730 s, core18.148241/47.887015 s,
329.1/429.7 MiB with zero swap/no faults and active services. Waveform SNR remains
96.917732/98.355163 dB. Validation files:
`runtime-single-{short,full}-planar-pad64-0026-validation.json`. Full eight-input
identity to prior CPU/0023 and GPU core/waveform byte identity are recorded in
`runtime-full-planar-pad64-0026-boundary-identity.json`. T213 startup is close to
prior63.528396 s, so this is not a broad synthesis-latency claim. PLANAR stays
default0 and earlier results retain their original local sizes.

Cumulative0027 adds opt-in `RADISH_TILED_GROUPS_X=2`, default1. The existing
PLANAR31 resource gate additionally requires gridX>=2; two groups consume the X
budget only, preserving Y/Z and odd X tails. Paired submissions cap at four
EXEC chunks even when tiled batch8 is requested. It changes no per-EXEC
WFI/flush, fence waits or fault checks. The targeted build and all14-file
reconstruction pass (`tiled-x2-0027-reconstruction-i5z4h0kg/reconstruction.json`),
ICD SHA256 `36666edbfb9759d6e53b5389103f7ccee2e907444b85fa630aca968c8d8af0f8`.
Padded dilation1/3/5 groups2/batch1 pass all27264 outputs each at unchanged gates,
four math chunks/submits; dilation1/batch4 also passes four chunks/one submit.
Logs: `operators-planar-x2-0027-first.log`,
`operators-planar-x2-0027-d{3,5}-batch1.log`, `operators-planar-x2-0027-d1-batch4.log`.
Full repeat2 passes all562342 outputs byte-identically in
`core-snake-planar-x2-0027-repeat2.log`: cold9.022 s, first45.621586/warm47.975705 s,
no faults/services active. Peak417.4 MiB plus25.3 MiB swap; warm is2.2218% slower
than0026's46.932926 s in a single pair confounded by swap. The unchanged fixed
output SHA0550fe07…f18e4 retains prior waveform evidence without a new tail run.
The53 math commands show no win24.073201017→24.095025469 s despite chunks
25944→13024; the cap4 policy leaves submissions3255→3256 (one extra odd-X submit).
Other1745 signatures
match but timing adds0.823383997 s, so most whole-wall increase is outside changed
math. Preserve the swap confound and single-pair limit.0027 is an opt-in narrow
correctness capability; keep0026/groups1 as the performance recommendation.
No fresh-runtime repeat or default change is claimed for this non-winning case.
Both first/warm maps are documented in `warm-planar-x2-result-0027-nf2vcmi7/REPORT.md`;
the faster first extraction also comes mainly from retained-command timing.

0028 is independent of the Conv1D stack and changes only InstanceNorm host/header
plus a new local16x1x4 shader. Explicit NORM_SUM4=1 selects a narrow exact-Radish
FP32 pack4/rank3/h1/n1/known-channel/affine0/epsilon1e-5 contract; default0 and
unsupported paths remain original. It replaces sub-mean/square plus first sum4,
preserving mean/later variance tree/final normalization and exact guarded-tail
arithmetic. Private source/SPIR-V and five-case probe/provenance reviews pass;
canonical/SIMPLEOMP builds pass. Initial actual compile-only
`operators-norm-sum4-0028-compile.log` rejects fused selection before VkCompute:
Net packs Vulkan top_shapes, so the initial pack1/C gate cannot match. The revised
patch requires pack4/16B/c=channels/4; both builds and all three reconstructed
files pass (`norm-sum4-0028-reconstruction-cpwcmz6a/reconstruction.json`). The
initial source/log are archived in `norm-sum4-0028-integration/failed-hint-evidence/`.
Corrected actual compile-only `operators-norm-sum4-0028-compile-packed-hint.log`
passes all five cases. Fused programs use179 repeat-expanded instructions/198 padded dwords/instrlen6,
8 full/1 half/zero loops, local16x1x4 and accepted no-private gate. Precise NIR
fmad is unfused. Original/fused hardware probes pass all independent gates and
all36606 dumped values match byte-for-byte, SHA256
`78e80ff3b454f44f7fbb4e15818e5ba0c4ee29ba89a9ccb147f38f10809c54ff`
(`norm-sum4-0028-integration/focused-identity.json`); no faults/services active.
Runtime builds; full repeat passes in `core-snake-planar-norm-sum4-0028-repeat2.log`
with X1/generic batch4/tiled8: cold9.694 s, first47.302535/warm46.983200 s,
all562342 values at unchanged gates and byte-identical fixed output SHA0550fe07…f18e4,
418.5 MiB/zero swap/no faults/services active. Prior fixed-waveform evidence
applies without a new tail run. Single warm wall is essentially flat versus0026
(+0.050274 s/0.1071%). Actual49 square commands1.778057033 s plus49 first
reductions0.368136867 s become49 fused reductions0.513706515 s, saving1.632487385 s.
Other1700 command signatures/901 layer paths match, but retained timing adds
1.547037747 s. Comparison: `norm0028-compare-prepared-l4fxn44o/comparison.json`.
The earlier focused7 claim was a padded-size counting error:95 encoded
instructions→ceil(95/16)=6, then four NOPs yield198 dwords (`ir3.c:396`). Actual
focused/model execution both report6. Frozen proof and independent profile audit:
`norm0028-result-audit-zm_1noi9/REPORT.md`. Fresh T78/T213 both pass exact-boundary
host references for all core/waveform values: synthesis31.783290/65.988211 s,
core17.777928/46.628144 s,328.6/436.9 MiB and0/46.9 MiB swap, no faults/services
active. Waveform SNR96.917732/98.355163 dB; validation files
`runtime-single-{short,full}-norm-sum4-0028-validation.json`. Full eight-input and
core/wave byte identity is recorded in `runtime-full-norm-sum4-0028-boundary-identity.json`.
No overall latency win; default0 and0026/groups1 recommendation remain unchanged.
The ledger pins revised source; initial97b28ffe…95bea remains historical only.

Cumulative0029 allows generic batch8 only for !loops/!barrier/instrlen<=32,
preserving64-group hardware chunks and per-EXEC synchronization. It records
profile-only submit-through-wait sum/max wall times; these include CPU scheduling,
not isolated GPU time or watchdog bounds. Defaults remain driver/model1/runtime4.
Build/all14-file reconstruction (`short-batch8-0029-integration-bot0hl5l/checkpoint.json`)
and16 grid cases pass, ICD
`c7ec53be69dbe249112c3e6aeae01532175390fbcf4718e2da1ff54cb4badad5`.
Paired fixed-core logs `core-snake-planar-short-batch{4,8}-0029-repeat2.log` pass
all562342 values with exact prior output identity: first47.715981→45.766135 s,
warm47.413913→45.339510 s (4.3751%), no faults/services active. Baseline419.7 MiB
plus7.7 MiB swap versus candidate417.5 MiB/zero swap; one warm each. All1798
command signatures/901 paths/114393 chunks match; only669 submission counts
change, whole26292→15398. Timing invariants pass; eligible observed submit maxima
34.062768→33.911360 ms first and5.011371→6.193822 ms warm. Evidence:
`short-batch8-result-0029-p9a9QvGK/REPORT.md`. Fresh T78/T213 all core/wave outputs
also pass exact-boundary references and retain0026 bytes: synthesis32.622528/
61.429138 s, core17.289251/45.667104 s,330.7/431.3 MiB and0/16.7 MiB swap,
no faults/services active. Validation files:
`runtime-single-{short,full}-short-batch8-0029-validation.json`; boundary identity:
`runtime-{short,full}-short-batch8-0029-boundary-identity.json`. T78 total regresses
from31.890578 s despite faster core because load9.191366→10.769532 s. Preserve
that regression, full-run swap and single-observation caveats. Recommend batch8
opt-in for this measured fixed path; norm0/X1 and existing defaults unchanged.

0030 changes only six integer expressions in the PLANAR math shader; positive,
bounded host-gated domains make unsigned division/shifts equivalent. Actual
C128/K11/d1 compile-only passes (`planar-index-compile-0030-first.log`):16 full/
0 half/no private, four loops/16 KiB,431 encoded instructions→instrlen27,
870 padded dwords. The original inner K sequence/registers/access widths and
barriers match exactly. Audit: `planar-index-compiler-38ya7qvg/actual-compile/REPORT.md`.
Subsequent all20 compile-only evidence covers53 layers, with six native27 and
14 native28, identical resource bounds; d1/d3/d5 focused references all pass.
Equal-batch1 d1 math10.858472/10.782274 ms is not a reliable speed claim.
Cumulative0031 adds27/28 only to the existing tiled-batch guard, keeping every
resource/packet/fence/fault/default condition and X2==31. Build/all14-file
reconstruction passes (`planar-index-0031-integration-1fgdngh5/checkpoint.json`),
ICD05ea3444c6b1e38a524ffe59133dd6097634412ffc0a25356b2fbf14b312365e;
focused batch8 passes eight math chunks/one submit, no faults. Full original-
Snake repeat2 passes identical output bytes:45.312337 first/44.956105 warm,
400.6 MiB plus30.6 MiB swap, no faults/services active. Versus0029,53 math
commands save0.373508 s; all1745 other signatures/901 paths,15398 submits and
114393 chunks match. Whole0.383405 s/0.8456% gain is one pair with swap; no fresh
runtime/default change. Audit: `planar-index-result-0031-cp_rznjt/REPORT.md`.


AffineSnake is a project-local module/graph route, not another ncnn/Mesa patch.
The strict714/875 graph retains all weights and48 original alpha/reciprocal pairs;
CPU/focused GPU and full fixed-core gates pass, with identical historical output
bytes. Warm43.124072 s/current7.4 MiB swap; exact192→48 targeted commands save
1.822263 s, all 1606 retained per-owner signatures match although traversal order
changes. Actual kernel8 full/1 half/local32/no shared/private, native4, precise
unfused arithmetic. See NOTES for compiler/attribution evidence. The transformer
only produces provisional output; fixture preparation requires matching completed
CPU runner/log/reference/output-identity proof and validates its marker/assets.
Runtime selection is explicit `--affine-snake`; fresh T78/T213 full same-boundary
core/wave gates pass32.105121/59.184846 s, exact prior bytes,0/74.2 MiB swap and
no faults/services active. Startup and memory pressure limit single-run timing
claims; original graphs and all defaults remain unchanged.


Height-one deconvolution support is implemented in the project graph rewriters,
metadata and runtime/model selectors; no additional Mesa or ncnn math patch is
needed. Explicit Snake/deconv and AffineSnake/deconv graphs preserve all weights
and replace three CPU fallbacks with stock Vulkan 2D operators plus reshapes.
The combined graph traces all 715 non-Input layers through Vulkan and passes
42.469886 s fixed warm extraction. Fresh T78/T213 core/waveform comparisons pass
30.156883/63.130064 s total, with no faults. Full startup regresses despite a
faster core; CPU remains faster. See `RUNTIME_INTEGRATION.md` and `NOTES.md` for
preparation, exact source pins, tolerances and memory/swap evidence. Defaults
remain unchanged.

Cumulative 0032 adds core indirect dispatch with synchronized execution-time CPU
argument reads. The direct executor, chunk limits, synchronization and batching
defaults remain unchanged. The 36 indirect checks cover late host writes and GPU
transfer-produced arguments, offsets and zero grids; 16 direct-grid regressions
also pass. No fault occurred. Public DispatchBase, shader-produced indirect
arguments, 3D indirect and replay remain outside this evidence. See NOTES for
exact source/binary pins. Apply 0032 to the pinned Mesa base instead of earlier
cumulative patches, not after them.

SHA-256:

```
28be2f82f4678dcb399b2eeba4f84b704ad0926440b20d3b823be0b39085bc8e  0001-radish-compute-bringup.patch
7c727e0d1c6ef82b306d4271368e8bb3388356ab9085088736c29812dd66f9fb  0002-ncnn-reduction-workgroup.patch
aad5856347d03a8f0e152f5669929709d76f4ad2623bc3817cacfcb39cf8b032  0003-radish-m3-cumulative.patch
61f39d1e70326e6ceacdf0ab837fdfaa5d54a02b5896875cc26771edc07ac584  0004-radish-bounded-dispatch-cumulative.patch
dfe5666fc16c2fecabc4cae7449c485abbc53fdbf908273231b2d6f8fde9f837  0005-ncnn-reduction-workgroup32-cumulative.patch
45aad97b1c3bc1a280606ce75d6de1dfc261a842c4d4a805bcd1e55b2b934614  0006-ncnn-execution-trace.patch
7ce02f0a61dbcf0d3872b82983b35b14007fa7d976e6b76800844ff6f24e0a54  0007-radish-compiler-lifetime-cumulative.patch
1f243bfc74c73805e192020e78375127f411f1a99db244e1845600fcff61315d  0008-ncnn-radish-pipeline-cache.patch
c12a335bfeb94c42aa4e18ab1a31e15896795b48f63e0bf29b8dbd3a359b6010  0009-radish-profile-batching-cumulative.patch
e7d733de8b7761f2f7214b249d1e21370c00f513f3ff5312679d97d3dd89a4bf  0010-ncnn-conv1d-padding-shape-hints.patch
6281b164967bd782f19bea49aace14d58b56d24c29706f7864d0195665586218  0011-ncnn-conv1d-gemm-induction.patch
7d89926b3dac7289aa93cfc95449ea06ea7313d4558c653d32de4194edb02bf8  0012-radish-shared32-groups-cumulative.patch
f75a263b3f486e3b4961410f5170c885c9bfb670ea9c72e584d6eb3fc279fec4  0013-ncnn-conv1d-shared32.patch
d1081b712ae8a4518b494ced59c7c1ba5fa1da501e3758e33030ddfb22d14695  0014-ncnn-conv1d-a5xx-tiled.patch
53edb9f05fea4eb13cc7d489178ecda63bcc9a5febed5502962f185d063dea10  0015-ncnn-radish-mixed-storage.patch
fc4095829ad35385e505d4e233c12657cd54fe7dc50d88566f1a1c512f167598  0016-radish-register-budget-cumulative.patch
f9e2c39cfab97fbcc58a34c1464af42d4f0b9be08ce1a14dc2f494fe40322d06  0017-ncnn-conv1d-shared-a-transpose.patch
e89d516bca15980a599f96e797b7726546913b05b370bac87a3ac88c8ebc9695  0018-ncnn-conv1d-tiled-dilation.patch
3f38da1822f50b5b46857e091e2366c96baf350136dd585615c7e663ec72ce57  0019-ncnn-radish-reshape-alias.patch
bf61ab2af644ddecb632c7c3d8e48ef261085f3a7c1ad68d657c33abf7cda3df  0020-ncnn-conv1d-tiled32.patch
a29dbbe0c3edf8912e4548cc873beca0ba3f1ba3197b6f1ee5f386bfd4f57dc0  0021-ncnn-conv1d-tile32x32.patch
0c03e76309ae835fa32946a61626614136dc5ea173419383258904933ec7514a  0022-radish-tiled-batching-cumulative.patch
5516b6823e67f70c989939a95e16d853fd00af43d3a280b13cefc4b60da40bfd  0023-radish-tiled-batch8-cumulative.patch
23b9bafe92a6ad41ae393bde9d50576d7e2e6a258036234411e198f0f7c7cce8  0024-ncnn-conv1d-planar-input.patch
fd0a0bb37e562535488406e183c46a233edf616939ad416efb6b09070bc0c17b  0025-radish-planar-batching-cumulative.patch
3bbc418cf86b74ef32c1789b4bdc88c2b88ba4f1368c19ab1d62ee9103996a5c  0026-ncnn-planar-pad-local64.patch
adc9ae8506e91abbacea3674a416b1e907672fecf60eb7c62dbe7c319367f6a1  0027-radish-tiled-x2-cumulative.patch
4aa4d0983dd680492a35db3993bce02030e6700e3add4bc22369f03239a3af33  0028-ncnn-instancenorm-sum4.patch
4d46236f83dfd943080241d05822479aaae6b2fa70e5fa8584b9a557f509995c  0029-radish-short-batch8-cumulative.patch
d706738bce01ab6fc5d2dc45776c843c3fa18e7c40157c4d9fe1627b94c4c65f  0030-ncnn-planar-positive-indices.patch
24090b6d37daf2533f6f5557e1c634cbd362603721d3fdf4b6852587b918be96  0031-radish-planar-index-batching-cumulative.patch
923c05c1fc31b6d690e5d93af4c1d3ced3cb48262004ace3ff8ab8d4ff347eef  0032-radish-indirect-dispatch-cumulative.patch
ee30ca55756e7a67283353803621a78a8cb6e78ba11a37606ab0c5981dba8878  0033-ncnn-planar-padded-channels.patch
b2112c3ae6e0bc580f0d606b8ac8f5cfc8c23a8ea6426511d249673bab7179ed  0034-ncnn-instancenorm-outplace.patch
d468a7b2dcadda77b4e06a286eba806aac28c6222f0f3ec9e8330cddf8dba657  0035-ncnn-instancenorm-coefficients.patch
```

| `0044-radish-resource-layout-storage-wip.patch` | After0044 foundation and combined resource WIP | Broader native formats/layouts/mips/cubes/border colors, compact buffer slots, private/constant backing and lost-queue cleanup. Three consolidated suites pass; BASIC election fails. Experimental source, not complete qualification. |

| `0044-radish-descriptor-backports-wip.patch` | After the three earlier0044 incremental patches | Logical1024 descriptors/static compaction, truthful resource limits, pipeline batch semantics, texture operations/formats, four public KHR backports and API subgroup16 plumbing. Three functional suites pass;2D BASIC fails and remains disabled. |

| `0044-radish-depth-consumers-wip.patch` | After0044 descriptor/backports WIP | D16/D32 native sampling/compare, depth views/barriers and matching-format image copies. Builds; depth consumer execution remains unqualified. Separate color copy/clear run completes40 representative command paths, not conformance. |

| `0044-radish-subgroup-linear-wip.patch` | After0044 depth consumers WIP | Preserve logical compute IDs while flattening BASIC physical workgroups; conservative resident-thread guard. Four focused cases pass; compute BASIC exposed after review. Broader legal workgroup/CTS gaps remain. |

| `0044-radish-compute-contract-wip.patch` | After subgroup-linear0044 | Public singleton/external capabilities, actual private/constant and five BASIC cases, per-pipeline noopt pass; RBA code is WIP, first valid texel read fails and feature remains false. |

| `0044-radish-device-group-robustness-wip.patch` | After compute-contract0044 | Singleton KHR group paths, mutable/fullsplit image consumers and all four core buffer-robustness cases pass; feature promotion, no full CTS/conformance claim. |

| `0044-radish-barrier-image-size-wip.patch` | After device-group-robustness0044 | Exact historical32/64 reduction passes under two-row physical mapping. Checked larger-image layouts build; large-array stride and general workgroup/CTS coverage remain incomplete. |

| `0044-radish-image-maintenance-wip.patch` | After barrier-image-size0044 | D16/D32 native depth consumers pass. Image maintenance contracts, two buffer-layout backports and internal global-load dependency build; not all new source paths qualified. |

0044 qualification erratum: historical resource/robustness PASS entries above include ordinary descriptor updates after recording. Those executions are implementation observations, not valid-use qualification; see CORE_API_0044.md and per-result errata.

| `0044-radish-array-fetch-wip.patch` | After image-maintenance0044 | Bounded private LDG array texel fetch and descriptor-specialized ownership. Corrected valid-order resource/depth/array and publicRBA checks pass; filtered/cube/large-allocation fallback incomplete. |

| `0044-radish-compute-properties-wip.patch` | After array-fetch0044 | Existing−8/+7 texel offsets and two sole-queue application hint levels with unchanged kernel priority. Three CTS property failures removed; full failed report and remaining gaps retained. |

| `0044-radish-filtered-array-wip.patch` | After compute-properties0044 | Software array TXL/TXD/gather filtering; bounded valid-order consumer passes4,930 exact words and full guards. Cube/large-array/full sampler coverage remains open. |

| `0044-radish-sampled-ms-wip.patch` | After filtered-array0044 | Native sampled4 layout/descriptors and color clear/copy/fetch consumer,4,320 exact words/guards. Depth/stencil query-only; distinct sample values unqualified. CTS sampled-count failures removed, full property case still fails. |

| `0044-radish-cube-native-resources-wip.patch` | After sampled-MS0044 | Rebuild live child native texture inventory, preserve raw indices; corrected cube/subword consumers pass6,120/5,950 words and guards. Prior faults retained; true16-native+alias capacity remains open. |

| `0044-radish-image-query-constants-wip.patch` | After cube-native-resources0044 | Exact descriptor-size constants preserve sixteen sampled plus four storage resources:176words/fullguards pass. Existing cube/subword/publicRBA requalify; native high-index S2EN remains unqualified. |

| `0044-radish-barrier-large-array-wip.patch` | After image-query-constants0044 | Irregular barrier32/33/3×5×3 checks pass3,520 words/fullguards; public automatic128MiB array sampling passes176 words/sparsecanaries. Broader configurations and fullmodel remain separate. |

| `0044-radish-register-budget-wip.patch` | After barrier-large-array0044 | Match compiler allocation to unchanged residency guard; all179 model programs load. First full inference fails numerically; failed output preserved, no performance acceptance or batching expansion. |

| `0044-radish-exact-buffer-size-wip.patch` | After register-budget0044 | Exact descriptor byte-size constants fix aligned SSBO bounds; valid SPIR-V RBA/boundary cases and all218112 first-convolution values pass. Fullmodel pending; historical invalid-module erratum retained. |

| `0044-radish-storage-cube-multisample-wip.patch` | After exact-buffer-size0044 | Six-face storageCube, tiny internal distinct MS lanes and public128MiB automatic MS array fetch pass bounded outputs/guards. Full conformance and all resource permutations remain unclaimed. |

| `0045-radish-shared-resource-batching.patch` | After complete 0044 storage-Cube/multisample stack | Resource-based eligibility preserves guards and native code; same-binary tiled1→8 resident fixed-core time falls22.0028%, exact outputs. Sequential pair, candidate23.6MiB swap and remaining53.76% historical regression explicit. |

| `0045-radish-short-buffer-batching.patch` | After shared-resource batching0045 | Bounded two-SSBO loop-free class; only225 normalization submit counts change,2,721 fewer, resident fixed-core2.4763% lower with exact outputs. Shader/math/geometry/guards unchanged; sequential conditions explicit. |

## Explicit application robustness and bounded tails (0045)

- `0045-ncnn-per-device-robustness.patch`: apply after the complete accepted 0042 ncnn stack. Adds an explicit per-device core feature choice, preserving the default.
- `0045-ncnn-tail-loads.patch`: four active stock shader families; bounds discarded spatial/paired loads before opting out of robustness. Rebuild embedded headers and their owning object.
- `0045-model-explicit-device-loader.patch`: separate private model-runner delta, applied at the Radish root. Uses an owned device and verified absolute loader path; the accepted builder remains unchanged.

Both feature modes pass the full fixed core; resident51.504783→34.333299 seconds with exact outputs and unchanged submission schedule. This is audited-workload qualification, not a default flip or arbitrary model safety. See `../EXPLICIT_ROBUSTNESS.md`.

## Host-only ASR integration (0045)

`0045-straylight-asr-vulkan-optin.patch` applies to the exact Straylight source snapshot pinned in `../results/asr-vulkan-host-only-0045.json`. CPU remains default. Rebuild both C++ and Rust for config ABI2; the host Vulkan build/link passes, but its original 16-bit-storage/Vulkan1.2 backend requirements blocked Radish device qualification; later bounded primitive checks and the private KHR host slice are recorded below. No service deployment or inference result is claimed. Commands are in `../ASR_VULKAN_HANDOFF.txt`.

`0045-ncnn-packed-a-mb-shift.patch` is a separate one-expression candidate after the explicit-device/four-tail ncnn stack. All full outputs pass with fewer encoded instructions. Resident33.952417 seconds versus34.333299 is a modest sequential observation, not sole-cause/statistical attribution. See `../GPU_PACKED_A.md` and `../results/packed-a-mb-shift-0045.json`.

`0045-radish-storage16.patch` is a separate incremental checkpoint after the two0045 batching patches. It implements logical16-bit storage bounds and aligned U32 memory lowering, then advertises only KHR_16bit_storage/storageBuffer16BitAccess. The valid public consumer passes500 output words and complete four-allocation guards in five short/dynamic cases. Current inference comparisons remain on frozen fcd; no ASR/API1.2/conformance claim. See ../results/storage16-public-0045.json.

`0045-ncnn-b-reciprocal-unselected.patch` is an optional isolated experiment after0045 packed-A mb shift, not part of the selected stack. The bounded address transformation and full numerical checks pass, but first/resident and targeted-layer wall times regress in the sequential comparison. Keep d332 selected; see ../results/packed-b-reciprocal-unselected-0045.json.

`0045-radish-storage8-timeline.patch` follows 0045 storage16. It adds logical byte-storage lowering and conditional real-kernel timeline exposure, qualified by separate public 8-bit storage and KHR timeline consumers. API remains 1.0; current inference stays frozen fcd and ASR is not yet qualified. See ../results/storage8-timeline-public-0045.json.

`0045-current-mad-private-device.patch` is the exact measured private three-file overlay for the current arithmetic diagnostic: explicit loader/device policy, pinned helper, canonical payload metadata and elapsed-rate reporting. It preserves both GLSL programs and all original controls. See ../GPU_MAD_UNROLL.md; this changes no selected model shader or driver policy.

`0045-radish-shader-int8.patch` follows storage8/timeline and adds only public KHR Int8 arithmetic exposure over the existing generic byte implementation. The validated U32-interface consumer checks 1,280 arithmetic/conversion/private/shared results and full guards. Float16 arithmetic and API1.0 are unchanged; ASR inference remains unqualified.

`0045-ggml-vulkan-khr-profile-private.patch` is a default-OFF backend source overlay on the pinned transcribe.cpp dependency. ON/OFF host objects and two actual Vulkan1.0 Q4 modules pass; real feature checks and actual descriptor limits remain required. Five-buffer device qualification, complete shader/backend coverage and ASR inference remain pending. See ../results/asr-khr-profile-host-only-0045.json and ../ASR_VULKAN_HANDOFF.txt.

`0045-ncnn-packed-dot-unroll4-unselected.patch` is an isolated experiment after the selected mb shift and fixed-tail/private-device stack. Native dependency and full numerical checks pass, but all twelve targeted layers and whole resident time regress in the observed pair; swap differs. It is not part of the selected stack. See ../results/packed-dot-unroll4-unselected-0045.json.

`0045-ncnn-packed-dot-unroll2-unselected.patch` retains native preload and passes exact numerical/recurrence checks, but targeted and whole resident time regress without swap. It remains outside the selected stack; see ../results/packed-dot-unroll2-unselected-0045.json.

`0045-radish-five-ssbo.patch` follows Int8 and changes only per-stage SSBO4→5, now backed by the real five-buffer fused Q4 consumer. `0045-ggml-q4-profile-bounded-loop.patch` follows the private KHR profile overlay and changes only the profile outer-loop hint; default behavior remains unchanged. See ../results/asr-q4-five-buffer-0045.json.

`0045-radish-public11-contract.patch` follows the five-SSBO checkpoint. It makes instance/device/manifest version1.1 consistent and supplies truthful unsupported external semaphore/fence capability queries. The separate private candidate passes bounded public negotiation/alias/error checks and five BASIC cases; the exact original CTS now executes five info tests with four Pass and one property Fail. This is not promotion or full conformance. See `../VULKAN11_COMPUTE_STATUS.md`.

## Integrated resource correction checkpoint (0045)

Apply these increments after `0045-radish-public11-contract.patch`, in order:

1. `0045-radish-packed-storage-formats.patch`
2. `0045-radish-etc2-eac-private.patch`
3. `0045-radish-disabled-feature-defaults.patch`
4. `0045-radish-input-attachment-host.patch`
5. `0045-radish-attachment-images.patch`
6. `0045-radish-mixed-stage-push.patch`
7. `0045-radish-snorm-storage.patch`

The exact reconstruction pins are in `../results/compute11-private-source-stack-0045.json`. Original packed SNORM and the core supplement pass after correction; host descriptors, mixed push/timeline and tiny attachment resource paths pass. The original official property case improves31→15 messages but still fails. ETC2/EAC remains private and numerically unqualified: its sRGB case fails; compression features are not promoted. No rendering, full format matrix or conformance claim follows. See `../VULKAN11_COMPUTE_STATUS.md`.

`0045-radish-depth-attachment-host.patch` and then `0045-radish-mixed-depth-layouts.patch` follow the ac2a integrated stack. They add D16/D32/S8 attachment host objects and core1.1 maintenance2 layouts, qualified by six bounded host/fenced-transition cases. They do not promote framebuffer sample masks or claim initialized sample, combined depth/stencil, or rendering support. See `../results/depth-host-layouts-0045.json`.

Apply `0045-radish-combined-d24s8.patch`, then `0045-radish-compressed-blit-src-query.patch` after the c2aa depth/layout stack. Exact9407 reconstruction and initialized single-sample evidence are in `../results/d24s8-public-0045.json`. The private image-transfer header changes; olderd903 diagnostic consumers require their frozen headers. No compression feature/sample-mask/rendering promotion.
