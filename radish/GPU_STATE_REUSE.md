# Rejected experiment: tiled state reuse (0038)

The fixed A506 experiment is numerically correct but fails its prospective
performance criterion. Mean GPU elapsed time changes **7.195364250 →
7.165351000 ms**, a **0.417119%** reduction. All four balanced pairs improve,
but each is below 0.63%; the preselected requirement was at least **5% mean
GPU reduction and all four pairs improving**. Keep 0037 as the recommended
driver. No full-model test, expanded gate or rollout follows this result.
The experiment is preserved as an explicitly rejected optional incremental
patch so it can be inspected and reproduced.

## Exact scope and result

`RADISH_TILED_STATE_REUSE=1` skips seven repeated restore/program/descriptor
setups after the first chunk within one existing eight-chunk submit. The
skipped restore includes its UCHE invalidation; this is part of the tested
mechanism, not merely fewer register writes. Every restore-site WFI, per-chunk
constant/base update, EXEC, post-EXEC WFI, cache flush and memory wait remains.
There is no state reuse across submissions. The measured fixture is the
accepted C128/K11/dilation1/W213 padded case. The opt-in guard checks native27,
local16x8, grid4x2, 16 full/zero half registers, 16 KiB shared memory and four
loops, together with descriptor and batching constraints. Those checks do not
establish shader semantic identity; the pinned helper and actual compiler
audit identify the measured workload.

Timestamping is enabled for all twelve calls. Reuse alone follows four warmups
and eight measured calls in the original balanced schedule. The actual driver
skip counter is seven on each of six ON samples and absent on every OFF
sample. It is not inferred from the requested environment value. All eight
compiled programs retain their accepted final NIR/native instructions/stats.

| Measured pair, OFF / ON index | OFF GPU ms | ON GPU ms | GPU reduction | Host math-submit reduction | Whole-forward reduction |
|---|---:|---:|---:|---:|---:|
| 4 / 5 | 7.202031 | 7.174687 | 0.379671% | 7.053043% | 2.387361% |
| 7 / 6 | 7.187343 | 7.156197 | 0.433345% | 2.424628% | 4.194289% |
| 8 / 9 | 7.188958 | 7.172239 | 0.232565% | -0.658060% | -1.258803% |
| 11 / 10 | 7.203125 | 7.158281 | 0.622563% | -7.097343% | -0.020609% |

Positive reductions mean faster ON. Mean host math submit-to-completion is
**8.055321250 → 8.008771500 ms** (0.577876%); mean complete forward is
**11.427674250 → 11.277778250 ms** (1.311693%). Whole-forward includes
construction, output poison/flush, submit, wait and download. GPU timestamps
exclude the host poison but include retained command/state/wait/flush work.
The mixed host pair directions do not replace the GPU criterion. This bounded
sequence does not establish a confidence interval or model benefit.

## Correctness and conditions

Before each submit, the helper fills the actual mapped GPU output with a
changing NaN pattern and flushes it. Twelve ordered poison markers and the
completed manifest are mandatory. This closes a stale-output gap in the
initial, built-but-never-executed helper: identical inputs and recycled output
memory alone could hide a skipped write. No GPU commands or shader instructions
were added for the poison.

All **327,168 output values** pass the unchanged full double reference
(`2e-5 + 2e-5*abs(reference)`), with maximum absolute error 3.4222612e-5. Every
run matches OFF run0 byte for byte, every output is finite, and actual GPU input
readback matches the original pattern. All fourteen raw files also match the
earlier 0036 capture. Exact work is **38 dispatches / 188 chunks / 38 submits**,
with twelve valid timestamps. The frozen analyzer passed unchanged; there was
no parser correction or discarded observation.

Service time is 1.698 s, CPU time 1.550 s, peak memory 62.5 MiB and zero swap.
Both live services remain active and there are no new GPU faults. GPU thermal
endpoints are 32.2→33.2°C. Whole-capture clock counters record five transitions
and residency deltas of 472/52/56/168 ms at 19.2/133.3/400/650 MHz. These broad
window counters cannot assign a frequency to individual calls.

