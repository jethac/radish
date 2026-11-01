# GPU elapsed measurement

Checkpoint 0036 adds a default-off diagnostic for one accepted A506 convolution.
It measures elapsed time between GPU packets, enabling the next arithmetic and
bandwidth measurements. It does not expose Vulkan timestamp queries or establish
GPU saturation. The latest full-model result remains 35.730754 s warm with 0035.

## Qualified result

The fixed FP32 case is C128 → C128, width 213, kernel 11, dilation 1, padding 5
and bias. Four warmups precede eight balanced OFF/ON measurements in one process.
Both modes use the same shader, tensor layout and math dispatch geometry.

| Mean of four measured runs | Diagnostic OFF | Diagnostic ON |
| --- | ---: | ---: |
| Math submit through fence/fault check | 8.114958 ms | 7.889763 ms |
| Complete forward, including output download | 11.380150 ms | 11.345424 ms |
| GPU queued elapsed | — | **7.160924 ms** |

Measured GPU samples are 7.148020, 7.169218, 7.175104 and 7.151354 ms. The
prospective perturbation budget passes: neither ON mean exceeds OFF by 5%, and
neither timing scope has four adjacent pairs all exceeding 5%. These small
samples do not establish a speedup or zero instrumentation overhead.

The GPU interval is 90.762% of the matching host submit interval. The remaining
0.728839 ms includes excluded GPU packets, queue/fence work and host completion;
it is not a measurement of removable CPU overhead. The case performs 76,775,424
useful versus 92,274,688 issued multiply/add FLOPs, yielding **10.721 useful /
12.886 issued GFLOP/s**. Its short spatial tail uses 83.203% of issued arithmetic.
This differs from the full model's aggregate 1.214% padding waste.

All 327,168 output values across twelve forwards pass the unchanged double
reference gate, `2e-5 + 2e-5 * abs(reference)`. Every output is bit-identical to
OFF run zero, and actual GPU input readback preserves every byte. Each math
command retains eight chunks and one submit; the complete capture retains
38 dispatches, 188 chunks and 38 submits. Six ON samples are valid; OFF,
padding and conversion commands emit none.

The process peaks at 62.5 MiB with zero swap and runs for 1.682 s, including
compilation and validation. Both live services remain active; no new GPU fault
is recorded. GPU temperature is 32.9 → 33.2 °C. Whole-capture DVFS counters
include five transitions and cannot assign a frequency to individual samples.
Long model shapes, sustained behavior, attainable arithmetic throughput,
physical memory bandwidth and actual occupancy remain unmeasured.

## Diagnostic contract

Apply cumulative Mesa patch `0036-radish-gpu-elapsed-cumulative.patch` to base
`ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230`, instead of 0032 or earlier cumulative
patches. Only `rd_queue.c` differs from 0032. Exact `RADISH_GPU_ELAPSED=1` and
`RADISH_PROFILE=1` select the diagnostic, evaluated at dispatch execution.

Eligibility requires direct A506 work, grid 4×2×1, local 16×8×1, four loops,
barrier, 16 KiB shared memory, no private memory, at most 16 full registers and
no half registers, native length 27, four descriptors, tile 1×1×1 and batch 8.
Resource matching alone does not establish shader identity; the pinned helper
uses the accepted built-in ncnn convolution. Its actual 431 encoded instructions
through END match the accepted native program exactly.

Each selected attempt owns a 4 KiB sample BO. Two `RB_DONE_TS` event writes and
one trailing WFI add eleven ring dwords without changing existing chunk, submit,
restore, program, descriptor, wait or flush sequences. Start follows the first
restore; stop follows the eighth existing flush/wait. Thus the interval includes
eight execution/state/flush sequences and seven later restores. It excludes the
first restore, pre-start queue delay, final WFI/fence and CPU readback. Scheduling
or preemption can occur inside it; this is not ALU busy time.

The existing bounded fence and fault check precedes nonblocking CPU readback.
Both guard words and all four poisoned timestamp halves must validate. Positive
ordered timestamps, a five-second bound and host-interval sanity are required.
Conversion uses the existing 19.2 MHz always-on counter conversion, independent
of GPU DVFS. Each process permits at most sixteen attempts and one bounded line
per attempt. Diagnostic allocation/read/data failure invalidates its sample;
actual compute failure retains the existing device-loss behavior. Disabled,
ineligible and capped work adds no sample allocation or GPU packets.

## Reproduce

Build the diagnostic driver with the existing Mesa procedure. In the configured
ncnn build, explicitly build the optional helper:

