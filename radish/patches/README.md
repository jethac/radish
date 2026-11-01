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
| `0012-radish-shared32-groups-cumulative.patch` | Same Mesa base as 0001 | Current driver; adds opt-in 1..8 shared32 groups per hardware chunk, default 1; shared/reduction/full-core checks pass |
| `0013-ncnn-conv1d-shared32.patch` | Current ncnn stack including 0010/0011 | Optional exact Radish FP32 shared32 Conv1D GEMM; both builds and K3/K7/K11 device checks pass; full model passes bit-identically but slows to 198.453 s; opt-in candidate rejected for performance |

| `0014-ncnn-conv1d-a5xx-tiled.patch` | Current ncnn stack including 0010/0011/0013 | Opt-in FP32 64x64 output tile, local16x8 and 16 KiB shared memory; source review complete, hardware validation pending |
| `0015-ncnn-radish-mixed-storage.patch` | Same ncnn base plus 0006 | Opt-in explicit FP16/FP32 storage conversions for masked GPU consumers; model validation pending |

To reconstruct the current driver, make a separate checkout/worktree at the
Mesa base, read its `AGENTS.md`, and apply 0012 with `git apply` instead of
0001/0003/0004/0007/0009. Set
`RADISH_MESA` to that checkout and run `../scripts/build.sh`. Preserve the AC8
checkout and use the shared scratch disk for source/build outputs. The existing
sysroot/build environment paths and private loader procedure are in `NOTES.md`.

The pinned ncnn/glslang checkout is separate. `../scripts/build-ncnn.sh` applies
0005 if applicable, accepts an already-applied patch, migrates the exact earlier
0002 adaptation, then applies/verifies 0006, 0008, 0010, 0011 and candidates 0013–0015.
The first candidate build stopped at overlapping earlier-patch verification.
The build script accepts exact ordered 0010/0011/0013/0014 Conv1D prefixes
and 0006/0015 net.cpp prefixes, rejecting unrelated edits. This preserves the
source check despite overlapping patch contexts. Canonical and SIMPLEOMP builds pass with revised 0013. Set
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
ncnn means and two complete model extractions pass at 8. The latest GPU run takes
103.192 s first / 101.275 s warm versus a 32.052 s packed six-thread CPU warm mean;
the performance goal remains open. The GPU warm result contains one iteration.

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
FP32, packing and shape gates preserve existing paths elsewhere. Compile-time
register use, 128-thread execution and complete output accuracy remain to be
validated before enabling it for a model.

Candidate 0015 is disabled unless `RADISH_NCNN_MIXED_STORAGE=1` and the exact
Radish device uses packed FP16 with FP32 arithmetic. It honors each consumer's
storage mask with explicit GPU casts, including equal-pack transitions, and
uploads CPU inputs with the consumer's masked options. This supports planned
FP32 normalization regions; it does not establish correct full-model FP16
inference. `../scripts/keep-kokoro-norms-fp32.py` creates the guarded fixed-fixture
variant: 54 normalization chains, 486 masked layers, and CPU FP32 output
bit-identical to baseline. GPU verification remains pending. See `../NOTES.md`
for the observed zero normalization denominators.

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
8e6879a4c70537160547c302bc4f99c26495f6dcf5727d75bed2db021fd6a8b7  0014-ncnn-conv1d-a5xx-tiled.patch
53edb9f05fea4eb13cc7d489178ecda63bcc9a5febed5502962f185d063dea10  0015-ncnn-radish-mixed-storage.patch
```
