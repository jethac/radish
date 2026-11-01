// SPDX-License-Identifier: MIT
#include "radish_norm_affine_snake.h"
#include "radish_affine_snake.h"

#include "layer.h"
#include "net.h"
#if NCNN_VULKAN
#include "allocator.h"
#include "gpu.h"
#include "layer/vulkan/instancenorm_vulkan.h"
#endif

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <set>
#include <vector>

namespace radish_ncnn {
namespace {

bool fp32_options(const ncnn::Option& opt)
{
    return !opt.use_fp16_packed && !opt.use_fp16_storage && !opt.use_fp16_arithmetic
        && !opt.use_fp16_uniform && !opt.use_bf16_packed && !opt.use_bf16_storage
        && !opt.use_int8_inference && !opt.use_int8_packed && !opt.use_int8_storage
        && !opt.use_int8_arithmetic && !opt.use_int8_uniform
        && !opt.use_int16_packed && !opt.use_int16_storage && !opt.use_tensor_storage;
}

template<class Mat>
bool shape(const Mat& m, int required_pack, size_t& count)
{
    if (m.dims != 2 || m.n != 1 || m.w <= 0 || m.h <= 0 || m.d != 1 || m.c != 1)
        return false;
    const int pack = m.elempack;
    if ((required_pack && pack != required_pack)
        || (pack != 1 && pack != 4 && pack != 8 && pack != 16)
        || m.elemsize != static_cast<size_t>(pack) * sizeof(float))
        return false;
    if (m.h != 128 / pack && m.h != 256 / pack)
        return false;
    if (static_cast<size_t>(m.w) > std::numeric_limits<size_t>::max() / m.h)
        return false;
    count = static_cast<size_t>(m.w) * m.h;
    if (count > std::numeric_limits<uint32_t>::max()
        || count > std::numeric_limits<size_t>::max() / m.elemsize
        || m.cstep != count || m.total() != count)
        return false;
#if NCNN_BATCH
    if (m.nstep != count)
        return false;
#endif
    return true;
}

template<class Mat>
bool input_shapes(const std::vector<Mat>& inputs, int required_pack)
{
    if (inputs.size() != 5)
        return false;
    size_t count = 0;
    for (size_t i = 0; i < inputs.size(); ++i)
        if (!shape(inputs[i], required_pack, count))
            return false;
    for (size_t i = 1; i < inputs.size(); ++i)
        if (inputs[i].w != 1 || inputs[i].h != inputs[0].h
            || inputs[i].elempack != inputs[0].elempack)
            return false;
    return true;
}

template<class Mat>
bool output_shape(const Mat& output, const Mat& input)
{
    size_t count = 0;
    return shape(output, input.elempack, count) && output.w == input.w && output.h == input.h;
}

template<class Mat>
bool hints_match(const std::vector<Mat>& inputs, const std::vector<ncnn::Mat>& hints)
{
    if (inputs.size() != hints.size())
        return false;
    for (size_t i = 0; i < inputs.size(); ++i)
        if (inputs[i].w != hints[i].w
            || inputs[i].h * inputs[i].elempack != hints[i].h * hints[i].elempack)
            return false;
    return true;
}

#if NCNN_VULKAN
const char norm_affine_snake_shader[] = R"glsl(#version 450
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(std430, binding = 0) readonly buffer X { vec4 x_data[]; };
layout(std430, binding = 1) readonly buffer Gamma { vec4 gamma_data[]; };
layout(std430, binding = 2) readonly buffer Beta { vec4 beta_data[]; };
layout(std430, binding = 3) readonly buffer Alpha { vec4 alpha_data[]; };
layout(std430, binding = 4) readonly buffer Reciprocal { vec4 reciprocal_data[]; };
layout(std430, binding = 5) readonly buffer Coefficients { vec4 coefficients_data[]; };
layout(std430, binding = 6) writeonly buffer Y { vec4 y_data[]; };
layout(push_constant) uniform parameter { uint width; uint packs; } p;
void main()
{
    uint j = gl_GlobalInvocationID.x;
    uint q = gl_GlobalInvocationID.y;
    if (j >= p.width || q >= p.packs) return;
    uint i = q * p.width + j;
    precise vec4 product = x_data[i] * coefficients_data[q * 2];
    precise vec4 normalized = product + coefficients_data[q * 2 + 1];
    precise vec4 gamma1 = gamma_data[q] + vec4(1.0);
    precise vec4 scaled = gamma1 * normalized;
    precise vec4 styled = scaled + beta_data[q];
    precise vec4 t0 = alpha_data[q] * styled;
    precise vec4 t1 = sin(t0);
    precise vec4 t2 = t1 * t1;
    precise vec4 t3 = reciprocal_data[q] * t2;
    precise vec4 y = styled + t3;
    y_data[i] = y;
}
)glsl";

// SPIR-V core OpDecorate=71, OpFAdd=129, OpFMul=133 and NoContraction=42.
// Check the generated program, not merely the GLSL spelling of precise.
bool no_contraction(const std::vector<uint32_t>& spv)
{
    if (spv.size() < 5 || spv[0] != 0x07230203u)
        return false;
    std::set<uint32_t> precise;
    std::vector<uint32_t> arithmetic;
    unsigned multiplies = 0, adds = 0, sines = 0;
    for (size_t i = 5; i < spv.size();)
    {
        const uint32_t words = spv[i] >> 16;
        const uint32_t op = spv[i] & 0xffffu;
        if (!words || words > spv.size() - i)
            return false;
        if (op == 71 && words == 3 && spv[i + 2] == 42)
            precise.insert(spv[i + 1]);
        if (op == 129 || op == 133)
        {
            if (words != 5)
                return false;
            arithmetic.push_back(spv[i + 2]);
            if (op == 129) ++adds;
            else ++multiplies;
        }
        if (op == 12 && words >= 5)
        {
            if (spv[i + 4] == 50) return false;
            if (spv[i + 4] == 13) ++sines;
        }
        i += words;
    }
    if (multiplies != 5 || adds != 4 || sines != 1)
        return false;
    for (uint32_t id : arithmetic)
        if (!precise.count(id))
            return false;
    return true;
}

bool radish_device(const ncnn::VulkanDevice* device)
{
    return device && device->is_valid() && device->info.vendor_id() == 0x5143
        && device->info.device_id() == 506
        && !std::strcmp(device->info.device_name(), "Radish Adreno 506 (experimental compute)");
}

bool gpu_options(const ncnn::Option& opt)
{
    return fp32_options(opt) && opt.use_vulkan_compute && opt.use_packing_layout
        && !opt.use_subgroup_ops && !opt.use_cooperative_matrix && !opt.use_shader_local_memory;
}

bool allocation(const ncnn::VkMat& m, const ncnn::VulkanDevice* device)
{
    size_t count = 0;
    if (!shape(m, 4, count) || !m.data || !m.refcount || *m.refcount <= 0 || !m.allocator
        || m.allocator->vkdev != device || !m.data->buffer || !m.data->memory)
        return false;
    const size_t bytes = count * m.elemsize;
    if (m.offset > m.data->capacity || bytes > m.data->capacity - m.offset
        || m.data->offset > std::numeric_limits<size_t>::max() - m.data->capacity
        || m.buffer_capacity() != bytes)
        return false;
    const VkPhysicalDeviceLimits& limits = device->info.physicalDeviceProperties().limits;
    const size_t alignment = device->info.buffer_offset_alignment();
    return alignment && m.buffer_offset() % alignment == 0 && m.buffer_offset() % 16 == 0
        && bytes <= limits.maxStorageBufferRange;
}

bool coefficients_allocation(const ncnn::VkMat& m, int packs,
                             const ncnn::VulkanDevice* device, ncnn::VkAllocator* allocator)
{
    if (!allocator || allocator->vkdev != device || m.allocator != allocator
        || m.dims != 1 || m.w != packs || m.h != 1 || m.d != 1 || m.c != 1 || m.n != 1
        || m.elempack != 8 || m.elemsize != 32u || packs <= 0 || m.cstep != static_cast<size_t>(packs)
        || m.total() != static_cast<size_t>(packs)
#if NCNN_BATCH
        || m.nstep != static_cast<size_t>(packs)
#endif
        || !m.data || !m.refcount || *m.refcount <= 0 || !m.data->buffer || !m.data->memory)
        return false;
    const size_t bytes = static_cast<size_t>(packs) * 32u;
    const size_t alignment = device->info.buffer_offset_alignment();
    return m.offset <= m.data->capacity && bytes <= m.data->capacity - m.offset
        && m.data->offset <= std::numeric_limits<size_t>::max() - m.data->capacity
        && m.buffer_capacity() == bytes && alignment && m.buffer_offset() % alignment == 0
        && m.buffer_offset() % 16 == 0
        && bytes <= device->info.physicalDeviceProperties().limits.maxStorageBufferRange;
}

bool disjoint(const ncnn::VkMat& a, const ncnn::VkMat& b)
{
    if (a.buffer() != b.buffer())
        return true;
    const size_t ao = a.buffer_offset(), bo = b.buffer_offset();
    return ao <= bo ? a.buffer_capacity() <= bo - ao : b.buffer_capacity() <= ao - bo;
}
#endif

} // namespace

