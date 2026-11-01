#include "allocator.h"
#include "command.h"
#include "gpu.h"
#include "layer_shader_type.h"
#include "pipeline.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {

const uint16_t guard_half = 0x3555;
const size_t guard_scalars = 16;
int unpack_subnormals = -1;
size_t subnormal_checks = 0;

struct Instance {
    ~Instance() { ncnn::destroy_gpu_instance(); }
};

struct Allocators {
    const ncnn::VulkanDevice* device;
    ncnn::Option& opt;
    Allocators(const ncnn::VulkanDevice* d, ncnn::Option& o) : device(d), opt(o)
    {
        opt.blob_vkallocator = device->acquire_blob_allocator();
        opt.workspace_vkallocator = opt.blob_vkallocator;
        opt.staging_vkallocator = device->acquire_staging_allocator();
    }
    ~Allocators()
    {
        if (opt.blob_vkallocator) device->reclaim_blob_allocator(opt.blob_vkallocator);
        if (opt.staging_vkallocator) device->reclaim_staging_allocator(opt.staging_vkallocator);
    }
};

int create_packing(ncnn::Pipeline& pipeline, int shader, int from, int to,
                   const ncnn::Option& opt)
{
    std::vector<ncnn::vk_specialization_type> spec(5);
    spec[0].i = from;
    spec[1].i = to;
    pipeline.set_local_size_xyz(32, 1, 1);
    CHECK(pipeline.create(shader, opt, spec) == 0);
    CHECK(pipeline.pipeline() != VK_NULL_HANDLE);
    CHECK(pipeline.local_size_x() == 32 && pipeline.local_size_y() == 1 && pipeline.local_size_z() == 1);
    CHECK(pipeline.shader_info().binding_count == 4 && pipeline.shader_info().push_constant_count == 3);
    return 0;
}

int initialize(ncnn::VkMat& buffer, bool fp32)
{
    CHECK(!buffer.empty() && buffer.allocator->mappable && buffer.mapped_ptr());
    if (fp32) {
        float* ptr = static_cast<float*>(buffer.mapped_ptr());
        for (size_t i = 0; i < buffer.buffer_capacity() / sizeof(float); ++i)
            ptr[i] = ncnn::float16_to_float32(guard_half);
    } else {
        uint16_t* ptr = static_cast<uint16_t*>(buffer.mapped_ptr());
        for (size_t i = 0; i < buffer.buffer_capacity() / sizeof(uint16_t); ++i)
            ptr[i] = guard_half;
    }
    buffer.data->access_flags = VK_ACCESS_HOST_WRITE_BIT;
    buffer.data->stage_flags = VK_PIPELINE_STAGE_HOST_BIT;
    CHECK(buffer.allocator->flush(buffer.data) == 0);
    return 0;
}

int record_packing(ncnn::VkCompute& command, const ncnn::Pipeline& pipeline,
                   const ncnn::VkMat& input, const ncnn::VkMat& output,
                   unsigned n, unsigned stride)
{
    std::vector<ncnn::VkMat> bindings = {input, input, output, output};
    std::vector<ncnn::vk_constant_type> constants(3);
    constants[0].u32 = n;
    constants[1].u32 = 1;
    constants[2].u32 = stride;
    ncnn::VkMat dispatcher;
    dispatcher.w = n;
    dispatcher.h = 1;
    dispatcher.c = 1;
    const uint64_t before = command.pending_dispatch_total();
    command.record_pipeline(&pipeline, bindings, constants, dispatcher);
    CHECK(command.pending_dispatch_total() > before);
    return 0;
}

bool half_result(float input, uint16_t bits)
{
    const float actual = ncnn::float16_to_float32(bits);
    if (!std::isfinite(actual)) return false;
    if (input == 0.f) return actual == 0.f;
    const double magnitude = std::fabs(static_cast<double>(input));
    const double minimum_normal = std::ldexp(1.0, -14);
    int exponent;
    std::frexp(magnitude, &exponent);
    const double step = std::ldexp(1.0, magnitude < minimum_normal ? -24 : exponent - 11);
    const double low = std::floor(magnitude / step) * step;
    const double high = std::ceil(magnitude / step) * step;
    const double signed_low = std::copysign(low, input);
    const double signed_high = std::copysign(high, input);
    return actual == signed_low || actual == signed_high ||
           (magnitude < minimum_normal && actual == 0.f);
}