Evidence under WORK: `state-reuse-0038-first/probe/`,
`state-reuse-analysis-0038-i58lyilx/actual-analysis.json`, and independent
`state-reuse-review-0038-_6i9cj2e/ACTUAL-REJECTION.txt`
(SHA256 `14dda333…0ea91a`). Executed helper source is `cb164c06…4ab2`,
binary `2510a415…6024d`, driver `bd2a672a…e1a0f`, and private runner
`3b1b87f8…`. Canonical Mesa remains at 0037; the globally staged device ICD remains
0032. No new model or Vulkan conformance claim is made.

Before this run, only the completed 0037 v2 device raw/results were archived
after their audit. The original host capture remains in
`gpu-calibration-deferred-dkyybh_d/probe-first/raw/`, with a second verified copy,
metadata and restore script in
`state-reuse-implementation-0038-4n5ll5k4/device-raw-v2-archive/`. Both services
remained active; models and other captures were unchanged.

## Optional reproduction of the rejected candidate

Read Mesa's local `AGENTS.md` and the pinned-base instructions in
`patches/README.md`. In a **separate Mesa checkout** at that base, apply
`0037-radish-gpu-calibration-cumulative.patch`, then
`0038-radish-tiled-state-reuse.patch`. The latter is incremental over 0037,
SHA256 `0d3a34785a96f50aabba75529372d83838a71e22ae5613bc350a33cd4d37a72e`.
Build to a separate artifact directory; it is not part of the recommended
canonical driver stack.

The optional `radish-state-reuse` target is excluded from normal builds. Its
canonical host build passes and produces the exact device-tested helper binary,
SHA256 `2510a415061b59ad2e763dc0e96083404a0db8de76e9f747646165249176024d`
(10,951,784 bytes). Canonical queue/driver remain unchanged at 0037
(`2c3f9ed1…85f3a` / `2f4fde0e…1b1682`). The portable wrapper differs
from the executed private runner only in deriving the checkout root from its
file location; host syntax and pin checks cover that adaptation. It has not
been separately run on the tablet. The analyzer is exactly the source that
passed the frozen actual capture.

```bash
export RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock
cmake --build "$RADISH_WORK/build-ncnn" --target radish-state-reuse -j2
# DRIVER/DRIVER_SHA select the separate rejected-candidate build and recorded hash.
# PROBE/PROBE_SHA select the optional helper build and its recorded hash.
LABEL=state-reuse-history
args=(--driver "$DRIVER" --driver-sha256 "$DRIVER_SHA"
      --probe "$PROBE" --probe-sha256 "$PROBE_SHA")
python3 radish/scripts/device-state-reuse.py compile "$LABEL" "${args[@]}" --check-only
(
    exec 9>"$RADISH_DEVICE_LOCK"
    flock -w1800 9
    python3 radish/scripts/device-state-reuse.py compile "$LABEL" "${args[@]}"
)
# Review actual compiler identity before running tensors.
(
    exec 9>"$RADISH_DEVICE_LOCK"
    flock -w1800 9
    python3 radish/scripts/device-state-reuse.py probe "$LABEL" "${args[@]}"
)
python3 radish/scripts/analyze-state-reuse.py \
    "$RADISH_WORK/state-reuse-$LABEL/probe/run.log" \
    "$RADISH_WORK/state-reuse-$LABEL/probe/raw" \
    "$RADISH_WORK/state-reuse-$LABEL/analysis.json"
```

The wrapper preserves the shared lock, immutable compile/probe receipt, private
ICD and payload, service/fault checks, 384 MiB memory/128 MiB swap caps and
90-second service limit. It does not replace the global ICD. Performance
rejection still produces a complete functional report; the preselected decision
criterion is recorded separately from numerical/protocol success.