struct NormAffineSnakeContext::Impl
{
    mutable std::mutex mutex;
    std::string error;
    bool registered = false;
    AffineSnakeContext cpu_affine_context;
#if NCNN_VULKAN
    bool compile_attempted = false;
    const ncnn::VulkanDevice* device = 0;
    std::vector<uint32_t> spirv;
#endif

    int fail(const char* reason)
    {
        std::lock_guard<std::mutex> lock(mutex);
        error = reason;
        std::fprintf(stderr, "RadishNormAffineSnake: %s\n", reason);
        return -1;
    }

#if NCNN_VULKAN
    int prepare(const ncnn::VulkanDevice* vkdev, const ncnn::Option& opt)
    {
        if (!radish_device(vkdev) || !gpu_options(opt))
            return fail("Vulkan requires the exact Radish A506 and FP32 pack4 buffer options");
        std::lock_guard<std::mutex> lock(mutex);
        if (compile_attempted)
        {
            if (device != vkdev || spirv.empty())
            {
                error = "context shader compilation failed or context was reused with another device";
                return -1;
            }
            return 0;
        }
        compile_attempted = true;
        device = vkdev;
        ncnn::Option compile_opt = opt;
        compile_opt.vulkan_device_index = vkdev->info.device_index();
        const int result = ncnn::compile_spirv_module(norm_affine_snake_shader,
            static_cast<int>(sizeof(norm_affine_snake_shader) - 1), compile_opt, spirv);
        if (result || !no_contraction(spirv))
        {
            spirv.clear();
            error = "shader compile or five-multiply/four-add NoContraction gate failed";
            std::fprintf(stderr, "RadishNormAffineSnake: %s\n", error.c_str());
            return -1;
        }
        return 0;
    }
#endif
};

