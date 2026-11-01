# A506 arithmetic and copy calibration

Driver checkpoint 0037 extends the default-off elapsed diagnostic to two fixed
FP32 kernels. It measures achieved arithmetic and copy rates on the normal
non-barrier path. These are observed lower bounds, not hardware ceilings,
physical DRAM bandwidth, occupancy measurements or a Kokoro speedup. Model
performance remains at the accepted 0035 checkpoint: 35.730754 s warm full core.

## Fixed workload and actual compiler admission

Both kernels use local16x8x1, grid4x2x1, one EXEC containing eight groups, one
chunk and one submit. They use no barriers, shared or private memory. This
retains the existing non-barrier scheduling policy. Each kind runs two warmups
and three measured calls; all ten samples and outputs are retained.

| Kernel | Work per call | Actual native program |
|---|---|---|
| FP32 MAD | 1024 threads × 32 independent scalar chains × 4096 runtime iterations × two operations = 268,435,456 FLOPs | 117 encoded instructions through END; native length8; 16 full/zero half registers; one loop. Twenty encoded loop MADs expand to 32 scalar MADs, without hot-loop memory or closed-form elimination. |
| Vec4 copy | 13,087,232 bytes read and the same number written; 26,174,464 logical read+write bytes | 33 encoded instructions through END; native length3; four full/zero half registers; one loop, with a true vec4 load and store. |

Instruction counts shown end at END; native length is the driver’s rounded
program-length field. Deferred-readback v2
compiles to exactly the same native programs as v1. Shader source and dispatch
geometry are unchanged. Calibration requires `RADISH_PROFILE=1`,
`RADISH_GPU_ELAPSED=1` and the exact per-dispatch
`RADISH_GPU_ELAPSED_CALIBRATION=mad|copy` selector plus the admitted resource
class; at most sixteen selected batches are sampled per process. The existing
convolution class remains unchanged. The 19.2 MHz timer measures elapsed queued
GPU work, including retained command work and diagnostic overhead; it is not a
shader-clock or ALU-busy counter. This adds no public Vulkan query feature or
API-version claim.

## Measurements and the readback correction

The first capture was numerically correct, but its complete-call timer included
CPU output downloads. Copy calls were separated by 86.54–129.94 ms from the
previous fence completion to the next submit. Whole-call minus profiled-dispatch
time contained 86.58–129.28 ms of CPU readback and surrounding host work; it was
not a separately instrumented memcpy timer. The capture recorded 15 whole-window
GPU clock transitions. Copy GPU intervals varied 5.69–24.44 ms across all five
calls, so v1 is not a sustained-bandwidth reference.

V2 changes only host scheduling: ten compute-only submit/waits finish before
any output is downloaded. All ten poisoned, disjoint destinations stay live.
`record_submit_wait_seconds` excludes the deferred downloads; uploads, reference
work and file writes also remain outside the measured calls. Both captures and
the original source are preserved.

| Capture | MAD measured GPU median (range) | MAD achieved median | Copy measured GPU median (range) | Copy logical read+write median |
|---|---|---|---|---|
| v1, per-call readback | 12.605833 ms (12.590416–12.623385) | 21.294543 GFLOP/s | 24.440104 ms (10.243437–24.442031) | 1.070964 GB/s |
| v2, deferred readback | 12.591666 ms (12.515416–12.688177) | 21.318502 GFLOP/s | 6.085520 ms (5.969062–6.153281) | 4.301106 GB/s |

The rates include diagnostic overhead. The earlier 0036 OFF/ON perturbation
qualification applies to its convolution, not a new matched OFF experiment for
these calibrations. Three measured calls do not establish sustained behavior or
statistical confidence. The arithmetic comparison with convolution also changes
workload and packet geometry: calibration has eight groups in one EXEC, while
the accepted convolution has eight sequential EXECs with intervening state,
wait and flush packets. A rate gap does not isolate its cause.

V2 copy inter-submit gaps are 0.362–0.557 ms, versus 86.54–129.94 ms in v1.
It records three whole-window clock transitions and GPU temperature 32.6→33.5°C.
Endpoint residency/temperature records cannot assign a clock to each kernel or
exclude transient effects. The shorter copy intervals follow removal of a real
readback gap; they do not identify physical DRAM traffic or establish a ceiling.

## Accuracy and evidence

Each capture checks every one of 163,840 MAD output scalars against an exact
dyadic integer/double oracle and every one of 65,436,160 copy output bytes against
an independently generated finite pattern. All 160 output guard bytes have
run-specific tags. The seed, coefficient and copy-source buffers are read back
from the GPU and checked against their originals. Sixteen raw files total
92,528,416 bytes, plus the terminal JSON. Output files precede reference
validation; a successful result is published only after the full checks.

V1 used 305.4 MiB with zero swap; service time was 2.220 s. Both services stayed
active and no new GPU fault was recorded. V2 independently passes all ten valid
timers and the same full raw/input/guard oracles. All sixteen raw files are
byte-identical to v1. V2 uses 303.4 MiB/zero swap,
2.196 s service time, no new GPU faults and both services active.