int unpack_result(uint16_t bits, float actual)
{
    const float expected = ncnn::float16_to_float32(bits);
    CHECK(std::isfinite(actual));
    if ((bits & 0x7c00) == 0 && (bits & 0x03ff) != 0) {
        const int mode = actual == expected ? 1 : actual == 0.f ? 0 : -1;
        CHECK(mode >= 0);
        CHECK(unpack_subnormals < 0 || unpack_subnormals == mode);
        unpack_subnormals = mode;
        ++subnormal_checks;
    } else {
        CHECK(actual == expected);
    }
    return 0;
}

int check_guards(const ncnn::Mat& half, const ncnn::Mat& full, size_t count)
{
    const uint16_t* h = static_cast<const uint16_t*>(half.data);
    const float* f = static_cast<const float*>(full.data);
    CHECK(half.total() * half.elempack >= count + guard_scalars);
    CHECK(full.total() * full.elempack >= count + guard_scalars);
    for (size_t i = count; i < half.total() * half.elempack; ++i) CHECK(h[i] == guard_half);
    for (size_t i = count; i < full.total() * full.elempack; ++i)
        CHECK(f[i] == ncnn::float16_to_float32(guard_half));
    return 0;
}

float sample(size_t index, unsigned repeat)
{
    const float values[] = {
        0.f, 1.f, -2.f, 0.5f, -0.125f, 1.00048828125f, -1.00048828125f,
        1.0006f, -1.0006f, 1.00146484375f, 0.3333f, -0.3333f,
        0.00006103515625f, 0.000030517578125f, -0.000030517578125f,
        0.000000059604644775390625f, 0.0000000298023223876953125f,
        65504.f, -65504.f, 16.f, -8.f, 0.25f, 2.f, -4.f
    };
    return values[(index + repeat * 7) % (sizeof(values) / sizeof(values[0]))];
}

int check_seeded_unpack(const ncnn::VulkanDevice* device, const ncnn::Option& opt,
                        const ncnn::Pipeline& unpack)
{
    const uint16_t values[] = {0x0001, 0x03ff, 0x8001, 0x83ff, 0x0400, 0x8400, 0x7bff, 0};
    ncnn::VkMat packed(6, size_t(8), 4, opt.blob_vkallocator);
    ncnn::VkMat output(6, size_t(16), 4, opt.blob_vkallocator);
    CHECK(initialize(packed, false) == 0 && initialize(output, true) == 0);
    std::memcpy(packed.mapped_ptr(), values, sizeof(values));
    CHECK(packed.allocator->flush(packed.data) == 0);
    ncnn::Mat half_readback, full_readback;
    ncnn::VkCompute command(device);
    CHECK(record_packing(command, unpack, packed, output, 2, 2) == 0);
    command.record_clone(packed, half_readback, opt);
    command.record_clone(output, full_readback, opt);
    CHECK(command.pending_dispatch_total() > 0 && command.submit_and_wait() == 0);
    CHECK(!half_readback.empty() && !full_readback.empty());
    CHECK(std::memcmp(half_readback.data, values, sizeof(values)) == 0);
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        CHECK(unpack_result(values[i], static_cast<const float*>(full_readback.data)[i]) == 0);
    CHECK(check_guards(half_readback, full_readback, 8) == 0);
    std::printf("PASS: seeded binary16 subnormal unpack %s, normal boundaries and guards\n",
                unpack_subnormals ? "preserved" : "flushed");
    return 0;
}