class RadishNormAffineSnake final : public ncnn::Layer
{
public:
    explicit RadishNormAffineSnake(NormAffineSnakeContext::Impl& context) : context_(context)
    {
        one_blob_only = false;
        support_inplace = false;
        support_packing = true;
#if NCNN_VULKAN
        support_vulkan = true;
        support_vulkan_packing = true;
#endif
    }

    ~RadishNormAffineSnake() override { destroy_pipeline(cpu_opt_); }

    int load_param(const ncnn::ParamDict& params) override
    {
        for (int i = 0; i < 30; ++i)
            if (params.type(i))
                return context_.fail("operator parameters are unsupported");
        if (params.type(31) && params.get(31, -1) != 0)
            return context_.fail("feature masks are unsupported");
        return 0; // Parameter 30 is standard ncnn shape metadata.
    }

    int load_model(const ncnn::ModelBin&) override { return 0; }

    int create_pipeline(const ncnn::Option& opt) override
    {
        destroy_pipeline(cpu_opt_);
        if (!fp32_options(opt) || bottoms.size() != 5 || tops.size() != 1)
            return context_.fail("expected five inputs, one output and FP32 options");
        const int pack = opt.use_vulkan_compute ? 4 : 0;
        if (!input_shapes(bottom_shapes, pack) || top_shapes.size() != 1
            || !output_shape(top_shapes[0], bottom_shapes[0]))
            return context_.fail("missing or incompatible rank-two shape hints");

        cpu_opt_ = opt;
        cpu_opt_.use_vulkan_compute = false;
        vulkan_mode_ = opt.use_vulkan_compute;
        if (!vulkan_mode_)
        {
            cpu_[0] = ncnn::create_layer_cpu("Reshape");
            cpu_[1] = ncnn::create_layer_cpu("InstanceNorm");
            cpu_[2] = ncnn::create_layer_cpu("Reshape");
            cpu_[3] = AffineSnakeContext::create(&context_.cpu_affine_context);
            const int channels = bottom_shapes[0].h * bottom_shapes[0].elempack;
            for (size_t i = 0; i < 4; ++i)
            {
                ncnn::ParamDict params;
                if (i == 0) { params.set(0, -1); params.set(1, 1); params.set(2, channels); }
                if (i == 1) { params.set(0, channels); params.set(1, 1.e-5f); params.set(2, 0); }
                if (i == 2) { params.set(0, -1); params.set(1, channels); }
                if (i == 3 && cpu_[i])
                {
                    cpu_[i]->bottoms.resize(5);
                    cpu_[i]->tops.resize(1);
                    cpu_[i]->bottom_shapes = bottom_shapes;
                    cpu_[i]->top_shapes = top_shapes;
                }
                if (!cpu_[i] || cpu_[i]->load_param(params) || cpu_[i]->create_pipeline(cpu_opt_))
                {
                    destroy_pipeline(cpu_opt_);
                    return context_.fail("CPU original-chain subordinate creation failed");
                }
            }
        }
#if NCNN_VULKAN
        if (opt.use_vulkan_compute)
        {
            const char* sum4 = std::getenv("RADISH_NCNN_NORM_SUM4");
            if (!radish_device(vkdev) || !gpu_options(opt) || !sum4 || std::strcmp(sum4, "1")
                || bottom_shapes[0].w > INT_MAX - 3
                || static_cast<size_t>(bottom_shapes[0].w) >
                    vkdev->info.physicalDeviceProperties().limits.maxStorageBufferRange / 16u / bottom_shapes[0].h)
                return context_.fail("Vulkan requires selected SUM4 on bounded Radish FP32 pack4 hints");
            norm_ = new (std::nothrow) ncnn::InstanceNorm_vulkan;
            if (!norm_) return -100;
            norm_->vkdev = vkdev;
            const ncnn::Mat& raw_hint = bottom_shapes[0];
            const int channels = raw_hint.h * raw_hint.elempack;
            ncnn::ParamDict params;
            params.set(0, channels);
            params.set(1, 1.e-5f);
            params.set(2, 0);
            const ncnn::Mat hint(raw_hint.w, 1, raw_hint.h, (void*)0, 16u, 4);
            norm_->bottom_shapes.assign(1, hint);
            norm_->top_shapes.assign(1, hint);
            if (norm_->load_param(params) || norm_->create_pipeline(opt)
                || !norm_->pipeline_instancenorm_radish_sum4_pack4
                || !norm_->pipeline_instancenorm_radish_sum4_pack4->pipeline())
            {
                destroy_pipeline(cpu_opt_);
                return context_.fail("normalization requires selected SUM4 with known FP32 pack4 hints");
            }
            if (context_.prepare(vkdev, opt))
            {
                destroy_pipeline(cpu_opt_);
                return -1;
            }
            pipeline_ = new (std::nothrow) ncnn::Pipeline(vkdev);
            if (!pipeline_)
            {
                destroy_pipeline(cpu_opt_);
                return -100;
            }
            pipeline_->set_local_size_xyz(32, 1, 1);
            const std::vector<ncnn::vk_specialization_type> specializations;
            const int result = pipeline_->create(context_.spirv.data(),
                context_.spirv.size() * sizeof(uint32_t), specializations);
            if (result || !pipeline_->pipeline() || pipeline_->local_size_x() != 32
                || pipeline_->local_size_y() != 1 || pipeline_->local_size_z() != 1
                || pipeline_->shader_info().binding_count != 7
                || pipeline_->shader_info().push_constant_count != 2
                || pipeline_->shader_info().specialization_count != 0)
            {
                destroy_pipeline(cpu_opt_);
                return context_.fail("Vulkan pipeline creation or layout validation failed");
            }
            for (int i = 0; i < 7; ++i)
                if (pipeline_->shader_info().binding_types[i] != 1)
                {
                    destroy_pipeline(cpu_opt_);
                    return context_.fail("Vulkan shader requires seven storage-buffer bindings");
                }
        }
#else
        if (opt.use_vulkan_compute)
        {
            destroy_pipeline(cpu_opt_);
            return context_.fail("this build has no Vulkan support");
        }
#endif
        ready_ = true;
        return 0;
    }

