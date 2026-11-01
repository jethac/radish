# Initial compute ABI

Status: implemented and exercised by the index/vector-add tests on A506. This is
an experimental ABI, version 2. Shader caching is disabled until cache keys
include the ABI and descriptor-layout mapping.

## Resources

Each descriptor-set layout sorts bindings by binding number, then packs array
elements consecutively. A pipeline concatenates those layouts in set order to
produce up to 32 hardware SSBO slots. The limit is currently an implementation
bound; testing covers three populated slots, not every possible slot count.

SPIR-V uses NIR's `32bit_index_offset` SSBO address representation. Resource
indices become `(hardware slot, 0)`; resource reindexing changes the slot, and
explicit IO lowering supplies the byte offset. Compile-time resource usage tracks
which descriptors are required. Unused sets need not be bound, and unused
descriptors may remain uninitialized. Unused hardware slots receive zero extent
and a harmless internal BO address.

For each dispatch, a5xx `CP_LOAD_STATE4` loads SSBO extents using
`SB4_CS_SSBO/ST4_CONSTANTS`, then addresses using `SB4_CS_SSBO/ST4_UBO`. Extents
are byte counts split across the width/height fields, following computerator's
a5xx implementation. Addresses include both the buffer's memory-binding offset
and descriptor offset, and go through Freedreno BO relocations/residency tracking.

Command recording snapshots ordinary descriptor values. Update-after-bind flags
are unsupported. Changing an ordinary descriptor already referenced by an
executable command buffer invalidates that command buffer under Vulkan's rules;
this implementation does not promise live descriptor mutation after recording.

## Constants and builtins

The first 32 constant dwords are reserved for up to 128 bytes of push constants.
Loads divide byte offsets by four before using ir3's constant-load intrinsic.
ir3 allocates any required driver parameters after that reservation. Immediate
constants start at `ir3_const_state(v)->allocs.max_const_offset_vec4`.

Constant packets address vec4 s; hardware constant length is programmed from the
compiled variant in the units used by the existing a5xx compute implementation.
The shader's compiled sysval register assignments program `WGIDCONSTID` and
`LOCALIDREGID`. NIR lowers global invocation ID to local invocation plus workgroup
ID times local size. Driver parameter uploads use `ir3_driver_params_cs` for
original API workgroup counts, local sizes and the current chunk's base group.
`nir_lower_system_values` already adds the base to the API WorkgroupId;
`nir_lower_compute_system_values` adds it in the GlobalInvocationId formula.
Radish retains `load_base_workgroup_id` through `has_base_workgroup_id=true`;
it must not add the base again in a private lowering pass. Hardware global
offsets stay zero. This internal ABI does not expose Vulkan DispatchBase.

Hardware chunks contain at most 64 groups, or one group for barrier kernels
by default. `RADISH_SHARED_GROUPS=1..8` allows up to eight groups per chunk
for barrier kernels with at most 32 local invocations. Larger local sizes
retain the one-group limit. Rectangular tiles preserve all three logical axes
and the original NumWorkgroups. Tests cover X/Y/Z chunk boundaries, independent group/global
IDs, 3D local IDs, partial outputs, shared exchange and zero-size dispatches.
This bounds groups per chunk, not the runtime of arbitrary shader loops.

The first path uses a single-size wave, fixed workgroups of at most 128
invocations and no private-memory spills. Current cumulative patch 0016 enforces
the measured A506 register budget before shader upload. It uses
`ir3_shader_halfregs(v) = 2 * (max_reg + 1) + (max_half_reg + 1)` and requires
resident threads times that footprint to be at most 4096 half-register units
(2048 full vec4 registers). A barrier workgroup reserves its entire local size,
rounded up to a wave; a non-barrier shader requires one resident wave. This
accounts for full and half register banks together. The earlier local128 /
21-full tiled program is rejected with `VK_ERROR_FEATURE_NOT_PRESENT` before
upload or execution; revised local128 / 16-full / 0-half passes all 25984
operator outputs. This bound does not prove the cause of every historical hang
or general occupancy safety. The full FP32 InstanceNorm+tiled model also passes
under this guard in both first and warm extractions, with 37 actual local16x8
barrier math commands and a passing waveform check; broader
workgroup/register combinations still need their own evidence.
Shared variables now lower to explicit byte
addresses and ir3 local-memory operations. Tree reductions pass with 32/128
invocations, including byte address 16380. The driver caps static shared memory
at 16 KiB and rejects larger allocations before GPU submission. Accesses above
16 KiB faulted; their cause remains unresolved, not a proven hardware ceiling.
Static allocation size feeds ir3's constant/local-buffer budgeting.
UBOs, images/textures, private memory and indirect dispatch remain unimplemented.

## Completion and visibility

Execution waits on submission dependencies, submits a bounded chunk or eligible
batch described below, waits for it, then proceeds. Each chunk restores compute
state, invalidates UCHE, emits shader/resources/constants and runs `CP_EXEC_CS`.
Afterward, `CACHE_FLUSH_TS` writes a sequence number to a control BO and
`CP_WAIT_REG_MEM` waits for that value. A bounded five-second DRM fence wait must
succeed before the Vulkan runtime signals submission completion. A post-wait
`FD_CTX_FAULTS` query must also succeed and equal the device's initial counter.
The matching kernel increments this counter before forcing completion of a
faulting submission during recovery. Query storage is zeroed because the kernel
copies a four-byte counter into Mesa's eight-byte parameter buffer. The ncnn
hang now returns DEVICE_LOST; no later recorded command executes. This counter
does not cover every IOMMU crashstate path, so output checks and kernel-log
inspection remain required. Shader BOs are marked for fault-dump capture.

Host mappings are default Freedreno write-combined BO mappings, exposed as
host-visible and host-coherent memory. Submission, flush and unmap issue a CPU
memory barrier; explicit invalidation also prepares the BO for CPU reading.
The coherent regression verifies 32 dispatches without flush/invalidate calls.
Typed command records preserve transfer/dispatch order. Fill/update use
CP_MEM_WRITE; aligned copies use CP_MEM_TO_MEM, with 4 KiB chunks and bounded
fence waits. Unaligned copies use CPU memmove with BO CPU preparation and a host
barrier after preceding work has drained. This fallback depends on the serial
queue; asynchronous batching must replace or explicitly synchronize it.
Mixed transfer/compute and chained producer/consumer tests pass 48 dispatches;
broader memory-model and cross-submission coverage remains required before
narrowing synchronization or introducing asynchronous batching. These selected
tests do not prove every Vulkan memory dependency.

`RADISH_BATCH_CHUNKS=1..4` can combine up to four chunks of one logical dispatch
in a submission, only for shaders without loops or barriers and with instruction
length at most 32. Every chunk retains its state, cache and sequence-number waits;
the submission retains its fence wait and fault-counter check. Both controls
default to 1. Dependent-dispatch and full fixed-core output checks pass with
batching at 4 and shared groups at 8. These bounded policies still incur substantial
submission overhead; they do not establish general asynchronous queue support.
