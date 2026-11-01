#ifndef RADISH_KOKORO_SHAPES_H
#define RADISH_KOKORO_SHAPES_H

#include <string>
#include <vector>

namespace radish {

// Logical FP32 scalar dimensions, before ncnn applies Vulkan packing.
struct KokoroShape {
    int dims, w, h, d, c;
};

struct KokoroShapeRecord {
    int layer, blob;
    std::string name;
    KokoroShape shape;
};

struct KokoroShapePlan {
    std::string param_text;
    std::vector<KokoroShapeRecord> shapes;
    int layers, blobs;
};

// Supports the audited 1368/1739 layout graph, 1098/1307 InstanceNorm and
// 906/1067 RadishSnake, 714/875 RadishAffineSnake, and their height-one
// deconvolution variants 912/1073 and 720/881, at T=1..4096.
// Replaces existing -23330 hints,
// including stale ones,
// while retaining every other parameter token, layer, and blob in order.
// No weights, tensor allocation/execution, ncnn, or Vulkan are needed.
// Throws std::runtime_error on unsupported metadata, malformed graphs, or
// integer overflow. Callers must independently pin the graph and weight files.
KokoroShapePlan infer_kokoro_shapes(const std::string& scalar_param_text, int time_frames);

} // namespace radish
#endif