```bash
cmake --build "$RADISH_WORK/build-ncnn" --target radish-gpu-elapsed
DRIVER="$RADISH_WORK/build/src/freedreno/radish/libvulkan_radish.so"
PROBE="$RADISH_WORK/build-ncnn/radish-gpu-elapsed"
DRIVER_SHA=$(sha256sum "$DRIVER" | cut -d' ' -f1)
PROBE_SHA=$(sha256sum "$PROBE" | cut -d' ' -f1)
LABEL=qualification-NEW
export RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock
exec 9>"$RADISH_DEVICE_LOCK"
flock -w 1800 9
python3 radish/scripts/device-gpu-elapsed.py compile "$LABEL" \
  --driver "$DRIVER" --driver-sha256 "$DRIVER_SHA" \
  --probe "$PROBE" --probe-sha256 "$PROBE_SHA" --lock-fd 9
flock -u 9
```

Use the same absolute lock as every other checkout accessing the tablet. Review
`$RADISH_WORK/gpu-elapsed-$LABEL/compile/run.log` for the resources and native
program above. The compile receipt proves process success; resource review is
separate. Then, in the same shell with unchanged paths and hashes:

```bash
flock -w 1800 9
python3 radish/scripts/device-gpu-elapsed.py probe "$LABEL" \
  --driver "$DRIVER" --driver-sha256 "$DRIVER_SHA" \
  --probe "$PROBE" --probe-sha256 "$PROBE_SHA" --lock-fd 9
flock -u 9
python3 radish/scripts/analyze-gpu-elapsed.py \
  "$RADISH_WORK/gpu-elapsed-$LABEL/probe/run.log" \
  "$RADISH_WORK/gpu-elapsed-$LABEL/probe/raw" \
  "$RADISH_WORK/gpu-elapsed-$LABEL/analysis.json"
```

Choose a fresh label for another execution. The runner stages an immutable
private ICD/driver/loader and checks hashes, services, faults, memory, storage
and thermal evidence. It keeps all twelve outputs, both input captures, logs
and failure evidence. No global device driver is replaced. `--check-only`
verifies host pins without device actions. `RADISH_SERIAL` selects the tablet;
the loader must match the pinned build, optionally selected with `--loader`.
The helper checks numerical output; the separate analyzer checks timestamps,
profiles and the interpretation budget. Preserve both results.

The analyzer allows one observed stdout/stderr interleaving: the completed
initial conversion's stderr profile can print after the first stdout run marker.
It still requires the exact global profile sequence and per-run math signatures.
The original parser failure and narrow correction are retained in the evidence.

## Provenance and next work

The hardware capture used the original private runner; the portable label/path
adaptation has source review and host checks. The public CMake target builds the
exact tested helper bytes. The canonical Mesa rebuild has the same reviewed
queue source, but its binary was not the privately staged binary in this capture.
The installed private global ICD and existing runtime captures retain 0032.

- Cumulative patch: `d5c54cca17e20090166c5b85a0763f8727559b13d5eadc4cff9bcea4e0ae8694`.
- Queue source: `82ae2521b15b6b6a262eb8107bea2c2f9fddf76b17b31643e55e6714f91557f3`.
- Tested ICD: `4e47924331062e88e1bddfab8afb374a5e36faa84e51b66b36ed82adb2e87cfe`.
- Canonical rebuild: `940a03291193ad411a0624d9955f395c09350a391ba3ca00f6aab48c5a7648de`.
- Helper: `683a663fea4eb8ca0d191dd434f928acdfd271b8dd5ad383e3bf1056077093f7`.
- Raw run log: `6004507a6d1629dbde6f0686bd2b91beea91ddc44ee6813aeea0d7db0aae24ec`.

Private evidence is under `WORK/gpu-elapsed-probe-private-ipv0_4vm/`,
`gpu-elapsed-review-0036-e2_1iyzb/` and
`gpu-elapsed-implementation-0036-v7o7qye9/`. The reviewed actual report is
`ACTUAL-REPORT.txt`, SHA `8f0734711d16061dd638f8ca24750d0be0574cda4ce58dfe3757b0fd1f184269`.

Next are independent FP32 arithmetic and representative vec4-copy calibrations
using the normal non-barrier scheduling path, followed by a change aimed at a
measured dominant loss. Achieved calibration rates will be lower bounds on
attainable throughput, not universal hardware ceilings. CPU crossover and
full GPU efficiency remain open requirements in `GOAL.md`.