int check_partial_words(const ncnn::VulkanDevice* device, const ncnn::Option& opt,
                         const ncnn::Pipeline& scatter, unsigned stride, unsigned repeat)
{
    const unsigned width = 3;
    const size_t allocation = (4u * stride + guard_scalars + 7) / 8 * 8;
    ncnn::VkMat input(8, size_t(16), 4, opt.blob_vkallocator);
    ncnn::VkMat packed(allocation, size_t(2), 1, opt.blob_vkallocator);
    CHECK(initialize(input, true) == 0 && initialize(packed, false) == 0);
    float* src = static_cast<float*>(input.mapped_ptr());
    for (unsigned i = 0; i < width * 4; ++i) src[i] = sample(i, repeat);
    CHECK(input.allocator->flush(input.data) == 0);
    ncnn::Mat half_readback;
    ncnn::VkCompute command(device);
    CHECK(record_packing(command, scatter, input, packed, width, stride) == 0);
    command.record_clone(packed, half_readback, opt);
    CHECK(command.pending_dispatch_total() > 0 && command.submit_and_wait() == 0);
    CHECK(!half_readback.empty() && half_readback.total() >= allocation);
    const uint16_t* result = static_cast<const uint16_t*>(half_readback.data);
    for (size_t i = 0; i < half_readback.total(); ++i) {
        const size_t row = i / stride;
        const size_t x = i % stride;
        if (row < 4 && x < width) {
            CHECK(half_result(sample(x * 4 + row, repeat), result[i]));
        } else if (result[i] != guard_half) {
            std::fprintf(stderr, "FAIL adjacent-half guard stride=%u repeat=%u index=%zu got=%04x expected=%04x\n",
                         stride, repeat, i, result[i], guard_half);
            return 1;
        }
    }
    std::printf("PASS: scalar CAS width=3 stride=%u repeat=%u all active values and inactive halfwords\n", stride, repeat);
    return 0;
}

int run_case(const ncnn::VulkanDevice* device, const ncnn::Option& opt,
             const ncnn::Pipeline& pack, const ncnn::Pipeline& scatter,
             const ncnn::Pipeline& unpack, const ncnn::Pipeline& relu,
             unsigned width, bool scalar, unsigned repeat)
{
    const size_t count = 4u * width;
    const size_t allocation = (count + guard_scalars + 7) / 8 * 8;
    ncnn::VkMat input(allocation / 4, size_t(16), 4, opt.blob_vkallocator);
    ncnn::VkMat packed(scalar ? allocation : allocation / 4,
                       scalar ? size_t(2) : size_t(8), scalar ? 1 : 4, opt.blob_vkallocator);
    ncnn::VkMat output(allocation / 4, size_t(16), 4, opt.blob_vkallocator);
    CHECK(initialize(input, true) == 0 && initialize(packed, false) == 0 && initialize(output, true) == 0);
    float* src = static_cast<float*>(input.mapped_ptr());
    for (size_t i = 0; i < count; ++i) src[i] = sample(i, repeat);
    CHECK(input.allocator->flush(input.data) == 0);
    std::vector<float> reference(count);
    for (size_t i = 0; i < count; ++i)
        reference[i] = src[scalar ? (i % width) * 4 + i / width : i];

    ncnn::Mat half_readback, full_readback;
    {
        ncnn::VkCompute command(device);
        CHECK(record_packing(command, scalar ? scatter : pack, input, packed, width, width) == 0);
        CHECK(record_packing(command, unpack, packed, output, width, width) == 0);
        command.record_clone(packed, half_readback, opt);
        command.record_clone(output, full_readback, opt);
        CHECK(command.pending_dispatch_total() > 0 && command.submit_and_wait() == 0);
    }
    CHECK(!half_readback.empty() && !full_readback.empty());
    const uint16_t* initial = static_cast<const uint16_t*>(half_readback.data);
    const float* converted = static_cast<const float*>(full_readback.data);
    std::vector<float> activation_reference(count);
    for (size_t i = 0; i < count; ++i) {
        if (!half_result(reference[i], initial[i])) {
            std::fprintf(stderr, "FAIL conversion scalar=%d width=%u index=%zu input=%.9g half=%04x\n",
                         scalar, width, i, reference[i], initial[i]);
            return 1;
        }
        CHECK(unpack_result(initial[i], converted[i]) == 0);
        activation_reference[i] = converted[i] < 0.f ? converted[i] * 0.125f : converted[i];
    }
    CHECK(check_guards(half_readback, full_readback, count) == 0);

    {
        ncnn::VkCompute command(device);
        std::vector<ncnn::vk_constant_type> constants(1);
        constants[0].u32 = width;
        ncnn::VkMat dispatcher;
        dispatcher.w = width;
        dispatcher.h = 1;
        dispatcher.c = 1;
        const uint64_t before = command.pending_dispatch_total();
        command.record_pipeline(&relu, std::vector<ncnn::VkMat>{packed}, constants, dispatcher);
        CHECK(command.pending_dispatch_total() > before);
        CHECK(record_packing(command, unpack, packed, output, width, width) == 0);
        command.record_clone(packed, half_readback, opt);
        command.record_clone(output, full_readback, opt);
        CHECK(command.submit_and_wait() == 0);
    }
    const uint16_t* activated = static_cast<const uint16_t*>(half_readback.data);
    const float* final = static_cast<const float*>(full_readback.data);
    for (size_t i = 0; i < count; ++i) {
        if (!half_result(activation_reference[i], activated[i])) {
            std::fprintf(stderr, "FAIL ReLU scalar=%d width=%u index=%zu reference=%.9g half=%04x\n",
                         scalar, width, i, activation_reference[i], activated[i]);
            return 1;
        }
        CHECK(unpack_result(activated[i], final[i]) == 0);
    }
    CHECK(check_guards(half_readback, full_readback, count) == 0);
    std::printf("PASS: %s width=%u scalars=%zu repeat=%u conversion/ReLU/readback and guards\n",
                scalar ? "scalar four-row CAS" : "pack4", width, count, repeat);
    return 0;
}

}

