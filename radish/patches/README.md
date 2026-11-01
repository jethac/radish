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
| `0014-ncnn-conv1d-a5xx-tiled.patch` | Current ncnn stack including 0010/0011/0013 | Opt-in FP32 64x64 tile, local16x8, 16 KiB shared; revised 16-full/0-half shader passes all 25984 operator outputs; no accepted model speed win |
| `0015-ncnn-radish-mixed-storage.patch` | Same ncnn base plus 0006 | Opt-in GPU storage conversions; FP32 norm islands remove diagnosed NaNs, but packed full core and waveform accuracy still fail |
| `0016-radish-register-budget-cumulative.patch` | Same Mesa base as 0001 | Historical register-budget driver, retained in0022; includes0012 and rejects combined full/half register footprints beyond the measured A506 budget before shader upload |
| `0017-ncnn-conv1d-shared-a-transpose.patch` | After 0014 in the ordered Conv1D stack | Both builds/operator/full fused FP32 core/waveform pass; 76.534312 s first/74.317956 s warm (one warm), versus same-graph CPU 32.079044 s (two warm); GPU remains slower |
| `0018-ncnn-conv1d-tiled-dilation.patch` | After 0017 in the ordered Conv1D stack | Extends only dilation1/3/5; operator/full fused FP32 core/waveform pass; 69.862470 s first/69.678044 s warm, 416.7 MiB/zero swap, no faults |
| `0019-ncnn-radish-reshape-alias.patch` | Same ncnn base; independent Reshape file | Default-off exact Radish FP32 pack4 rank3→rank2 output alias; original input copies retained; builds/operator/full-core gates pass, output bit-identical, 67.138187 s first/66.919494 s warm. Earlier bidirectional variant in bf4d98f is rejected at 109.107981 s warm |
| `0020-ncnn-conv1d-tiled32.patch` | After 0018 in the ordered Conv1D stack | Default-off local8x4, tile64x32, 12 KiB shared; builds/compiler/operator/full-core accuracy pass, output byte-identical; 101.354178 s first/100.467084 s warm versus accepted 66.919494 s warm; 414.8 MiB plus 52 MiB swap; rejected for performance |
| `0021-ncnn-conv1d-tile32x32.patch` | After 0020 in the ordered Conv1D stack | Revises the same default-off TILED32 path to tile32x32, eight accumulators, 8 KiB shared; builds/all20 variants/operator/full-core accuracy pass; output byte-identical, first 138.633996 s versus accepted first 67.138187 s, peak 425.2 MiB/zero swap/no faults; rejected for performance, no warm repeat; accepted64x64 path unchanged |
| `0022-radish-tiled-batching-cumulative.patch` | Same Mesa base as 0001; replaces 0016 | Prior accepted driver; optional 2/4 sequential single-group tiled chunks per submit, unchanged waits/flushes and register guard; full core 56.013717 s warm, byte-identical, 428.5 MiB/zero swap/no faults; fresh T78 runtime passes |
| `0023-radish-tiled-batch8-cumulative.patch` | Same Mesa base as 0001; replaces 0022 | Current driver; adds batch8 under unchanged 0022 resource gate; build/reconstruction/operator/full-core checks pass, Snake 49.266772/49.429406 s first/warm, byte-identical, 419 MiB/zero swap/no faults; fresh T213 synthesis 63.528396 s, all core/waveform values pass, 458 MiB/zero swap; runtime default8 after validation, driver/operator/model1, no16 |
| `0024-ncnn-conv1d-planar-input.patch` | After 0021 in the ordered Conv1D stack | Default-off PLANAR layout for eligible tiled64; one initialized padding/transpose workspace replaces existing padding, original math order/output retained; builds/seven-file reconstruction/all20 compiler variants/padded dilation1/3/5 pass. Fixed core48.738814/48.863090 s first/warm, all562342 outputs byte-identical, 420.1 MiB/zero swap/no faults; single warm pair1.1457% faster, fresh runtime pending |
| `0025-radish-planar-batching-cumulative.patch` | Same Mesa base as 0001; alternative to 0023 | Candidate adds only native instrlen31 beside36/37 in existing tiled-batch resource gate; build/all14-file reconstruction, padded operators and full fixed core pass; all53 math commands retain batch8 schedule. Remaining conditions/defaults and ncnn K bound unchanged; PLANAR remains opt-in |
| `0026-ncnn-planar-pad-local64.patch` | After0024 in the ordered Conv1D stack | Four reviewed host changes: planar prepass local8x4→8x8, two pipeline guards and group-count formula; shader bodies/defaults unchanged. Reconstruction/builds/compile/focused/full-core gates pass, first48.370236/warm46.932926 s, identical outputs,418.4 MiB/zero swap; single warm3.9501% below0024. Fresh T78/T213 passes31.890578/63.320730 s, all core/waveform values, zero swap/no faults; PLANAR default0 |

To reconstruct the accepted driver checkpoint, make a separate checkout/worktree at the
Mesa base, read its `AGENTS.md`, and apply 0023 with `git apply` instead of
0001/0003/0004/0007/0009/0012/0016/0022. Set
`RADISH_MESA` to that checkout and run `../scripts/build.sh`. Preserve the AC8
checkout and use the shared scratch disk for source/build outputs. The existing
sysroot/build environment paths and private loader procedure are in `NOTES.md`.

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
0002 adaptation, then applies/verifies 0006, 0008, 0010, 0011 and candidates 0013–0015/0017/0018/0020/0021/0024/0026, plus independent 0019.
The first candidate build stopped at overlapping earlier-patch verification.
The build script accepts exact ordered 0010/0011/0013/0014/0017/0018/0020/0021/0024/0026 Conv1D prefixes
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
Vulkan1.0 GLSL/SPIR-V, AArch64 syntax and canonical/SIMPLEOMP/runtime builds.
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
```
