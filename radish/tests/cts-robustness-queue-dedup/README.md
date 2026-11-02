# Modified robustness host fixture

This is a one-condition host setup correction to Vulkan-CTS revision
`5a9cb32498158eb60774cf8a3d4e0b8c68f7fe9e`. When the universal and
dedicated-compute selections name the same queue family, append only one
queue-create record, as required by VUID02802. Distinct-family behavior,
shaders, inputs and numerical oracles are unchanged.

The candidate source retains upstream Apache-2.0 copyright and license
notices. Full upstream license/NOTICE are in `../scratch-provenance/`.
The isolated build reproduces original executable 0e715 before replacing only
`vktRobustnessUtil.cpp.o` in a copied archive. It leaves all original build
inputs and executable immutable. Modified executable 4295 outcomes are never
counted as unmodified CTS or official conformance passes.

Use `scripts/build-cts-robustness-queue-dedup.py` with `RADISH_WORK` pointing
to the retained pinned CTS source/build layout. It fails closed if the
original binary, source, archive reconstruction or baseline relink differs.
A fresh output directory is required. No driver workaround is included.
