# Packed-A convolution experiment

Generated-by: LLM

Checkpoint 0042 packs constant convolution weights so the A preload can store one
contiguous vec4 into shared memory. The focused actual Convolution1D comparison
reduces mean GPU elapsed **7.170520 → 6.517812 ms (9.102666%)**, with all four
balanced pairs positive. This experimental, default-off change advances to
runtime checks. A fresh fixed full-core comparison also passes correctness and
records a 4.195% warm reduction, with a cold-start penalty described below.
**End-to-end runtime acceptance and default changes remain pending; CPU still wins.**

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

Both actual math programs use local16x8x1, grid4x2x1, 16 KiB shared memory, 16 full
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
16F/0H, local16x8 and 16KiB shared. Their 26,256 math chunks/3,294 submits are retained.

Cold load increases **2.397 s**, so load plus first extraction is approximately
**0.712 s slower** despite the faster extraction. This is an observed combined
startup cost, not an isolated packing measurement. OFF also incurred swap while
ON did not; a single pair cannot separate that condition from whole-call latency
or establish sustained performance. The [fixed-core summary](results/gpu-packed-a-0042-model.json)
records source/profile/log pins. Fresh end-to-end request/waveform validation is
still pending before any recommendation change.

## Reproduction in separate source/build trees

The ncnn patch is **incremental over the current 17-file dirty accepted baseline**,
not clean upstream. That baseline is ncnn `f18f3ffc94dbc76a277e94556c588ab2c2d0e1d9`,
glslang `780b6a209132dc74e99458f62ebe5fa7e57c41cf`, with the current ordered
[patch stack](patches/README.md) through 0035. Its exact file pins are in
[the baseline manifest](results/gpu-packed-a-0042-baseline.json). Preserve untracked
shader files when copying it. The existing build-ncnn.sh knows only the accepted
prefixes and will reject an 0042 source tree; configure the opt-in tree directly.
A fresh consistent ncnn build is required because adding a shader changes generated
enum tables. Never link the new helper to an older generated shader table.

From the repository root, with the existing cross toolchain/sysroot setup:

```bash
export RADISH_WORK=/run/media/jethac/Shared/radish
TOP=$PWD
EXPERIMENT="$RADISH_WORK/packed-a-0042-repro"
SRC="$EXPERIMENT/ncnn"
BUILD="$EXPERIMENT/build-ncnn"
mkdir -p "$EXPERIMENT"
test ! -e "$SRC"
cp -a "$RADISH_WORK/ncnn" "$SRC"
python3 - "$SRC" "$TOP/radish/results/gpu-packed-a-0042-baseline.json" <<'PYBASE'
import hashlib, json, pathlib, sys
source, manifest = pathlib.Path(sys.argv[1]), json.load(open(sys.argv[2]))
for name, pin in manifest['files'].items():
    data = (source / name).read_bytes()
    assert len(data) == pin['bytes'] and hashlib.sha256(data).hexdigest() == pin['sha256'], name
PYBASE
patch -d "$SRC" -p1 < "$TOP/radish/patches/0042-ncnn-planar-packed-a.patch"
SYSROOT=${RADISH_SYSROOT:-$TOP/streams/gpu-mesa16/work/sysroot}
GCC_INSTALL=${RADISH_GCC_INSTALL:-$SYSROOT/usr/lib/gcc/aarch64-linux-gnu/16}
cmake -G Ninja -S "$TOP/radish/tests/ncnn" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$RADISH_WORK/loader-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DNCNN_SOURCE="$SRC" \
  -DCMAKE_C_FLAGS=-mcpu=cortex-a53 \
  "-DCMAKE_CXX_FLAGS=-mcpu=cortex-a53 --gcc-install-dir=$GCC_INSTALL" \
  -DNCNN_VULKAN=ON -DNCNN_SIMPLEVK=ON -DNCNN_THREADS=ON \
  -DNCNN_OPENMP=OFF -DNCNN_SIMPLEOMP=OFF -DNCNN_RUNTIME_CPU=OFF \
  -DNCNN_ARM82=OFF -DNCNN_ARM82DOT=OFF -DNCNN_ARM82FP16FML=OFF \
  -DNCNN_ARM84BF16=OFF -DNCNN_ARM84I8MM=OFF -DNCNN_ARM86SVE=OFF \
  -DNCNN_BUILD_TESTS=OFF -DNCNN_BUILD_TOOLS=OFF -DNCNN_BUILD_EXAMPLES=OFF \
  -DNCNN_BUILD_BENCHMARK=OFF -DNCNN_BENCHMARK=OFF
cmake --build "$BUILD" --target radish-gpu-packed-a -j2
```

The new target is EXCLUDE_FROM_ALL; default builds and runtime selectors are
unchanged. For the timer driver, use a separate Mesa checkout at base
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

## Remaining end-to-end work

Fixed-shape forward/padded-channel/full-core outputs, native resources and matching
batching now pass in this private experiment. Fresh runtime request shapes,
waveforms, reload cost and end-to-end latency remain to validate before adoption.
The fixed-core pair has one first and one warm value per variant and different
swap observations. The accepted recommendation, global device ICD and public
Vulkan/conformance claims remain unchanged while those checks are pending.
