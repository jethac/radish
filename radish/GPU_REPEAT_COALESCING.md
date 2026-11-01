# Rejected experiment: compiler repeat coalescing (0039)

0039 is rejected **before tensor execution**. Deferring eight cold output
collect affinities permits real repeated MAD encodings, but introduces moves
inside the K reduction and across its loop boundaries. This fails the
preselected requirement that improved repeat encoding add no hot or outer
backedge moves. No shader timing, tensor reference, model performance or
conformance result follows. Canonical Mesa remains 0037; the global device
ICD remains 0032.

## Compiler result

The default-off `RADISH_IR3_DEFER_COLD_COLLECT=1` patch changes only IR3
register-coalescing order for a narrow A506 compute structure: local16x8,
16 KiB shared memory, four SSBOs and exactly eight matching cold vec4 output
collects. Mandatory phi handling remains first. These structural checks do
not establish shader semantic identity; the fixed C128/K11/dilation1/W213
padded helper and captured native trace identify this experiment.

Both fresh-process compile-only captures use the same candidate driver,
existing helper and loader; only the compiler toggle differs. OFF preserves
all 77,728 emitted Mesa trace/native/resource lines from the accepted trace.
ON changes only math program 7; its final NIR and pre-allocation stages remain
exact, and the other seven programs retain native instructions and statistics.

| Math program measure | OFF | ON |
|---|---:|---:|
| Inner MAD encodings | 32 scalar | 13 rpt1 + 6 scalar |
| Inner scalar-equivalent MAD operations / vec4 LDS | 32 / 3 | 32 / 3 |
| Inner encoded instructions / expanded slots | 47 / 55 | 36 / 55 |
| Inner register MOVs | 0 | 2 |
| Whole encoded instructions through END | 431 | 457 |
| Native rounded program length | 27 | 29 |
| Whole printed expanded instructions / MOVs | 717 / 63 | 738 / 110 |
| Full / half registers | 16 / 0 | 16 / 0 |

Both retain four loops, local16x8 and 16 KiB shared memory; no private-memory
opcode appears and pipeline creation passes the private-memory guard.
Instruction counts shown end at END; native length is the driver's rounded
program-length field. The inner expanded slot count stays 55 despite the new
MOVs because two explicit NOP slots disappear; this does not establish equal
GPU latency.

The actual new inner moves save `r1.w` into `r0.z` before the first A load,
then restore it on the inner backedge. The allocation has placed a vec4 LDS
result across the still-live spatial address register. More shuffling occurs
at K32 boundaries. For K=1408, the audited additional pre-output register-MOV
count is `44*(10+32+31)+43*11 = 3685` per invocation, plus conditional output
assembly moves. This is an executed-path count derived from IR/native control
flow, not measured GPU time or cycles. The attempted move relocation was not
confined to cold output assembly.

Native length 29 also lies outside the current tiled batching set 27/28/31/36/37 and
qualified convolution elapsed gate 27. Those gates were not expanded. Compiler
process success therefore does not qualify an equivalent eight-chunk,
one-submit timing comparison, and the cost rejection already rules out the
planned paired tensor helper.

## Evidence and reproduction

Actual captures are `WORK/repeat-coalescing-0039-first-mode{0,1}/compile/`.
The source-owner report is `ir3-cold-collect-private-vrgii66e/actual-first/REPORT.md`;
the independent audit is
`ir3-cold-collect-review-o4t3y95m/ACTUAL-COMPILER-REJECTION.txt`
(SHA256 `c3827d0a…252a58`). OFF/ON services take 2.225 / 2.253 s and peak at 45.4 / 45.0 MiB;
these are compile costs, not shader execution times. No new GPU faults occur
and both live services remain active. Compile-only returns before helper
VkTransfer/VkCompute or model/tensor upload; existing device initialization
may submit its own dummy work.

The exact incremental patch is
`0039-ir3-cold-output-collect.patch`, SHA256
`9633ded58bf108c543d536582f795adbc40bafab0c591457bdfeb89ac4b9eaac`.
Read Mesa's `AGENTS.md` and the pinned-base instructions in `patches/README.md`.
Use a **separate experimental Mesa checkout** with cumulative 0037 followed by
0039; the rejected 0038 queue patch is not needed. Build into a separate
artifact directory. The private builder first reproduced canonical 0037
byte-for-byte, then replaced only the merge-register object; shared link
inputs and dependency headers were checked unchanged.

Hardware used private runner `2e8d7bb0…dd431`, driver `8189ca1f…a0633`,
helper `2510a415…6024d` and loader `75699a38…0104a`. That runner reused the
existing helper/loader through verified hardlinks. The public compile-only
adaptation stages a fresh complete payload, then reuses exactly that payload
for ON. It has only host syntax, pin and argument-check evidence; it has not
been separately executed on the tablet. No new C++ helper or analyzer is
introduced. The existing optional `radish-state-reuse` binary must match
SHA256 `2510a415061b59ad2e763dc0e96083404a0db8de76e9f747646165249176024d`.

```bash
export RADISH_DEVICE_LOCK=/home/jethac/Projects/lenovo-8/.device.lock
# DRIVER/DRIVER_SHA select the separate 0037+0039 compiler build.
# PROBE points to the existing optional radish-state-reuse binary.
PROBE_SHA=2510a415061b59ad2e763dc0e96083404a0db8de76e9f747646165249176024d
LABEL=repeat-coalescing-history
args=(--driver "$DRIVER" --driver-sha256 "$DRIVER_SHA"
      --probe "$PROBE" --probe-sha256 "$PROBE_SHA")
python3 radish/scripts/device-repeat-coalescing.py compile "$LABEL" \
    "${args[@]}" --defer-cold-collect 0 --check-only
(
    exec 9>"$RADISH_DEVICE_LOCK"
    flock -w1800 9
    python3 radish/scripts/device-repeat-coalescing.py compile "$LABEL" \
        "${args[@]}" --defer-cold-collect 0
    python3 radish/scripts/device-repeat-coalescing.py compile "$LABEL" \
        "${args[@]}" --defer-cold-collect 1
)
```

`--defer-cold-collect` is mandatory and accepts only 0/1. `probe` is explicitly
rejected before device access. OFF requires a fresh remote label; ON requires
the successful OFF receipt with identical asset hashes and unchanged log.
Evidence is under `WORK/repeat-coalescing-LABEL/compile-{off,on}/`. Both launches
set `IR3_SHADER_DEBUG=disasm,optmsgs` and the toggle before pipeline creation.
The shared lock, private ICD, 384 MiB memory / 128 MiB swap caps, 90 s service limit,
service/fault checks and own-unit drain remain in place.

Manually inspect the captured math program and its indexed IR stages: confirm
OFF native identity, unchanged pre-allocation arithmetic, actual repeat
formation, all inner/outer-backedge moves, resources and rounded length. The
recorded rejection above is the expected result. A passing compile receipt
alone is not native admission or numerical correctness. Any later attempt to
remove the live-range cost needs a separate source proposal; do not weaken
the failed rule to time this candidate.
