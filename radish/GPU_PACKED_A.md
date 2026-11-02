# Packed-A convolution experiment

## 2026-11-02: nonnegative block shift passes the current full model

Replacing only packed-A `(mb / 4)` with `(mb >> 2)` removes eight encoded instructions from each of the 23 variants; the existing nonnegative block invariant proves equivalence. Nine variants shrink native28→27 and fourteen remain native28. All retain 16 full/no half registers, four loops, 16 KiB shared storage and unchanged floating arithmetic/shared loads/stores. The other 156 compiler records are byte-identical.

On the same qualified explicit robust=false path, first fixed-core extraction changes 35.067822→34.700589 seconds; resident changes **34.333299→33.952417 seconds (1.1094%)**. Full references and accepted output bytes pass twice, with all 589 GPU layers. All 1,520 ordered owners/geometries/loops/chunks/submissions remain identical: 10,809 total submits, 3,294 math. Both runs have zero swap (405.4 versus 406.2 MiB peak); no new faults, both services active.

This is a small sequential observation. Math command wall time saves only 0.171195 seconds of the 0.380882-second resident reduction, while other commands also improve; do not attribute the whole gain to this expression or claim statistical confidence. Cold load changes 11.700→11.892 seconds. Read-only pre/post frequency samples are idle 19.2 MHz and do not establish sustained GPU frequency or utilization. No clock/power settings changed.

