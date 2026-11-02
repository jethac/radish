# Shared-resource batching recovery: 0045

## 2026-11-02: normalization submission recovery passes

With math batching fixed at eight, the next queue-only change reduces resident fixed-core time from **52.746539 to 51.440372 seconds (2.4763%)**. First extraction falls from 52.666168 to 51.590609 seconds. Both complete 562,342-value reference checks pass and the saved outputs retain the accepted bytes. All 589 layers execute on the GPU.

Only 225 normalization submission counts change among 1,520 ordered command signatures: 3,518→797, saving 2,721 submissions. Math remains 3,294; total submissions fall 13,530→10,809 with all 71,750 chunks unchanged. The new class requires exactly two ordinary SSBOs, fixed local 16×1×4, no loops/barriers/shared/private/texture resources, bounded expanded instruction/memory work and the existing register bound. No shader, geometry, per-chunk synchronization or math policy changes. The maximum observed normalization submission grows from 2.308 to 3.574 ms in the resident extraction; the overall candidate maximum stays below 113 ms.

This is one sequential comparison. Baseline peak swap was 23.6 MiB; candidate peak was 408.3 MiB RAM with zero swap. No new faults occurred and both services stayed active. The candidate remains 49.96% slower than the historical 0042 fixed-core result; end-to-end speech performance is still separate. [Exact source, profiles and limits](results/normalization-batching-0045.json).

The first load attempt exposed a manifest-formatting dependency in ncnn: pretty JSON selected direct ICD loading and a subgroup-size fallback, causing the exact pad-layout guard to reject the pipeline. A fresh capture restores the accepted compact manifest/private-loader route, and live process maps confirm the selected libraries. Host staging now checks that route. A separate output-space preflight rejection is preserved; the original disk reserve was maintained by archiving only a verified completed private executable. Neither failure is counted as inference evidence.

Apply `0045-radish-short-buffer-batching.patch` after the shared-resource batching patch. Keep `RADISH_BATCH_CHUNKS=8` and `RADISH_TILED_BATCH_CHUNKS=8` fixed for this comparison; toggling generic batching would change unrelated commands. The original repeat-2 runner, references and bounds remain. The following section preserves the first recovery's separate pair.

The same driver and model reduce resident fixed-core time from **67.626229 to 52.746539 seconds (22.0028%)** when the existing tiled batch limit changes from one to eight chunks. Both processes perform two complete extractions; all 562,342-value reference comparisons pass, and their saved outputs match the accepted GPU bytes exactly. Every extraction retains 589 GPU layers and no CPU fallback.

| Measurement | Batch 1 | Batch 8 |
| --- | ---: | ---: |
| Cold model load | 20.321 s | 20.222 s |
| First extraction | 67.590699 s | 52.666168 s |
| Resident extraction | 67.626229 s | 52.746539 s |
| Packed math submissions | 26,256 | 3,294 |
| Total submissions | 36,492 | 13,530 |
| Peak memory | 408 MiB | 407.6 MiB |
| Peak swap | 0 | 23.6 MiB |

Only the 58 packed-math submission counts change among all 1,520 ordered command signatures. Their 26,256 chunks and the other 1,462 command signatures/submissions stay identical. Warm packed-math command wall time falls by 15.218815 seconds; the retained command wall time grows by 0.332210 seconds. The source change affects eligibility for the existing batching path. All per-chunk setup, waits, cache flushes and dispatch geometry remain intact.

The former native-length/no-half-register fingerprint excluded valid robust kernels. The replacement checks the actual four ordinary SSBO bindings, absence of image/sampler/raw/constant-data resources, original 16×8 local shape, four loops, 16 KiB shared storage, zero private storage and the unchanged 4,096 combined-register residency bound. The A506, single-thread-size, full-register and eight-chunk limits remain. All 179 load-time programs have byte-identical decoded compiler output to the admitted model; all 23 packed variants retain 15 full and two half registers, with no native private loads/stores.

This is one sequential pair, not randomized statistics or GPU elapsed timing. The candidate used 23.6 MiB swap, so conditions were not identical. No new GPU faults occurred; both live services remained active and each transient unit drained. The same historical 0042 fixed-core resident result is 34.303633 seconds: the current candidate remains **53.76% slower** in that uncontrolled comparison. These measurements do not describe waveform generation or end-to-end speech latency.

Apply [the incremental patch](patches/0045-radish-shared-resource-batching.patch) after the complete 0044 stack through storage-Cube/multisample. The source selector remains opt-in: `RADISH_TILED_BATCH_CHUNKS` defaults to 1. The measured pair fixes `RADISH_BATCH_CHUNKS=8`, `RADISH_SHARED_GROUPS=8`, `RADISH_TILED_GROUPS_X=1`, packed-A and the accepted normalization/model selectors, varying only `RADISH_TILED_BATCH_CHUNKS=1` versus `8`. Existing model wrappers already carry these controls. Retained native admission precedes the original repeat-2 model run, with unchanged references, exact accepted output check and 640 MiB/128 MiB swap/300-second bounds.

[The exact result](results/batching-recovery-0045.json) records source, driver, model, fixtures, runner, native identity, capture and review hashes. The system ICD remains unchanged. This qualifies batching recovery for the measured workload; normalization batching, remaining latency and end-to-end Straylight performance remain active work.
