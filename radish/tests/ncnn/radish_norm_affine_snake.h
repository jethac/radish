// SPDX-License-Identifier: MIT
#ifndef RADISH_NCNN_NORM_AFFINE_SNAKE_H
#define RADISH_NCNN_NORM_AFFINE_SNAKE_H

#include <cstddef>
#include <memory>
#include <string>

namespace ncnn { class Layer; class Net; }
namespace radish_ncnn {
class RadishNormAffineSnake;

// One context per Net, configured and registered before load_param. The context
// outlives every layer/Net and the GPU/device owner outlives both; finish command
// work before destroying them. The context cannot be registered again or moved
// to another device. Pipeline creation is serialized; forwards use local state.
//
// Five rank2 FP32 inputs: raw[W,C], gamma/beta/alpha/stored_reciprocal[1,C],
// C128/256. Fixed normalization epsilon1e-5, affine0, no operator parameters or
// weights. CPU delegates the original Reshape/InstanceNorm/Reshape/AffineSnake
// chain with packing preserved. GPU requires exact Radish506 FP32 pack4 and
// selected SUM4 pipelines, then records one fresh-output combined final stage.
// Existing graphs and the separate AffineSnake type are unchanged.
class NormAffineSnakeContext
{
public:
    NormAffineSnakeContext();
    ~NormAffineSnakeContext();
    int register_layer(ncnn::Net& net);
    std::string last_error() const;
    unsigned shader_compile_attempts() const;
    size_t compiled_spirv_words() const;
    NormAffineSnakeContext(const NormAffineSnakeContext&) = delete;
    NormAffineSnakeContext& operator=(const NormAffineSnakeContext&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    static ncnn::Layer* create(void* context);
    static void destroy(ncnn::Layer* layer, void* context);
    friend class RadishNormAffineSnake;
};
} // namespace radish_ncnn
#endif