    int destroy_pipeline(const ncnn::Option&) override
    {
        ready_ = false;
#if NCNN_VULKAN
        delete pipeline_;
        pipeline_ = 0;
        if (norm_)
        {
            norm_->destroy_pipeline(cpu_opt_);
            delete norm_;
            norm_ = 0;
        }
#endif
        int result = 0;
        for (ncnn::Layer*& layer : cpu_)
        {
            if (!layer) continue;
            if (layer->destroy_pipeline(cpu_opt_)) result = -1;
            delete layer;
            layer = 0;
        }
        return result;
    }

    int forward(const std::vector<ncnn::Mat>& inputs, std::vector<ncnn::Mat>& outputs,
                const ncnn::Option& opt) const override
    {
        if (!ready_ || vulkan_mode_ || opt.use_vulkan_compute || !fp32_options(opt)
            || !input_shapes(inputs, 0) || !hints_match(inputs, bottom_shapes) || outputs.size() != 1)
            return context_.fail("invalid CPU input/output contract");
        for (const ncnn::Mat& input : inputs)
            if (!input.data || !input.refcount)
                return context_.fail("CPU inputs must have owned FP32 storage");
        ncnn::Option cpu_opt = opt;
        cpu_opt.use_vulkan_compute = false;
        ncnn::Mat rank3;
        int status = cpu_[0]->forward(inputs[0], rank3, cpu_opt);
        if (status || rank3.empty()) return status ? status : -100;
        if (!cpu_[1]->support_packing && rank3.elempack != 1)
        {
            ncnn::Mat unpacked;
            ncnn::convert_packing(rank3, unpacked, 1, cpu_opt);
            rank3 = unpacked;
            if (rank3.empty()) return -100;
        }
        if (!rank3.refcount) return context_.fail("CPU reshape must own its allocation");
        if (rank3.refcount == inputs[0].refcount || *rank3.refcount != 1)
        {
            rank3 = rank3.clone(cpu_opt.blob_allocator);
            if (rank3.empty()) return -100;
        }
        status = cpu_[1]->forward_inplace(rank3, cpu_opt);
        if (status) return status;
        ncnn::Mat normalized;
        status = cpu_[2]->forward(rank3, normalized, cpu_opt);
        if (status || normalized.empty()) return status ? status : -100;
        std::vector<ncnn::Mat> affine_inputs(inputs);
        affine_inputs[0] = normalized;
        std::vector<ncnn::Mat> final(1);
        status = cpu_[3]->forward(affine_inputs, final, cpu_opt);
        if (status || final[0].empty()) return status ? status : -100;
        if (!output_shape(final[0], inputs[0]))
            return context_.fail("CPU original-chain output shape changed");
        outputs[0] = final[0];
        return 0;
    }

#if NCNN_VULKAN
    int forward(const std::vector<ncnn::VkMat>& inputs, std::vector<ncnn::VkMat>& outputs,
                ncnn::VkCompute& command, const ncnn::Option& opt) const override
    {
        if (!ready_ || !vulkan_mode_ || !pipeline_ || !norm_ || !gpu_options(opt) || !radish_device(vkdev)
            || context_.device != vkdev || !input_shapes(inputs, 4)
            || !hints_match(inputs, bottom_shapes) || outputs.size() != 1
            || !opt.blob_vkallocator || opt.blob_vkallocator->vkdev != vkdev)
            return context_.fail("invalid Vulkan input/output contract");
        for (const ncnn::VkMat& input : inputs)
            if (!allocation(input, vkdev))
                return context_.fail("invalid Vulkan buffer allocation or range");
        const ncnn::VkMat& x = inputs[0];
        const uint32_t groups_x = static_cast<uint32_t>((static_cast<uint64_t>(x.w) + 31) / 32);
        if (groups_x > vkdev->info.max_workgroup_count_x()
            || static_cast<uint32_t>(x.h) > vkdev->info.max_workgroup_count_y())
            return context_.fail("dispatch exceeds advertised workgroup counts");
        ncnn::VkMat output(x.w, x.h, 16u, 4, opt.blob_vkallocator);
        if (output.empty()) return -100;
        if (!allocation(output, vkdev))
            return context_.fail("invalid Vulkan output allocation");
        for (const ncnn::VkMat& input : inputs)
            if (!disjoint(output, input))
                return context_.fail("output allocation overlaps an input");
        ncnn::VkMat rank3 = x;
        rank3.dims = 3;
        rank3.h = 1;
        rank3.c = x.h;
        rank3.cstep = x.w;
        ncnn::VkMat coefficients;
        const int status = norm_->forward_radish_coefficients(rank3, coefficients, command, opt);
        if (status) return status;
        if (!coefficients_allocation(coefficients, x.h, vkdev, opt.workspace_vkallocator)
            || !disjoint(output, coefficients))
            return context_.fail("invalid normalization coefficient allocation");
        for (const ncnn::VkMat& input : inputs)
            if (!disjoint(coefficients, input))
                return context_.fail("normalization coefficient allocation overlaps an input");
        std::vector<ncnn::VkMat> bindings(inputs);
        bindings.push_back(coefficients);
        bindings.push_back(output);
        std::vector<ncnn::vk_constant_type> constants(2);
        constants[0].u32 = static_cast<uint32_t>(x.w);
        constants[1].u32 = static_cast<uint32_t>(x.h);
        ncnn::VkMat dispatcher;
        dispatcher.w = x.w;
        dispatcher.h = x.h;
        dispatcher.c = 1;
        command.record_pipeline(pipeline_, bindings, constants, dispatcher);
        outputs[0] = output;
        return 0;
    }
#endif

private:
    NormAffineSnakeContext::Impl& context_;
    ncnn::Layer* cpu_[4] = {0, 0, 0, 0};
    ncnn::Option cpu_opt_;
    bool ready_ = false;
    bool vulkan_mode_ = false;
#if NCNN_VULKAN
    ncnn::Pipeline* pipeline_ = 0;
    ncnn::InstanceNorm_vulkan* norm_ = 0;
#endif
};

