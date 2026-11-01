// SPDX-License-Identifier: MIT
#ifndef RADISH_NCNN_AFFINE_SNAKE_H
#define RADISH_NCNN_AFFINE_SNAKE_H

#include <cstddef>
#include <memory>
#include <string>

namespace ncnn {
class Layer;
class Net;
}

namespace radish_ncnn {

class RadishAffineSnake;
class RadishNormAffineSnake;

// One context per Net. Register before load_param, after configuring Net options.
// The context must outlive the Net (including its registry and every custom
// layer), and the VulkanDevice/GPU instance must outlive both. Finish all command
// work before destroying the Net. Declare objects in this order:
//     GPU/device owner, AffineSnakeContext, Net, extractor/command work.
// Registration and model loading are serialized; normal forwards may run in
// parallel. A context cannot be registered again or moved to another device.
//
// RadishAffineSnake has five rank-two inputs: normalized x[W,C], gamma[1,C],
// beta[1,C], alpha[1,C], and the separately stored reciprocal[1,C], C=128/256.
// It consumes no weights/operator parameters and requires matching shape hints.
// CPU execution delegates the original eight operators in their original order.
// Vulkan requires exact Radish A506 FP32 pack4 buffers and records one fresh-output
// dispatch. Every intermediate stays FP32; gamma/beta are not folded and the
// reciprocal is not recomputed. Unsupported modes fail. This module neither
// rewrites nor selects a graph and does not alter the accepted RadishSnake type.
class AffineSnakeContext
{
public:
    AffineSnakeContext();
    ~AffineSnakeContext();

    int register_layer(ncnn::Net& net);
    std::string last_error() const;
    // Read-only probe diagnostics; both are zero before a Vulkan pipeline is
    // requested. A failed compile is still an attempt and is never retried.
    unsigned shader_compile_attempts() const;
    size_t compiled_spirv_words() const;

    AffineSnakeContext(const AffineSnakeContext&) = delete;
    AffineSnakeContext& operator=(const AffineSnakeContext&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    static ncnn::Layer* create(void* context);
    static void destroy(ncnn::Layer* layer, void* context);
    friend class RadishAffineSnake;
    friend class RadishNormAffineSnake;
};

} // namespace radish_ncnn
#endif