Private evidence under WORK: `gpu-calibration-review-0037-vw40_8ne/` holds v1
compiler/actual audits; `gpu-calibration-deferred-dkyybh_d/` holds the frozen v2
source, runner and both device phases. The v2 independent audit is
`gpu-calibration-deferred-review-zj15oy0d/ACTUAL-REPORT-v2.txt`
(SHA256 `5ce51be2…de853`); full analysis and conditions are frozen alongside it.

The v1 device raw directory alone was archived after all seventeen files were
rehashed against two verified host copies. Its original host capture remains at
`gpu-calibration-probe-3jtdz6ll/probe-first/raw/`; the additional copy, metadata
and restore script are in
`gpu-calibration-implementation-0037-snn5enhu/device-raw-v1-archive/`.
Models, other captures and the private driver/helper payloads were retained.

## Occupancy and the next bounded experiment

The driver's 2048-full-unit admission limit describes one complete local128
barrier group at 16 full registers. It does **not** establish total physical
register capacity. IR3's register/shared-memory models permit eight modeled
waves here, but neither that estimate nor `max_waves` measures active occupancy.
Keep the existing shared-memory and register guards unchanged.

The next proposed mechanism is default-off reuse of immutable program and
buffer-descriptor state between chunks within an existing tiled submit. Retain
the first chunk's complete setup and every WFI, per-chunk constant/base update,
EXEC, cache flush and memory wait. It is not implemented in this checkpoint.
The earlier X2 experiment halved EXEC chunks without improving math time, so
neither a large gain nor an occupancy explanation is established. A fixed
shader and bounded elapsed A/B measurement should decide whether this mechanism
is useful before model integration.

The follow-up [0038 experiment](GPU_STATE_REUSE.md) passed full poisoned-output
checks but reduced mean GPU time only 0.417%, below its planned 5% gate. It is
retained as rejected history; canonical 0037 remains unchanged.

## Reproduction and pins

The optional `radish-gpu-calibration` target is excluded from default builds.
The public runner uses a fresh safe label, one immutable payload, and requires
compile and probe to have exactly the same helper, driver, loader and ICD pins.
It retains the shared lock, live-service/fault checks, 384 MiB memory/128 MiB swap
caps and 90-second limit. Fresh staging reserves the payload plus all raw output
bytes, metadata allowance and 64 MiB of disk. Run the compile phase first and
review actual native/resource admission before starting the probe.

Canonical driver and optional helper builds pass. The helper binary is
byte-identical to the device-tested v2 binary. The canonical ICD hash differs
from the private measured build despite the identical reviewed queue source;
no new device result is attributed to that canonical artifact. The globally
staged device ICD and model evidence remain on 0032.

```bash
export RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock
cmake --build "$RADISH_WORK/build-ncnn" --target radish-gpu-calibration -j2
LABEL=calibration-0037-local
DRIVER="$RADISH_WORK/build/src/freedreno/radish/libvulkan_radish.so"
PROBE="$RADISH_WORK/build-ncnn/radish-gpu-calibration"
DRIVER_SHA=2f4fde0ee36bdf4441a95dc750d904cab3e2c42ab2ab3c84aca8b903471b1682
PROBE_SHA=e561a2665253a84c6517a246cf70c4a0280a6f4801c8bae042928fcd5f80783b
args=(--driver "$DRIVER" --driver-sha256 "$DRIVER_SHA"
      --probe "$PROBE" --probe-sha256 "$PROBE_SHA")
python3 radish/scripts/device-gpu-calibration.py compile "$LABEL" "${args[@]}" --check-only
(
    exec 9>"$RADISH_DEVICE_LOCK"
    flock -w1800 9
    python3 radish/scripts/device-gpu-calibration.py compile "$LABEL" "${args[@]}"
)
# Review actual compiler admission before the separate probe phase.
(
    exec 9>"$RADISH_DEVICE_LOCK"
    flock -w1800 9
    python3 radish/scripts/device-gpu-calibration.py probe "$LABEL" "${args[@]}"
)
python3 radish/scripts/analyze-gpu-calibration.py \
    "$RADISH_WORK/gpu-calibration-$LABEL/probe/run.log" \
    "$RADISH_WORK/gpu-calibration-$LABEL/probe/raw" \
    "$RADISH_WORK/gpu-calibration-$LABEL/analysis.json"
```

Run from the checkout root with `RADISH_WORK` set to the existing build/artifact
directory. These SHA values identify this checkpoint; a rebuild must use its
separately recorded hashes. `RADISH_SERIAL` and `RADISH_DEVICE_LOCK` overrides
are supported. Evidence is under `$RADISH_WORK/gpu-calibration-LABEL/`. The public analyzer
independently checks all raw bytes and timer/profile relationships; native
register/private-memory/dataflow admission remains a separate compiler review.
The integrated analyzer has passed on the actual frozen v2 capture.

Patch `0037-radish-gpu-calibration-cumulative.patch` SHA256:
`56273f3da2569c3c3c95d877fac5f81fc2bb876cb9f800699efb9659d17f60b6`.
Executed v2 helper source: `c69aefc6…db1d6`; private binary: `e561a266…0783b`;
driver: `393383ce…a2900`; private runner: `7ce69b33…2e233`.
The portable wrapper adaptation has source review, syntax, host pin checks and
patch reconstruction; the hardware evidence used the private runner. It has
not independently been run on the tablet.
