// SPDX-License-Identifier: MIT
#ifndef RADISH_NCNN_SNAKE_H
#define RADISH_NCNN_SNAKE_H

#include <cstddef>
#include <memory>
#include <string>

namespace ncnn {
class Layer;
class Net;
}

namespace radish_ncnn {

class RadishSnake;

// One context per Net. Register before load_param, after configuring Net options.
// The context must outlive the Net (including its registry and every custom
// layer), and the VulkanDevice/GPU instance must outlive both. Finish all command
// work before destroying the Net. Declare objects in this order:
//     GPU/device owner, SnakeContext, Net, extractor/command work.
// Registration and model loading are serialized; normal forwards may run in
// parallel. A context cannot be registered again or moved to another device.
//
// RadishSnake has three rank-two inputs: x[W,C], alpha[1,C], and the separately
// stored reciprocal[1,C], with C=128 or 256. It consumes no weights or operator
// parameters and requires complete, matching shape hints. CPU execution calls
// the original five ncnn operators; Vulkan requires the exact Radish A506,
// FP32 pack4 buffer storage, and records one fresh-output dispatch. Unsupported
// precision/layout/device modes fail rather than silently selecting another
// implementation. This module does not rewrite or select a model graph.
class SnakeContext
{
public:
    SnakeContext();
    ~SnakeContext();

    int register_layer(ncnn::Net& net);
    std::string last_error() const;
    // Read-only probe diagnostics; both are zero before a Vulkan pipeline is
    // requested. A failed compile is still an attempt and is never retried.
    unsigned shader_compile_attempts() const;
    size_t compiled_spirv_words() const;

    SnakeContext(const SnakeContext&) = delete;
    SnakeContext& operator=(const SnakeContext&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    static ncnn::Layer* create(void* context);
    static void destroy(ncnn::Layer* layer, void* context);
    friend class RadishSnake;
};

} // namespace radish_ncnn
#endif
