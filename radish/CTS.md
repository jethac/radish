> Historical initial CTS snapshot. Current requirements, official-harness status and preserved failure scope are in [VULKAN11_COMPUTE_STATUS.md](VULKAN11_COMPUTE_STATUS.md). Modified-harness results never count as official passes.

# Pinned CTS diagnostic subset

Source audit: 2026-11-01, Khronos VK-GL-CTS
`5a9cb32498158eb60774cf8a3d4e0b8c68f7fe9e`. The isolated AArch64 cross-build
completed successfully. No CTS device cases have been executed.

[scripts/build-cts.sh](scripts/build-cts.sh) prepares an AArch64 Release build
with `DEQP_TARGET=vulkan_headless` and four parallel build jobs by default:

```sh
RADISH_JOBS=4 radish/scripts/build-cts.sh
```

Use `--fetch-only` to stop after dependency preparation.
[scripts/fetch-cts-deps.py](scripts/fetch-cts-deps.py) handles the pinned
dependencies; its interface is `--source PATH --build PATH [--verify-only]`.
The build script uses a lock and checks for an active build; do not start a
second build against the existing output directory.

Defaults are `/run/media/jethac/Shared/radish/downloads/vk-gl-cts` for source
and `/run/media/jethac/Shared/radish/build-cts` for output. The expected binary
path is `build-cts/external/vulkancts/modules/vulkan/deqp-vk` under that shared
work directory. Overrides are `RADISH_WORK`, `RADISH_CTS_SOURCE`,
`RADISH_CTS_BUILD`, `RADISH_CTS_TOOLCHAIN`, `RADISH_SYSROOT`,
`RADISH_GCC_INSTALL`, and `RADISH_JOBS`.
When changing `RADISH_SYSROOT`, supply a matching toolchain: the toolchain
sets CMake's target sysroot, while this variable selects the GCC installation.
The output directory retains `CTS_REVISION`, `DEPENDENCIES.json`,
`commands.log`, tool versions, configure/build logs, and `BINARY.sha256`.

The initial binary is 97,536,760 bytes with SHA256
`0e715f9c53ec1dfd742d93a7a45740cbf4a8caa9193fe761e71dd6347c2a6660`.
Its interpreter is `/lib/ld-linux-aarch64.so.1`; runtime libraries include
libm, zlib, libstdc++, libgcc, and libc. The linked sysroot requires
`GLIBC_2.43` from libm and `GLIBCXX_3.4.32` from libstdc++; deployment must use
matching target libraries. `BINARY.json` and `binary-readelf.log` preserve
the initial artifact metadata without running the binary.

The existing `streams/gpu-mesa16/work/sysroot` is a read-only build input.
The old GLES CTS checkout at `streams/gpu-mesa16/downloads/vk-gl-cts`, revision
`7f5fb62245d535a1fc0bf50b2c7e5a342dc551fe`, remains untouched. The pinned new
CTS source needs no queue-selection patch: its default-context code already
falls back to a compute-only queue (`vktContextManager.cpp`, around line 403).
Use unchanged upstream sources; do not patch tests to hide driver gaps.

The three lists in [tests/cts](tests/cts) preserve the source-audited scope:

| List | Cases | Purpose |
| --- | ---: | --- |
| [api-memory-first.txt](tests/cts/api-memory-first.txt) | 14 | Queries, single resource creation, and small host mappings; no test-body queue submissions |
| [compute-first.txt](tests/cts/compute-first.txt) | 5 | Empty shader, SSBO access/copy, then tiny shared-memory barriers |
| [known-gaps.txt](tests/cts/known-gaps.txt) | 3 | Property validation and two guarded unsupported capabilities |

Resource creation still initializes the driver and may allocate DRM buffers.
`device_memory_small` allocates 1 KiB; `buffer_storage_small` creates an
unbound 1 KiB buffer. Mapping cases request 33 bytes, rounded to Radish's
64-byte noncoherent atom. Query paths start with `dEQP-VK.info.`; current
compute paths include `dEQP-VK.compute.pipeline.basic.`.

