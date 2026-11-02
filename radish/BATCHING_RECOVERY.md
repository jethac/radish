# Shared-resource batching recovery: 0045

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
