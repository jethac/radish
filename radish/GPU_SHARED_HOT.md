# Shared-hot convolution diagnostic

Checkpoint 0041 compares the accepted tiled convolution with a synthetic kernel
that loads one shared-memory tile and repeats its arithmetic. Mean GPU elapsed
falls **7.213085250 → 5.417721000 ms (24.890379%)**. This is evidence about two
complete implementations, not a production convolution change or an isolated
measurement of memory or barrier cost. Model performance remains **35.730754 s**
for the accepted fixed warm core; Vulkan features and conformance status are unchanged.

## Scope and compiler evidence

The baseline uses the unchanged builtin planar shader for 128 input/output channels, kernel width 11 and dilation 1,
width 213, with specializations `[11,128,128,1,1]` and input pitch 268. The synthetic
kernel loads 32 reduction rows once, then executes 44 ordered repetitions.
Both issue 92,274,688 FP32 operations: 1024 invocations, 32 scalar accumulation
chains and 1408 updates per chain. Both use local size 16×8×1, grid 4×2×1, eight
one-group chunks and one submit. Every EXEC, wait and cache flush is retained.

| Actual native program | Encoded instructions through END | Native length | Inner-loop encoded / expanded slots |
|---|---:|---:|---:|
| Accepted planar baseline | 431 | 27 | 47 / 55 |
| Original shared-hot | 394 | 25 | 50 / 59 |

Both retain 32 scalar MADs and three vec4 shared loads per K update, 16 full and
zero half registers, 16 KiB shared memory, four loops and no private memory.
The builtin baseline native is byte-identical to the accepted program. Counts
end at END; native length is the driver's rounded program-length field.

**Both source variants failed the original no-hot-MOV admission rule.** The
original shader keeps an output-coordinate value live through the reduction;
its shared load overwrites that register and forces a save/restore pair per
update, or **2816 extra MOVs per invocation**. A single equivalent output-index
reformulation failed to remove them. Both compile-only rejections remain
recorded, and source tuning stopped.

Before any candidate timing, the diagnostic scope was revised to measure the
original implementation with these moves included. The added `precise`
accumulator also remains disclosed: final NIR preserves exact unfused MADs,
but register placement and scheduling differ. The two kernels use different
inputs and have different expected outputs. Their gap cannot isolate tile
refill, addressing, barriers, physical traffic, shared-memory latency or bank
costs, and the extra moves do not establish a conservative quantitative bound.

The new default-off class requires `RADISH_GPU_ELAPSED=1`, `RADISH_PROFILE=1`,
`RADISH_GPU_ELAPSED_CALIBRATION=shared-hot` and tiled batch 8, plus exact A506,
native length 25, register, shared-memory, loop, descriptor and direct-dispatch geometry
checks. It changes neither the normal model batching allowlist nor the accepted
baseline timer. These resource checks do not establish shader semantic identity;
the frozen source/native review provides that evidence. The sixteen-sample cap
and existing submission/fault handling remain intact.

## Balanced result and correctness

Twelve calls use `[baseline, shared-hot, shared-hot, baseline]` three times.
The first four warm both programs; the remaining eight form four balanced pairs.
All output downloads follow all twelve compute waits.

| Mean of four measured calls per program | Baseline | Shared-hot |
|---|---:|---:|
| GPU queued elapsed | 7.213085 ms | 5.417721 ms |
| Host submit interval | 8.055620 ms | 6.067568 ms |
| Host record/submit/wait call | 8.320751 ms | 6.336280 ms |
| Issued work / mean GPU interval | 12.792680 GFLOP/s | 17.032012 GFLOP/s |

The four pair reductions are 24.865127%, 24.819301%, 24.837629% and 25.039097%.
They meet the revised prospective exploratory criterion: an absolute mean gap
of at least 5% with all four pairs in the same direction. This does not reverse
the strict compiler rejections or accept a model optimization. The rates are
achieved workload observations, not hardware ceilings or measured occupancy.
GPU intervals include the retained command work; host whole-call timing excludes
uploads, poison, deferred downloads, references and file writes.

