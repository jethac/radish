# Original ScratchSpace asset

The unchanged shader assembly in `../shaders/core-scratch.comp.spvasm` and
GLSL provenance here come from Vulkan-CTS revision
`5a9cb32498158eb60774cf8a3d4e0b8c68f7fe9e`, under the retained Apache 2.0
license and notice. [Pinned upstream asset](https://github.com/KhronosGroup/VK-GL-CTS/blob/5a9cb32498158eb60774cf8a3d4e0b8c68f7fe9e/external/vulkancts/data/vulkan/device_generated_commands/ScratchSpace.comp.spvasm).
The original DGC wrapper is retained for the four input
and output values. Radish uses an ordinary core compute adapter; this is
not an original DGC CTS pass and does not advertise DGC.

Build the assembly with `spirv-as --target-env vulkan1.0
--preserve-numeric-ids`, then validate it with `spirv-val --target-env
vulkan1.0`. Do not regenerate from GLSL or optimize the assembly. Native
code, private allocation and full numerical guard checks remain separate
admission gates under the existing bounded device scope.
