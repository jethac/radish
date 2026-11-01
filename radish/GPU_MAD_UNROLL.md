# A506 MAD loop unrolling calibration

Checkpoint 0040 adds one default-off elapsed diagnostic class to 0037. It
compares the unchanged arithmetic calibration with four updates per loop
iteration. Both execute the same 268,435,456 FP32 operations and preserve the
same exact dyadic result. This is a calibration experiment; model shaders,
dispatch policies and Vulkan feature support are unchanged.

## Work and actual native admission

Each shader has 1024 invocations and 32 independent scalar accumulation chains
per invocation. The baseline runs 4096 iterations. The candidate performs four
ordered copies of the update and runs 1024 iterations. Both use local size 16×8×1,
grid 4×2×1, one EXEC command with eight groups, one chunk and one submit.

| Actual program | Encoded instructions through END | Native length | Scalar MADs per loop | Expanded hot-loop slots |
|---|---:|---:|---:|---:|
| Original `mad` | 117 | 8 | 32 | 42 |
| `mad-unroll4` | 178 | 12 | 128 | 138 |

Both compile to 16 full/zero half registers, one loop, no shared/private memory,
and no hot-loop moves or memory accesses. The original native program is
byte-identical to 0037; native instructions outside the candidate's loop are
also unchanged. Every chain retains its original scale/step operands and four
ordered updates. Counts end at END; native length is the driver's rounded
program-length field. Comparing four original iterations with one unrolled
iteration reduces expanded slots from 168 to 138. These are instruction counts,
not measured issue throughput or a predicted speedup. The compiler's eight
modeled waves do not establish active occupancy.

The diagnostic selects `mad-unroll4` only with `RADISH_GPU_ELAPSED=1`,
`RADISH_PROFILE=1` and its exact per-call calibration selector. Native length 12,
maximum full-register index 15, three descriptors and all previous A506/local/grid/loop/memory
qualifiers are required. This resource class is not shader semantic identity;
the frozen helper/native review provides that evidence. The old MAD, copy and
convolution classes, sixteen-sample cap and submission policy are unchanged.

## Paired result

Mean measured GPU elapsed is **12.597682 → 11.916470750 ms**, a
**5.407433% reduction**. Dividing the unchanged work by the mean candidate
interval gives **22.526423 GFLOP/s**. The four adjacent pair reductions are
5.116835%, 6.097896%, 5.374200% and 5.033579%, meeting the prospective at-least-5%
mean reduction plus all-four-pairs-positive criterion.

| Mean of four measured calls per program | Baseline | Unrolled |
|---|---:|---:|
| GPU queued elapsed | 12.597682 ms | 11.916471 ms |
| Host submit interval | 13.535760 ms | 12.725342 ms |
| Host record/submit/wait call | 13.842922 ms | 13.083741 ms |

The 17.857% reduction in expanded instruction slots yields only 5.407% lower
elapsed time. The larger loop body produces a real improvement; it does not
isolate control-overhead cost or identify the remaining bottleneck, issue rate,
dependency stalls or physical occupancy. No model shader was changed or benchmarked. The accepted full-core
measurement remains **35.730754 s**.

The fixed sequence is `[baseline, unroll, unroll, baseline]` three times. The
first four calls warm both programs; eight measured calls form four adjacent
baseline/candidate pairs. Both programs have timestamps enabled. All twelve
compute submit/waits finish before any CPU output download. The host field
`record_submit_wait_seconds` excludes uploads, poison, downloads, references
and file writes. GPU elapsed includes retained packet work and timestamp
instrumentation; it is not an ALU-busy counter. A rate from this short fixed
workload is an achieved lower bound, not a ceiling or sustained model speedup.

## Full correctness and evidence

Twelve disjoint GPU destinations are poisoned before the sequence and retain
different finite guard tags. All 393,216 output scalars match the original
integer/double dyadic oracle exactly and all twelve payloads are bit-identical.
All 48 output guard lanes and actual GPU seed/coefficient readbacks are checked.
The sixteen raw files total 1,835,296 bytes. They are written before reference
validation; only a completed successful manifest reports all gates passed.
Compile-only stops before probe tensors, allocators, uploads or VkCompute work;
ncnn device initialization may submit its existing dummy work.

The private source/design is `mad-unroll4-private-4rjzg9pv/`; native admission is
`mad-unroll4-review-loi8l_ck/native/admission.json`. Actual hardware uses private
runner `51a339d2…ea39e`. The portable runner changes only TOP discovery and has
source review and host checks; it has not separately run on the tablet.