All **327,168 output scalars** match independent integer/double oracles exactly.
Payloads match bit-for-bit within each kernel; cross-kernel identity is neither
expected nor asserted. Twelve distinct destinations are poisoned before the
sequence. All 48 output guards, five actual GPU input readbacks and their 20 guards
pass. The 22 raw files total 3,124,576 bytes and are saved before reference checks.
Twelve valid timer samples correspond to 96 chunks and 12 submits. Compile-only
returns before probe tensors, allocators, uploads or VkCompute work; existing
ncnn device initialization may submit dummy work.

Peak memory is 62.8 MiB with zero swap; service time is 1.470 s, no new GPU fault
is recorded and both services remain active. GPU temperature is 32.9°C before
and after. Whole-window residency increments are 132 ms at 650 MHz and 268 ms at
19.2 MHz, with two transitions. They include non-kernel work and cannot assign
frequency to individual calls. Four measured calls per kernel do not establish
sustained performance or statistical confidence.

## Evidence and reproduction

The original strict rejections are preserved under
`shared-hot-helper-review-dzpthdv6/{actual-first,coordinate-actual}/`.
The revised decision preceded timing in
`shared-hot-implementation-0041-cxsz2375/REVISED_DIAGNOSTIC.md`.
The full raw/native report is
`shared-hot-helper-review-dzpthdv6/exploratory/REPORT.md` (SHA256 1c641cbe…68c69);
conditions are independently checked in
`shared-hot-actual-conditions-ztqa_crm/REPORT.txt` (3c5378ac…eee60).

Hardware used private driver 49d84db0…76833, helper 09ffd977…916a8 and runner 73ef23d4…87aa9.
The canonical 0041 host build produces ICD f9610658…bb096 from the same queue
source bd9d79a6…2e5d5; that linked artifact was not the measured private ICD.
The optional public helper is byte-identical to the measured binary. The portable
runner changes only TOP discovery and has source/host checks, not a separate
tablet run. The exact public analyzer passes on the frozen capture and reproduces
the private analysis JSON. The global device ICD remains 0032.

Apply cumulative 0041 to the pinned Mesa base instead of 0040 or another cumulative
patch. All 14 files reconstruct exactly; only the 28-line diagnostic gate differs
from 0040. Rejected 0038/0039 changes are excluded. The optional target leaves
default builds untouched. Select a fresh label, keep identical assets for compile
and probe, and retain the shared absolute lock across checkouts.

```bash
export RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock
cmake --build "$RADISH_WORK/build-ncnn" --target radish-gpu-shared-hot -j2
LABEL=shared-hot-0041-local
DRIVER="$RADISH_WORK/build/src/freedreno/radish/libvulkan_radish.so"
PROBE="$RADISH_WORK/build-ncnn/radish-gpu-shared-hot"
DRIVER_SHA=f9610658f5a6d207a120f8bbd331bb7167c1d53550de344b94567042c2cbb096
PROBE_SHA=09ffd9774cf7ead1de8617b9300deed77a4de9a107db2d215ae302c2524916a8
args=(--driver "$DRIVER" --driver-sha256 "$DRIVER_SHA"
      --probe "$PROBE" --probe-sha256 "$PROBE_SHA")
python3 radish/scripts/device-gpu-shared-hot.py compile "$LABEL" "${args[@]}" --check-only
(
    exec 9>"$RADISH_DEVICE_LOCK"
    flock -w1800 9
    python3 radish/scripts/device-gpu-shared-hot.py compile "$LABEL" "${args[@]}"
)
# Review actual native programs under the revised diagnostic scope before probe.
(
    exec 9>"$RADISH_DEVICE_LOCK"
    flock -w1800 9
    python3 radish/scripts/device-gpu-shared-hot.py probe "$LABEL" "${args[@]}"
)
python3 radish/scripts/analyze-gpu-shared-hot.py \
    "$RADISH_WORK/gpu-shared-hot-$LABEL/probe/run.log" \
    "$RADISH_WORK/gpu-shared-hot-$LABEL/probe/raw" \
    "$RADISH_WORK/gpu-shared-hot-$LABEL/analysis.json"
```

Limits remain 384 MiB memory, 128 MiB swap and 90 seconds with live-service/fault
checks and own-unit cleanup. Fresh staging reserves the immutable payload, all
raw outputs, metadata and 64 MiB free disk. No global driver replacement is needed.
Cumulative patch SHA256:
`416045c8ef3b572abf128ad21b432673f8f35bc37989507b0fcd70e18577d2d5`.
The source helper is a1e1db98…a16dd; analyzer 1eb7c895…42e82.
