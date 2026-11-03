// SPDX-License-Identifier: MIT
// RadishAdaSnake: fused AdaIN (+ optional Snake) for rank-2 [W, C] FP32 pack4
// blobs on Vulkan, replacing Reshape/InstanceNorm/Reshape/(1+g)*n+b and
// y + inv_alpha * sin(alpha * y)^2 chains (scripts/fuse-student-adasnake.py).
// Inputs: x [W,C], g [C], b [C], norm weight [C], norm bias [C]
//         (+ alpha [C], inv_alpha [C] when snake=1). Params: 0=C 1=snake 2=eps.
// One workgroup of 128 invocations per four channels: two passes over W for
// the mean and variance, one for the output. GPU only.
#include "radish_ada_snake.h"
#include "layer.h"
#include "gpu.h"
#include "pipeline.h"
#include "command.h"
#include <cstdio>
#include <cstring>

namespace radish_ncnn {

static const char ada_snake_shader[] = R"(
#version 450
layout(constant_id = 0) const int snake = 0;
layout(constant_id = 1) const float eps = 1e-5;
layout(local_size_x = 128) in;
layout(binding = 0) readonly buffer x_blob { vec4 x_data[]; };
layout(binding = 1) readonly buffer g_blob { vec4 g_data[]; };
layout(binding = 2) readonly buffer b_blob { vec4 b_data[]; };
layout(binding = 3) readonly buffer nw_blob { vec4 nw_data[]; };
layout(binding = 4) readonly buffer nb_blob { vec4 nb_data[]; };
layout(binding = 5) readonly buffer a_blob { vec4 a_data[]; };
layout(binding = 6) readonly buffer ia_blob { vec4 ia_data[]; };
layout(binding = 7) writeonly buffer y_blob { vec4 y_data[]; };
layout(push_constant) uniform parameter { int w; int c4; } p;
shared vec4 red[128];
vec4 reduce(vec4 v) {
    const int t = int(gl_LocalInvocationID.x);
    red[t] = v;
    barrier();
    for (int s = 64; s > 0; s >>= 1) {
        if (t < s) red[t] += red[t + s];
        barrier();
    }
    vec4 r = red[0];
    barrier();
    return r;
}
void main() {
    const int q = int(gl_WorkGroupID.y);
    const int t = int(gl_LocalInvocationID.x);
    const int base = q * p.w;
    vec4 s = vec4(0.f);
    for (int i = t; i < p.w; i += 128) s += x_data[base + i];
    const vec4 mean = reduce(s) / float(p.w);
    s = vec4(0.f);
    for (int i = t; i < p.w; i += 128) { vec4 d = x_data[base + i] - mean; s += d * d; }
    const vec4 rstd = inversesqrt(reduce(s) / float(p.w) + eps);
    const vec4 g1 = g_data[q] + 1.f;
    const vec4 scale = rstd * nw_data[q] * g1;
    const vec4 shift = (nb_data[q] - mean * rstd * nw_data[q]) * g1 + b_data[q];
    vec4 alpha = vec4(0.f), ia = vec4(0.f);
    if (snake != 0) { alpha = a_data[q]; ia = ia_data[q]; }
    for (int i = t; i < p.w; i += 128) {
        vec4 y = x_data[base + i] * scale + shift;
        if (snake != 0) { vec4 sn = sin(alpha * y); y += ia * sn * sn; }
        y_data[base + i] = y;
    }
}
)";

class RadishAdaSnake final : public ncnn::Layer
{
public:
    RadishAdaSnake()
    {
        one_blob_only = false;
        support_inplace = false;
        support_packing = true;
#if NCNN_VULKAN
        support_vulkan = true;
        support_vulkan_packing = true;
#endif
    }
    ~RadishAdaSnake() override { destroy_pipeline(ncnn::Option()); }
    int load_param(const ncnn::ParamDict& pd) override
    {
        channels_ = pd.get(0, 0);
        snake_ = pd.get(1, 0);
        eps_ = pd.get(2, 1e-5f);
        return channels_ > 0 && channels_ % 4 == 0 ? 0 : -1;
    }
    int forward(const std::vector<ncnn::Mat>&, std::vector<ncnn::Mat>&, const ncnn::Option&) const override
    {
        std::fprintf(stderr, "RadishAdaSnake: CPU path not implemented (GPU only)\n");
        return -1;
    }
#if NCNN_VULKAN
    int create_pipeline(const ncnn::Option& opt) override
    {
        if (!opt.use_vulkan_compute) return 0;
        std::vector<uint32_t> spirv;
        ncnn::Option compile_opt = opt;
        compile_opt.vulkan_device_index = vkdev->info.device_index();
        if (ncnn::compile_spirv_module(ada_snake_shader, int(sizeof(ada_snake_shader) - 1), compile_opt, spirv))
            return -1;
        pipeline_ = new ncnn::Pipeline(vkdev);
        pipeline_->set_local_size_xyz(128, 1, 1);
        std::vector<ncnn::vk_specialization_type> sp(2);
        sp[0].i = snake_;
        sp[1].f = eps_;
        return pipeline_->create(spirv.data(), spirv.size() * sizeof(uint32_t), sp);
    }
    int destroy_pipeline(const ncnn::Option&) override
    {
        delete pipeline_;
        pipeline_ = 0;
        return 0;
    }
    int forward(const std::vector<ncnn::VkMat>& in, std::vector<ncnn::VkMat>& out,
                ncnn::VkCompute& cmd, const ncnn::Option& opt) const override
    {
        const size_t need = snake_ ? 7 : 5;
        if (!pipeline_ || in.size() != need || in[0].dims != 2 || in[0].elempack != 4
            || in[0].h * 4 != channels_)
            return -1;
        const ncnn::VkMat& x = in[0];
        ncnn::VkMat y(x.w, x.h, x.elemsize, x.elempack, opt.blob_vkallocator);
        if (y.empty()) return -100;
        std::vector<ncnn::VkMat> bindings(8);
        for (size_t i = 0; i < 7; i++) bindings[i] = i < in.size() ? in[i] : in[1];
        bindings[7] = y;
        std::vector<ncnn::vk_constant_type> constants(2);
        constants[0].i = x.w;
        constants[1].i = x.h;
        ncnn::VkMat dispatcher;
        dispatcher.w = 128;
        dispatcher.h = x.h;
        dispatcher.c = 1;
        cmd.record_pipeline(pipeline_, bindings, constants, dispatcher);
        out[0] = y;
        return 0;
    }
#endif
private:
    int channels_ = 0, snake_ = 0;
    float eps_ = 1e-5f;
#if NCNN_VULKAN
    ncnn::Pipeline* pipeline_ = 0;
#endif
};

static ncnn::Layer* create_ada_snake(void*) { return new RadishAdaSnake; }

int register_ada_snake(ncnn::Net& net)
{
    return net.register_custom_layer("RadishAdaSnake", create_ada_snake);
}

} // namespace radish_ncnn