Actual capture `gpu-mad-unroll-0040-first/probe/` passes the independent raw,
timestamp and profile analyzer. Peak memory is 61 MiB with zero swap; service
time is 0.992 s, no new GPU fault is recorded and both services remain active.
GPU temperature changes 32.2→32.9°C. Two whole-window clock transitions are
recorded, with residency increments of 276 ms at 19.2 MHz and 200 ms at 650 MHz.
This includes idle/staging work and cannot assign frequency to individual calls.
Four measured calls per program do not establish sustained throughput or
statistical confidence. Full private numerical/native evidence is in
`mad-unroll4-review-loi8l_ck/REPORT.md` (SHA256 `a14ed09f…89544`); the independent conditions review is in
`mad-unroll-actual-review-xg1c008q/ACTUAL-REPORT.txt`
(SHA256 `49bad5af…e2119`).

The measured private ICD is `30e19d89…8758d`. Canonical 0040 builds pass with
ICD `366948ba…51a8e`, using the same reviewed queue source `8aff704c…32aa0`.
The public optional helper is byte-identical to the measured binary. The
canonical ICD itself has host-build evidence; these device timings belong to
the separately pinned private artifact. The global device ICD remains 0032.

## Reproduction

Apply cumulative `0040-radish-mad-unroll-calibration-cumulative.patch` to the
pinned Mesa base instead of an earlier cumulative patch. It is 0037 plus the
three-line timer selector, with no rejected 0038/0039 modifications. All fourteen
files reconstruct exactly. The optional target is excluded from default builds.
The runner requires the frozen helper hash, a fresh label and identical assets
for its compile and probe phases. Keep the existing absolute shared device lock
when using a mirror checkout. Build and set DRIVER to the separately recorded
0040 artifact before the following commands; no global driver replacement is
required.

```bash
export RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock
cmake --build "$RADISH_WORK/build-ncnn" --target radish-gpu-mad-unroll -j2
LABEL=mad-unroll-0040-local
DRIVER="$RADISH_WORK/build/src/freedreno/radish/libvulkan_radish.so"
PROBE="$RADISH_WORK/build-ncnn/radish-gpu-mad-unroll"
DRIVER_SHA=366948babe207dadc9e34c540d5aa7122176a86b09b75b3a94265f9d89551a8e
PROBE_SHA=3bf41ad9fcbbb52d1e735a1d3c9f97df3a36efcea88dda3cb98e7226636d7b5e
args=(--driver "$DRIVER" --driver-sha256 "$DRIVER_SHA"
      --probe "$PROBE" --probe-sha256 "$PROBE_SHA")
python3 radish/scripts/device-gpu-mad-unroll.py compile "$LABEL" "${args[@]}" --check-only
(
    exec 9>"$RADISH_DEVICE_LOCK"
    flock -w1800 9
    python3 radish/scripts/device-gpu-mad-unroll.py compile "$LABEL" "${args[@]}"
)
# Review both actual native programs before the separate probe phase.
(
    exec 9>"$RADISH_DEVICE_LOCK"
    flock -w1800 9
    python3 radish/scripts/device-gpu-mad-unroll.py probe "$LABEL" "${args[@]}"
)
python3 radish/scripts/analyze-gpu-mad-unroll.py \
    "$RADISH_WORK/gpu-mad-unroll-$LABEL/probe/run.log" \
    "$RADISH_WORK/gpu-mad-unroll-$LABEL/probe/raw" \
    "$RADISH_WORK/gpu-mad-unroll-$LABEL/analysis.json"
```

The existing caps remain 384 MiB memory, 128 MiB swap and 90 seconds, with live
service/fault checks and own-unit cleanup. Fresh staging reserves the immutable
payload, all raw output bytes, metadata and 64 MiB free disk. Evidence is under
`$RADISH_WORK/gpu-mad-unroll-LABEL/`. The original 0037 calibration and rejected
0038/0039 experiments remain separately reproducible.

Cumulative patch SHA256:
`f627d0af11ea9ac77e0bccdc2d8e8aaff972ec500ef8698381004eb3700ee030`.
Helper source `e7ba38f1…73401`; private tested binary `3bf41ad9…d7b5e`.
The three-argument public analyzer checks every raw value, input, guard and
timer/profile relationship. Native resource and arithmetic admission remains
a separate compiler review. The analyzer has passed on the actual frozen capture.