NormAffineSnakeContext::NormAffineSnakeContext() : impl_(new Impl) {}
NormAffineSnakeContext::~NormAffineSnakeContext() = default;

int NormAffineSnakeContext::register_layer(ncnn::Net& net)
{
    if (impl_->registered || !net.layers().empty())
        return impl_->fail("register exactly once on an empty Net before loading parameters");
    if (!fp32_options(net.opt))
        return impl_->fail("registration requires FP32 options");
    const int result = net.register_custom_layer("RadishNormAffineSnake", create, destroy, this);
    if (result) return impl_->fail("custom-layer registration failed");
    impl_->registered = true;
    return 0;
}

std::string NormAffineSnakeContext::last_error() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

unsigned NormAffineSnakeContext::shader_compile_attempts() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
#if NCNN_VULKAN
    return impl_->compile_attempted ? 1u : 0u;
#else
    return 0;
#endif
}

size_t NormAffineSnakeContext::compiled_spirv_words() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
#if NCNN_VULKAN
    return impl_->spirv.size();
#else
    return 0;
#endif
}

ncnn::Layer* NormAffineSnakeContext::create(void* context)
{
    if (!context) return 0;
    return new (std::nothrow) RadishNormAffineSnake(*static_cast<NormAffineSnakeContext*>(context)->impl_);
}

void NormAffineSnakeContext::destroy(ncnn::Layer* layer, void*) { delete layer; }

} // namespace radish_ncnn