[Exact source/native/profile evidence](results/packed-a-mb-shift-0045.json) and the [isolated ncnn branch](https://github.com/jethac/ncnn/tree/radish/0045-packed-a-shift) preserve this candidate separately from the robustness choice. Apply `0045-ncnn-packed-a-mb-shift.patch` after the explicit-device/four-tail stack and regenerate the packed-A embedded header. The equivalent, fully correct candidate is selected for the next private runtime build; end-to-end Japanese validation remains separate.


Generated-by: LLM

Checkpoint 0042 packs constant convolution weights so the A preload can store one
contiguous vec4 into shared memory. The focused actual Convolution1D comparison
reduces mean GPU elapsed **7.170520 → 6.517812 ms (9.102666%)**, with all four
balanced pairs positive. This experimental, default-off change advances to
full-core and fresh runtime references. The fixed-core warm reduction is 4.195%;
actual resident requests improve while cold/shape-load misses regress. Packed A
remains **default off**, and CPU still wins. The GPU-efficiency goal remains open.

## Change and focused result

`RADISH_NCNN_PLANAR_PACKED_A=1` is sampled only during create_pipeline. It selects
reduction-major/channel-minor FP32 weights and a matching planar shader, including
the existing padded-channel path. OFF preserves the accepted layout/shader.
Packing uses an owned Mat, leaves original weights unchanged, and keeps padded
reduction entries positive zero. GPU weight storage is unchanged; normal layers
temporarily need an extra packed host Mat. Pipeline recreation can repeat this
cold cost. The focused calls use each real layer's upload_model and actual GPU
weights/biases. Padding is outside these timed math calls.

The twelve-call schedule is `[OFF,ON,ON,OFF]` three times: four warmups and eight
measured calls, four per variant. All GPU downloads follow all twelve waits.

| Mean of four measured calls | Baseline | Packed-A |
| --- | ---: | ---: |
| GPU queued elapsed | 7.170520 ms | 6.517812 ms |
| Host submit interval | 8.024979 ms | 7.299079 ms |
| Record/submit/wait call | 8.315877 ms | 7.627686 ms |
| Issued work / mean GPU interval | 12.868618 GFLOP/s | 14.157311 GFLOP/s |
| Useful work / mean GPU interval | 10.707093 GFLOP/s | 11.779325 GFLOP/s |

Pair reductions are 8.932796%, 9.201260%, 8.983988% and 9.291981%. Each call issues
92,274,688 scalar-MAD FP32 operations, of which 76,775,424 contribute to useful
output positions; each MAD counts as two operations, excluding bias/address/control.
The [focused summary](results/gpu-packed-a-0042-focused.json) retains every sample,
pair, statistic and raw hash. Rates are achieved workload observations, not a
hardware ceiling, bandwidth, occupancy or ALU-busy measurement. Four measured
calls per variant do not establish sustained performance or statistical confidence.

Real create_pipeline wall intervals are **1.066895107 s OFF / 0.113649046 s ON**.
They combine packing and shader/pipeline creation, and compilation/cache order
can differ. They neither isolate packing cost nor show that packing is faster.

## Correctness and actual native evidence

All **327,168 output scalars** match a separately recomputed integer/dyadic oracle
exactly, and all twelve output payloads match across variants. Twelve destinations
are preuploaded with changing NaNs and unique guards. All 48 output guard words,
four common-B guard words and actual GPU weight/bias/B readbacks pass. Production
weights and biases have no artificial guards. Baseline and packed weight bytes
intentionally differ; all 180,224 words satisfy the complete transpose relation.
The 22 raw files total **4,468,960 bytes**, written before the helper's CPU reference.
The offline analyzer also checks their sizes/hashes against the terminal runner
receipt and unchanged remote hash manifests.

Both actual math programs use local 16x8x1, grid4x2x1, 16 KiB shared memory, 16 full
and zero half registers, four loops and zero private memory. Private zero follows
from the source-reviewed pipeline-creation rejection gate and the exercised exact
candidate timer predicate. The candidate's 896 reported native dwords correspond
to timer length 28; baseline 870 corresponds to27. Native inspection confirms one
vec4 global A load followed by **one contiguous vec4 shared store**, replacing
four scalar stores. The inner K loop retains three vec4 shared loads and 32 scalar
MADs in identical order, with no inner-loop MOVs. Total MOV count is 63 each;
address/control work and program size change. No barrier is removed.

All twelve valid timer/profile records report eight chunks and one submit each
(96 chunks, twelve submits total). The existing batching gate already admitted
length 28; the separate 25-line timer patch adds only the default-off `packed-a`
class, requiring exact device/resources/geometry and explicit elapsed/profile
controls. It does not alter batching or the global device ICD. Both services
remain active, no new GPU fault is recorded, and focused peak memory is 65.9 MiB.
Whole-window clocks and temperature cannot assign a clock to an individual call.

Analyzer v1 rejected an invalid assumption that stdout BEGIN/END must bracket
stderr timer lines in the merged systemd-run log. Separate pipes can merge out
of order despite helper flushes. V2 preserves strict order within each stream and
correlates all sample/submit IDs, pipeline/selector/native/geometry and submit
interval fields. The original failed v1 evidence is retained privately; the same
GPU capture passes v2 without rerunning the workload. Native/resource admission
is a separate reviewed artifact; the analyzer's length fields alone cannot prove it.

## Fixed full-core result and cold-start tradeoff

A fresh OFF/ON comparison using one identical model executable passes all 562,342
values against the original reference in both first and warm extractions. Final
captured outputs match accepted bytes (`e128ce63…68d4d287`). Every extraction executes 589 GPU layers with
zero CPU fallbacks, including the five padded-channel convolutions.

| Fixed core, one first and one warm extraction per process | OFF | ON |
| --- | ---: | ---: |
| Cold load including setup/pipelines/completed upload | 9.030 s | 11.427 s |
| First extraction including input/output transfer | 36.200045 s | 34.514664 s |
| Warm extraction | 35.805708 s | 34.303633 s |
| Service memory / swap peak | 382.3 / 50.4 MiB | 403.7 / 0 MiB |

Warm wall time falls **1.502075 s (4.195%)**. The 58 intended math command intervals
fall 24.684938059→22.969857545 s, saving 1.715080514 s; remaining command intervals
increase 0.217441827 s. Those command intervals include host work/submits/waits,
not just device queued time. All 1,520 ordered command signatures match except
58 intended native lengths, and all 23 packed shader variants use native 28,
16F/0H, local 16x8 and 16 KiB shared. Their 26,256 math chunks/3,294 submits are retained.

Cold load increases **2.397 s**, so load plus first extraction is approximately
**0.712 s slower** despite the faster extraction. This is an observed combined
startup cost, not an isolated packing measurement. OFF also incurred swap while
ON did not; a single pair cannot separate that condition from whole-call latency
or establish sustained performance. The [fixed-core summary](results/gpu-packed-a-0042-model.json)
records source/profile/log pins. The fresh runtime comparison below validates
request/waveform correctness and measures the cold/resident tradeoff.

## Reproduction in separate source/build trees

The ncnn patch is incremental over the accepted 17-file baseline at ncnn
`f18f3ffc94dbc76a277e94556c588ab2c2d0e1d9` with glslang
`780b6a209132dc74e99458f62ebe5fa7e57c41cf`. The [baseline manifest](results/gpu-packed-a-0042-baseline.json)
is historical pre-0042 evidence: its original Convolution1D hash is not the
expected post-build hash. The current standard build-ncnn.sh applies/verifies 0042
after the existing ordered patch stack. **Do not manually double-apply 0042.**
Runtime selection remains 0 unless explicitly enabled; wrappers validate 0|1 and
capture/serialize the exact RADISH_NCNN_PLANAR_PACKED_A key. Use a fresh consistent
build for generated shader enums; never relink new code against old shader tables.

With the existing external ncnn source, sysroot and loader setup, from repo root:

```bash
export RADISH_WORK=/run/media/jethac/Shared/radish
EXPERIMENT="$RADISH_WORK/packed-a-0042-repro"
BUILD="$EXPERIMENT/build-ncnn"
RADISH_NCNN_BUILD_DIR="$BUILD" radish/scripts/build-ncnn.sh
cmake --build "$BUILD" --target radish-gpu-packed-a -j2
radish/scripts/build-runtime.sh
```

The paired-helper target is EXCLUDE_FROM_ALL. The supported source stack now
includes 0042; its runtime selector remains default off. For the timer driver, use a separate Mesa checkout at base
`ddabdd0e36bd2f5e0c8e6bd21c3d1da7e34c1230`, read its AGENTS.md, apply cumulative 0041
instead of earlier cumulative driver patches, then apply
`patches/0042-radish-packed-a-timer.patch`. Build with the existing scripts/build.sh
using `RADISH_MESA="$MESA" RADISH_WORK="$EXPERIMENT" radish/scripts/build.sh`;
keep the existing sysroot/venv settings and use the separate checkout as MESA. Do not replace the global ICD. The timer
patch applies to queue source SHA256 `bd9d79a6ddaea0a68fcda73eb63d26a2553579e347f33a6f815b2e0fabe2e5d5`.

Run the paired helper with explicitly pinned private assets and the original
shared absolute lock. The portable runner uses RADISH_DEVICE_LOCK across checkouts;
its inherited FD must refer to that existing inode. It never creates/unlinks it.

The hashes below describe the recorded measurement assets. A fresh public target
or driver build can have different binary bytes because its target name, object
paths or build inputs differ; matching source does not imply executable identity.
Compute the fresh executable hashes and deliberately pin a private reproduction
copy of the runner. Keep its exact pin check enabled. Updating only the CLI pin is
insufficient because the runner also checks its `HELPER_SHA` constant.

```bash
export RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock
LABEL=0042-local
DRIVER="$EXPERIMENT/build/src/freedreno/radish/libvulkan_radish.so"
PROBE="$BUILD/radish-gpu-packed-a"
DRIVER_SHA=$(sha256sum "$DRIVER" | cut -d' ' -f1)
PROBE_SHA=$(sha256sum "$PROBE" | cut -d' ' -f1)
REPRO_TOOLS="$EXPERIMENT/repro-tools"
RUNNER="$REPRO_TOOLS/radish/scripts/device-gpu-packed-a.py"
mkdir -p "$REPRO_TOOLS/radish/scripts"
cp radish/scripts/device-gpu-packed-a.py radish/scripts/check-gpu-log.py \
  "$REPRO_TOOLS/radish/scripts/"
python3 - "$RUNNER" "$PROBE_SHA" <<'PYPIN'
import pathlib, re, sys
runner, digest = pathlib.Path(sys.argv[1]), sys.argv[2]
assert re.fullmatch(r'[0-9a-f]{64}', digest)
source = runner.read_text()
source, count = re.subn(r"^HELPER_SHA = '[0-9a-f]{64}'$",
                       "HELPER_SHA = '" + digest + "'", source, flags=re.M)
assert count == 1
runner.write_text(source)
PYPIN
# Archive the new helper, driver, runner and loader pins before any device run.
sha256sum "$PROBE" "$DRIVER" "$RUNNER" \
  "$RADISH_WORK/build-loader/loader/libvulkan.so" \
  > "$EXPERIMENT/reproduction-pins.sha256"
args=(--driver "$DRIVER" --driver-sha256 "$DRIVER_SHA"
      --probe "$PROBE" --probe-sha256 "$PROBE_SHA")
python3 "$RUNNER" compile "$LABEL" "${args[@]}" --check-only
(
  exec 9<"$RADISH_DEVICE_LOCK"
  flock -w1800 9
  python3 "$RUNNER" compile "$LABEL" "${args[@]}"
)
```

Stop after the fresh compile-only process. Review its actual baseline/candidate
native code and resources, including the expected arithmetic/order/barriers,
contiguous A store, register/shared/private limits and exact timer eligibility.
A matching successful compile receipt and that fresh native qualification are
mandatory before the probe. The recorded measurement's binary pins and native
review cannot substitute for qualification of a different executable/driver pair.
Keep the new pin manifest and compile evidence with the reproduction results.
After those gates pass, use the same label, assets and pins:

```bash
(
  exec 9<"$RADISH_DEVICE_LOCK"
  flock -w1800 9
  python3 "$RUNNER" probe "$LABEL" "${args[@]}"
)
python3 radish/scripts/analyze-gpu-packed-a.py \
  "$RADISH_WORK/gpu-packed-a-$LABEL/probe/run.log" \
  "$RADISH_WORK/gpu-packed-a-$LABEL/probe/raw" \
  "$RADISH_WORK/gpu-packed-a-$LABEL/analysis.json"
```

The original public runner retains the measured helper and loader pins. Only the
private reproduction copy receives the newly reviewed `HELPER_SHA`; the same
value is supplied as CLI `PROBE_SHA`, and `DRIVER_SHA` is computed from the newly
built driver. Loader changes require their own deliberate pin/source review too.
Limits remain 384 MiB memory, 128 MiB swap and 90 seconds, plus live-service/fault
checks, own-unit drain, immutable payload hashes and 64 MiB free-disk reserve.
Only named raw/metadata files are collected; no executable payload directory pull.

Measured helper SHA256 is `61285af2adf1ce4c6b38b6939653f7df363823f8afd4ff125a767e235170678c`,
private driver `a9be6b5c18b7bd6fa33cfe9d0d42e00532a6649090d6720128ccd6072af12a4e`,
loader `75699a38b1f26c1e8bfa2062e6593177bb07d4e3e346486d117fab8a0de0104a`.
Exact analyzer v2 is `dc1f28bb7854ce9290c678b48e00206dc5d00996e4e12d695bfc87668e6a32c3`;
helper source is `28f6e40df8562e1e8d9d3193b66def88b31edd79033469ef55348ac7d5efd4ce`.
The portable runner changes only root discovery, shared-lock path selection and
its header from measured private runner 50c45fbd…92a7a1; it has host checks, not an
additional tablet run. [The interface](tests/ncnn/gpu_packed_a_interface.json)
specifies exact fixtures, CLI/environment, guards and raw inventory. Portable
runner SHA256: `bb32e4db5d7d45fdbb3722f03291ce064c6b88ffbeabc5194a395c85b032507e`.

## Fresh runtime result and remaining scope

All four original-ORT core/waveform references pass; all eight boundaries and both
outputs match 0035 bytes. Each request executes 589 GPU/zero CPU layers with 58 packed
math commands. T78/T213 retain 9760/26256 math chunks and 1238/3294 submits. The
[compact runtime result](results/gpu-packed-a-0042-runtime.json) retains timings,
hashes, validation and source/binary provenance.

| Request | T / Net | 0035 synthesis s | 0042 synthesis s | 0042 load s |
| --- | --- | ---: | ---: | ---: |
| 0 | 78 / miss | 27.427916 | 28.952951 | 11.629309 |
| 1 | 78 / hit | 18.163352 | 17.297296 | 0 |
| 2 | 213 / miss | 45.676598 | 46.216283 | 5.929418 |
| 3 | 213 / hit | 42.819037 | 41.225818 | 0 |

Resident hits save 0.866056/1.593219 s; misses cost 1.525035/0.539685 s more. Initial
and shape-change load increase 2.456407/2.413035 s, including packing, pipelines and
uploads. Request synthesis totals 134.086903108→133.692347753 s save only 0.394555355 s.
The larger 137.660401781→134.609214313 s session change includes pre-request,
publication and cleanup differences; it cannot be credited wholly to GPU. Session
peak 512.8 MiB plus 74.3 MiB swap compares with 531.4/57.2 previously. One historical
and one current sequence differ in binary/link, date, caches/clocks and swap.

The standard model/operator/runtime wrappers expose the option as default 0. See
[RUNTIME_INTEGRATION.md](RUNTIME_INTEGRATION.md#optional-packed-a-runtime-0042) for
the complete request command. Current canonical host builds reproduce measured
helper 61285af2…, model 0b2f39fa… and runtime 0971994a… bytes exactly. The newly linked
canonical ICD 6ae15421…7813f differs from measured private ICD a9be6b5c…12a4e despite
identical reviewed queue source; no separate device run of that canonical link is
claimed. The global device ICD remains 0032.

Correctness permits this explicit tradeoff, not a default recommendation. CPU
resident short/full 15.411271/36.802922 s remains faster than 17.297296/41.225818 s.
Sustained behavior, live application integration, current-graph cancellation and
restart remain unvalidated; neither GPU saturation nor Vulkan conformance follows.