| Compute case suffix | Local size | Grid | Resources / work |
| --- | --- | --- | --- |
| `empty_shader` | 1×1×1 | 1×1×1 | No descriptors; one dispatch, completion check only |
| `ssbo_rw_single_invocation` | 1×1×1 | 1×1×1 | One 1 KiB SSBO; 256 uint inversions |
| `copy_ssbo_single_invocation` | 1×1×1 | 1×1×1 | Two 4 KiB SSBOs; separate fill submission before compute |
| `shared_var_single_invocation` | 1×1×1 | 1×1×1 | One 4-byte SSBO; 4 bytes shared |
| `shared_var_single_group` | 3×2×5 | 1×1×1 | One 120-byte SSBO; 120 bytes shared, 30 invocations |

These cases use primary command buffers, direct dispatch, pipeline flags zero,
ordinary SSBO descriptors, and fence waits. The source checks cover
`compute/vktComputeBasicComputeShaderTests.cpp`,
`framework/vulkan/vkComputePipelineConstructionUtil.cpp`,
`api/vktApiObjectManagementTests.cpp`, and `memory/vktMemoryMappingTests.cpp`.
Paths are relative to the CTS Vulkan modules or framework as indicated.

After a successful build and explicit deployment of the private loader, ICD,
and binary, the bounded first command below queries queue properties. The
runner owner handles the device lock, service coordination, and GPU-log
baseline/checks. The exact argument vector is preserved in
[FIRST_RUN_PLAN.json](tests/cts/FIRST_RUN_PLAN.json).

```sh
systemd-run --quiet --wait --pipe --collect \
  -p MemoryMax=384M -p RuntimeMaxSec=45 -p LimitCORE=0 \
  --setenv=LD_LIBRARY_PATH=/var/tmp/radish \
  --setenv=VK_DRIVER_FILES=/var/tmp/radish/radish_icd.json \
  /var/tmp/radish/cts/deqp-vk \
  --deqp-vk-library-path=/var/tmp/radish/libvulkan.so.1 \
  --deqp-case=dEQP-VK.info.device_queue_family_properties \
  --deqp-log-filename=/var/tmp/radish/cts/queue-properties.qpa \
  --deqp-log-images=disable --deqp-log-shader-sources=disable \
  --deqp-shadercache=disable --deqp-watchdog=enable \
  --deqp-terminate-on-fail=enable --deqp-terminate-on-device-lost=enable \
  --deqp-archive-dir=/var/tmp/radish/cts
```

Run one reviewed case per bounded invocation initially and retain each QPA
under a distinct name. Stop after a timeout, device loss, or GPU fault. These
selected inline-shader cases need no external image or Amber assets; broader
CTS coverage would need a separate source/dependency audit and data deployment.

Keep Pass, Fail, NotSupported, timeout, and device loss distinct. In particular:

- `info.device_properties` remains a diagnostic failure candidate. Exported
  storage-buffer descriptor counts and `maxPerStageResources` are zero despite
  an internal 32-descriptor capacity. The test validates Vulkan minimum limits.
- `memory.requirements.dedicated_allocation.buffer.regular` should report
  NotSupported at its `VK_KHR_get_memory_requirements2` guard, before its
  `VK_KHR_dedicated_allocation` guard or buffer-creation code.
- `subgroups.basic.compute.subgroupelect` should report NotSupported at the
  Vulkan 1.1 guard, before BASIC-bit checks or GPU work. Neither unsupported
  outcome counts as a pass.

Do not expand these lists into wildcards. Indirect dispatch can reach a missing
downstream hook; event paths are also incomplete. UBO/image descriptors,
secondary buffers, shader objects, and nonzero pipeline flags are outside this
first subset. See [VULKAN_AUDIT.md](VULKAN_AUDIT.md) for the remaining gaps.

Small-case passes would not establish advertised resource maxima, the full
memory model, multi-workgroup shared/atomic behavior, subgroup semantics, or
Vulkan conformance. The known physical-64 ncnn reduction hang and broader
register-pressure behavior remain unresolved; this subset does not test them.