int main(int argc, char** argv)
{
    CHECK(argc == 1 || (argc == 2 && !std::strcmp(argv[1], "--compile-only")));
    const bool compile_only = argc == 2;
    CHECK(ncnn::create_gpu_instance() == 0);
    Instance instance;
    CHECK(ncnn::get_gpu_count() == 1);
    const ncnn::VulkanDevice* device = ncnn::get_gpu_device(0);
    CHECK(device && device->is_valid());
    CHECK(device->info.support_fp16_packed());
    CHECK(!device->info.support_fp16_storage() && !device->info.support_fp16_arithmetic());
    std::printf("ncnn device: %s; FP16 packed storage with FP32 arithmetic\n", device->info.device_name());
    ncnn::Option opt;
    opt.num_threads = 1;
    opt.use_vulkan_compute = true;
    opt.use_packing_layout = true;
    opt.use_fp16_packed = true;
    opt.use_fp16_storage = opt.use_fp16_uniform = opt.use_fp16_arithmetic = false;
    opt.use_bf16_packed = opt.use_bf16_storage = false;
    opt.use_int8_inference = opt.use_int8_packed = opt.use_int8_storage = false;
    opt.use_int8_arithmetic = opt.use_int8_uniform = false;
    opt.use_int16_packed = opt.use_int16_storage = false;
    opt.use_subgroup_ops = opt.use_cooperative_matrix = opt.use_shader_local_memory = false;
    Allocators allocators(device, opt);
    CHECK(opt.blob_vkallocator && opt.staging_vkallocator);
    ncnn::Pipeline pack(device), scatter(device), unpack(device), relu(device);
    CHECK(create_packing(pack, ncnn::LayerShaderType::packing, 1, 2, opt) == 0);
    CHECK(create_packing(scatter, ncnn::LayerShaderType::packing_pack4to1, 1, 2, opt) == 0);
    CHECK(create_packing(unpack, ncnn::LayerShaderType::packing, 2, 1, opt) == 0);
    std::vector<ncnn::vk_specialization_type> spec(2);
    spec[0].f = 0.125f;
    relu.set_local_size_xyz(32, 1, 1);
    CHECK(relu.create(ncnn::LayerShaderType::relu, opt, spec) == 0);
    CHECK(relu.pipeline() != VK_NULL_HANDLE && relu.local_size_x() == 32);
    CHECK(relu.shader_info().binding_count == 1 && relu.shader_info().push_constant_count == 1);
    if (compile_only) {
        std::puts("PASS: four ncnn packed pipelines compiled at local32; no probe dispatches");
        return 0;
    }
    std::puts("Precision gate: exact representable normal values; other conversions must be adjacent binary16 values. Subnormal conversion may flush; unpack policy must be consistent. No NaN/Inf accepted.");
    CHECK(check_seeded_unpack(device, opt, unpack) == 0);
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        for (unsigned stride : {4u, 5u})
            CHECK(check_partial_words(device, opt, scatter, stride, repeat) == 0);
        CHECK(run_case(device, opt, pack, scatter, unpack, relu, 33, false, repeat) == 0);
        for (unsigned width : {3u, 5u, 33u, 129u})
            CHECK(run_case(device, opt, pack, scatter, unpack, relu, width, true, repeat) == 0);
    }
    std::printf("PASS: packed conversion/ReLU, all outputs and guards; observed unpack subnormals=%s (%zu checks)\n",
                unpack_subnormals < 0 ? "not observable after conversion" : unpack_subnormals ? "preserved" : "flushed", subnormal_checks);
    return 0;
}
