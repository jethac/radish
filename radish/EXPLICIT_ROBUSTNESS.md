# Explicit application robustness: 0045

The audited, tail-corrected Kokoro fixed core passes both full reference comparisons with core buffer robustness enabled and disabled. On the same model binary, ICD, loader, inputs and batching settings, the explicit per-device choice reduces resident extraction from **51.504783 to 34.333299 seconds (33.3396%)**. Saved output bytes match the accepted GPU output in both processes. Each extraction runs all 589 GPU layers with no CPU fallback.

| Measurement | Explicit true | Explicit false |
| --- | ---: | ---: |
| Cold model load | 19.754 s | 11.700 s |
| First extraction | 51.673201 s | 35.067822 s |
| Resident extraction | 51.504783 s | 34.333299 s |
| Total submissions | 10,809 | 10,809 |
| Packed math submissions | 3,294 | 3,294 |
| Peak memory | 412.1 MiB | 405.4 MiB |
| Peak swap | 14.4 MiB | 0 |

All 1,520 ordered commands retain their owners, geometry, 71,750 chunks and submission counts. The 23 packed kernels change from 15 full/two half registers and native59/60 to 16 full/no half registers and native28, within the same 4,096-unit residency bound. Compiled lengths change across all 1,424 dispatches; one ordinary convolution also changes loop unrolling. Resident packed-math command wall time saves 9.221423 seconds; other profiled commands save 7.922651 seconds. This is application-requested compiler behavior, with no scheduling-policy or driver-guard change.

Before disabling the feature, source review found real discarded-tail reads in four stock convolution/deconvolution shader families. Pack4 spatial loads now have explicit bounds, and paired duplicate coordinates stay within valid input/weight/bias ranges. Existing valid operands, accumulation order and stores remain unchanged. Both modes use these fixes; the original out-of-bounds shaders were never run with robustness disabled. All used stock/custom families were reviewed for this workload, and native inspection confirms the tail guards survive false-mode compilation. This does not establish arbitrary ncnn model safety without robustness.

The ncnn API adds an explicit per-device feature choice and an accessor for the enabled state. The existing constructor retains its queried-feature default, and Radish continues to implement requested robustness. The private model runner requires a pinned absolute Vulkan loader path for either explicit mode and verifies the selected library with `dladdr`/`realpath`, removing the earlier manifest-format dependency. Owned device lifetime ends after model/cache/allocator consumers and before the instance. No application default, installed ICD or service changes.

This is one sequential pair with different swap use, not randomized statistics or GPU elapsed timing. Both runs pass two complete 562,342-value checks; retained final outputs are byte-identical. No new faults occur, both live services stay active, and units drain under the original 640 MiB RAM/128 MiB swap/300-second bounds and 64 MiB disk reserve. The result approximately restores the historical 34.303633-second fixed-core time under different conditions; it is not a CPU win or an end-to-end speech measurement. Japanese/frontend/runtime qualification and further optimization remain separate.

## Reproducing the source

[Exact source/artifact/runner/reference/review pins](results/explicit-robustness-0045.json) preserve both feature states and per-owner timing attribution. The isolated [ncnn campaign branch](https://github.com/jethac/ncnn/tree/radish/0045-explicit-robustness) contains the accepted 0042 source checkpoint, then separate API and four-tail commits. Accepted branches remain unchanged; no upstream PR is implied.

For a source checkout at the complete accepted 0042 ncnn stack, apply `patches/0045-ncnn-per-device-robustness.patch` and `patches/0045-ncnn-tail-loads.patch` with `git apply`. The separate `0045-model-explicit-device-loader.patch` applies at the Radish repository root to a private copy of `radish/tests/ncnn/model.cpp`. The standard accepted builder/source is intentionally unchanged. Rebuild the changed embedded shader headers and their owning `gpu.cpp` object, plus the model runner against the matching new `gpu.h`; reusing the old shader registry payload would omit the fixes. The measured private build verifies 261 unchanged archive members and unchanged shader catalog/indices.

Use the existing model CLI with `--vulkan --robust-buffer-access 1 --vulkan-loader /absolute/pinned/libvulkan.so.1`, then the same invocation with `0` only for an audited workload. Keep all model selectors, fixture/reference inputs and batching controls identical. The per-device flag is separate from the packed-A address-shift candidate, which has no result in this checkpoint.
