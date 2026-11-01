// SPDX-License-Identifier: MIT
#include "radish_affine_snake.h"
#include "runtime_helpers.h"
#include "layer.h"
#if NCNN_VULKAN
#include "layer/vulkan/binaryop_vulkan.h"
#include "layer/vulkan/unaryop_vulkan.h"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {
const double absolute_tolerance = 2e-5;
const double relative_tolerance = 2e-5;
const int widths[2] = {33, 35};
const int channels[2] = {128, 256};

ncnn::Mat hint(int width, int count, int pack)
{
    return ncnn::Mat(width, count / pack, static_cast<void*>(0), 4u * pack, pack);
}

std::string parameters()
{
    std::ostringstream out;
    out << "7767517\n12 12\n";
    for (int i = 0; i < 2; ++i)
    {
        const char* names[5] = {"x", "gamma", "beta", "alpha", "reciprocal"};
        for (int j = 0; j < 5; ++j)
            out << "Input " << names[j] << i << " 0 1 " << names[j] << i
                << " -23330=5,2," << (j == 0 ? widths[i] : 1) << ',' << channels[i] << ",1,1\n";
        out << "RadishAffineSnake snake" << i << " 5 1 x" << i << " gamma" << i << " beta" << i << " alpha" << i << " reciprocal" << i
            << " y" << i << " -23330=5,2," << widths[i] << ',' << channels[i] << ",1,1\n";
    }
    return out.str();
}

std::vector<ncnn::Mat> inputs(int width, int count, int pack)
{
    const float values[] = {0.f, -0.f, 1e-38f, -1e-38f, 1e-20f, -1e-20f,
                            1e-7f, -1e-7f, .125f, -.125f, 1.f, -1.f, 3.f, -3.f, .75f, -.75f};
    const float gamma[] = {-1.f, -.75f, -.5f, -.25f, 0.f, .25f, .5f, .75f};
    const float beta[] = {-.5f, -.25f, 0.f, .25f, .5f, .125f, -.125f, .75f};
    const float alpha[] = {.5f, .75f, 1.f, 1.25f, 1.5f, 2.f, 2.5f, 3.f};
    // Independent stored coefficients deliberately differ from 1/alpha.
    const float reciprocal[] = {1.75f, 1.25f, .875f, .625f, .5f, .375f, .25f, .125f};
    std::vector<ncnn::Mat> result(5);
    for (int i = 0; i < 5; ++i)
        result[i].create(i == 0 ? width : 1, count / pack, static_cast<size_t>(4) * pack, pack);
    for (const ncnn::Mat& m : result)
        if (m.empty()) return std::vector<ncnn::Mat>();
    for (int c = 0; c < count; ++c)
    {
        static_cast<float*>(result[1].data)[c] = gamma[c % 8];
        static_cast<float*>(result[2].data)[c] = beta[c % 8];
        static_cast<float*>(result[3].data)[c] = alpha[c % 8];
        static_cast<float*>(result[4].data)[c] = reciprocal[c % 8];
        for (int x = 0; x < width; ++x)
            static_cast<float*>(result[0].data)[((c / pack) * width + x) * pack + c % pack]
                = values[(x + c * 7) % 16];
    }
    return result;
}

std::vector<float> scalars(const ncnn::Mat& m)
{
    if (m.empty() || m.dims != 2 || m.n != 1 || m.d != 1 || m.c != 1
        || (m.elempack != 1 && m.elempack != 4) || m.elemsize != 4u * m.elempack)
        return std::vector<float>();
    std::vector<float> result(static_cast<size_t>(m.w) * m.h * m.elempack);
    const float* ptr = static_cast<const float*>(m.data);
    for (int c = 0; c < m.h * m.elempack; ++c)
        for (int x = 0; x < m.w; ++x)
            result[static_cast<size_t>(c) * m.w + x]
                = ptr[((c / m.elempack) * m.w + x) * m.elempack + c % m.elempack];
    return result;
}

bool same(const std::vector<float>& a, const std::vector<float>& b)
{
    return !a.empty() && a.size() == b.size() && !std::memcmp(a.data(), b.data(), a.size() * sizeof(float));
}

int compare(const std::vector<ncnn::Mat>& source, const ncnn::Mat& original,
            const ncnn::Mat& fused, int width, int count, const char* backend)
{
    const std::vector<float> a = scalars(original), b = scalars(fused);
    const std::vector<float> x = scalars(source[0]), gamma = scalars(source[1]), beta = scalars(source[2]);
    const std::vector<float> alpha = scalars(source[3]), reciprocal = scalars(source[4]);
    CHECK(a.size() == static_cast<size_t>(width) * count && b.size() == a.size());
    CHECK(x.size() == a.size() && gamma.size() == static_cast<size_t>(count) && beta.size() == gamma.size());
    CHECK(alpha.size() == gamma.size() && reciprocal.size() == gamma.size());
    size_t bit_mismatches = 0, numeric_mismatches = 0;
    double max_original = 0, max_fused = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const size_t channel = i / width;
        const double styled = (static_cast<double>(gamma[channel]) + 1.0) * x[i] + beta[channel];
        const double sine = std::sin(static_cast<double>(alpha[channel]) * styled);
        const double expected = styled + reciprocal[channel] * sine * sine;
        const double ea = std::fabs(static_cast<double>(a[i]) - expected);
        const double eb = std::fabs(static_cast<double>(b[i]) - expected);
        max_original = std::fmax(max_original, ea);
        max_fused = std::fmax(max_fused, eb);
        uint32_t abits, bbits;
        std::memcpy(&abits, &a[i], sizeof(abits));
        std::memcpy(&bbits, &b[i], sizeof(bbits));
        const bool bits_ok = abits == bbits && std::isfinite(a[i]) && std::isfinite(b[i]);
        const double limit = absolute_tolerance + relative_tolerance * std::fabs(expected);
        const bool numeric_ok = std::isfinite(expected) && std::isfinite(a[i]) && std::isfinite(b[i]) && ea <= limit && eb <= limit;
        if (!bits_ok && bit_mismatches++ < 8)
            std::fprintf(stderr, "bit mismatch[%zu] original=%08x fused=%08x\n", i, abits, bbits);
        if (!numeric_ok && numeric_mismatches++ < 8)
            std::fprintf(stderr, "numeric mismatch[%zu] original=%.9g fused=%.9g expected=%.17g limit=%.9g\n", i, a[i], b[i], expected, limit);
    }
    std::printf("%s AffineSnake C%d W%d: all %zu outputs, bit mismatches=%zu numeric mismatches=%zu, max abs original=%.9g fused=%.9g; atol=%g rtol=%g\n",
                backend, count, width, a.size(), bit_mismatches, numeric_mismatches, max_original, max_fused, absolute_tolerance, relative_tolerance);
    CHECK(bit_mismatches == 0 && numeric_mismatches == 0);
    return 0;
}

#if NCNN_VULKAN
int pipeline(const ncnn::Pipeline* p, const char* name)
{
    if (!p) return 0;
    CHECK(p->pipeline() != VK_NULL_HANDLE);
    CHECK(p->local_size_x() && p->local_size_y() && p->local_size_z());
    CHECK(static_cast<uint64_t>(p->local_size_x()) * p->local_size_y() * p->local_size_z() <= 128);
    std::printf("  original %s local=%ux%ux%u bindings=%d push-bytes=%d\n", name,
                p->local_size_x(), p->local_size_y(), p->local_size_z(),
                p->shader_info().binding_count, p->shader_info().push_constant_count * 4);
    return 0;
}

int inspect(ncnn::Layer* layer, bool unary)
{
    if (unary)
    {
        const auto* u = static_cast<const ncnn::UnaryOp_vulkan*>(layer);
        CHECK(u->pipeline_unaryop);
        CHECK(pipeline(u->pipeline_unaryop, "UnaryOp") == 0);
        return 0;
    }
    const auto* b = static_cast<const ncnn::BinaryOp_vulkan*>(layer);
    bool any = b->pipeline_binaryop || b->pipeline_binaryop_pack4;
    CHECK(pipeline(b->pipeline_binaryop, "BinaryOp") == 0);
    CHECK(pipeline(b->pipeline_binaryop_pack4, "BinaryOp pack4") == 0);
    for (int i = 0; i < 2; ++i)
    {
        any = any || b->pipeline_binaryop_broadcast[i] || b->pipeline_binaryop_broadcast_pack4[i]
            || b->pipeline_binaryop_broadcast_pack1to4[i];
        CHECK(pipeline(b->pipeline_binaryop_broadcast[i], "BinaryOp broadcast") == 0);
        CHECK(pipeline(b->pipeline_binaryop_broadcast_pack4[i], "BinaryOp broadcast pack4") == 0);
        CHECK(pipeline(b->pipeline_binaryop_broadcast_pack1to4[i], "BinaryOp broadcast pack1to4") == 0);
    }
    CHECK(any);
    return 0;
}
#endif

class Original
{
public:
    ncnn::Layer* stages[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    ncnn::Option opt;
    ~Original()
    {
        for (ncnn::Layer* layer : stages)
            if (layer) { layer->destroy_pipeline(opt); delete layer; }
    }
    int create(const ncnn::Option& options, int width, int count
#if NCNN_VULKAN
               , const ncnn::VulkanDevice* device
#endif
               )
    {
        opt = options;
        const char* types[8] = {"BinaryOp", "BinaryOp", "BinaryOp", "BinaryOp", "UnaryOp", "UnaryOp", "BinaryOp", "BinaryOp"};
        const int operations[8] = {0, 2, 0, 2, 9, 4, 2, 0};
        const int pack = opt.use_vulkan_compute ? 4 : 1;
        for (int i = 0; i < 8; ++i)
        {
#if NCNN_VULKAN
            if (opt.use_vulkan_compute)
            {
                stages[i] = ncnn::create_layer_vulkan(types[i]);
                if (stages[i]) stages[i]->vkdev = device;
            }
            else
#endif
                stages[i] = ncnn::create_layer_cpu(types[i]);
            CHECK(stages[i]);
            ncnn::ParamDict params;
            params.set(0, operations[i]);
            if (i == 0) { params.set(1, 1); params.set(2, 1.f); }
            CHECK(stages[i]->load_param(params) == 0);
            stages[i]->top_shapes.assign(1, hint(i == 0 ? 1 : width, count, pack));
            if (i == 0 || i == 4 || i == 5)
                stages[i]->bottom_shapes = stages[i]->top_shapes;
            else
            {
                stages[i]->bottom_shapes.push_back(hint(i == 2 || i == 7 ? width : 1, count, pack));
                stages[i]->bottom_shapes.push_back(hint(i == 2 ? 1 : width, count, pack));
            }
            CHECK(stages[i]->create_pipeline(opt) == 0);
#if NCNN_VULKAN
            if (opt.use_vulkan_compute) CHECK(inspect(stages[i], i == 4 || i == 5) == 0);
#endif
        }
        return 0;
    }
};

int cpu_case(ncnn::Layer* fused, Original& original, int width, int count, int pack)
{
    const std::vector<ncnn::Mat> source = inputs(width, count, pack);
    CHECK(source.size() == 5);
    std::vector<std::vector<float> > saved;
    for (const ncnn::Mat& m : source) saved.push_back(scalars(m));
    ncnn::Mat gamma1;
    CHECK(original.stages[0]->forward(source[1], gamma1, original.opt) == 0);
    std::vector<ncnn::Mat> pair(2), scale(1), styled(1), result(1), scaled(1), expected(1), actual(1);
    pair[0] = gamma1; pair[1] = source[0];
    CHECK(original.stages[1]->forward(pair, scale, original.opt) == 0);
    pair[0] = scale[0]; pair[1] = source[2];
    CHECK(original.stages[2]->forward(pair, styled, original.opt) == 0);
    pair[0] = source[3]; pair[1] = styled[0];
    CHECK(original.stages[3]->forward(pair, result, original.opt) == 0);
    CHECK(original.stages[4]->forward_inplace(result[0], original.opt) == 0);
    CHECK(original.stages[5]->forward_inplace(result[0], original.opt) == 0);
    pair[0] = source[4]; pair[1] = result[0];
    CHECK(original.stages[6]->forward(pair, scaled, original.opt) == 0);
    pair[0] = styled[0]; pair[1] = scaled[0];
    CHECK(original.stages[7]->forward(pair, expected, original.opt) == 0);
    CHECK(fused->forward(source, actual, original.opt) == 0);
    CHECK(compare(source, expected[0], actual[0], width, count, pack == 1 ? "CPU pack1" : "CPU pack4") == 0);
    for (size_t i = 0; i < source.size(); ++i) CHECK(same(saved[i], scalars(source[i])));
    std::puts("PASS: CPU delegated eight-operation identity, independent reference and retained inputs");
    return 0;
}

#if NCNN_VULKAN
int gpu_case(ncnn::Layer* fused, Original& original, const ncnn::VulkanDevice* device, int width, int count)
{
    radish_ncnn::Allocators allocators(device, original.opt);
    CHECK(allocators.opt.blob_vkallocator && allocators.opt.staging_vkallocator);
    const std::vector<ncnn::Mat> source = inputs(width, count, 4);
    CHECK(source.size() == 5);
    ncnn::VkCompute command(device);
    std::vector<ncnn::VkMat> gpu(5), pair(2), scale(1), styled(1), result(1), scaled(1), expected(1), actual(1);
    for (size_t i = 0; i < gpu.size(); ++i)
    {
        command.record_upload(source[i], gpu[i], allocators.opt);
        CHECK(!gpu[i].empty() && gpu[i].elempack == 4 && gpu[i].elemsize == 16u);
    }
    CHECK(command.submit_and_wait() == 0);
    CHECK(command.reset() == 0);
    ncnn::VkMat gamma1;
    CHECK(original.stages[0]->forward(gpu[1], gamma1, command, allocators.opt) == 0);
    pair[0] = gamma1; pair[1] = gpu[0];
    CHECK(original.stages[1]->forward(pair, scale, command, allocators.opt) == 0);
    pair[0] = scale[0]; pair[1] = gpu[2];
    CHECK(original.stages[2]->forward(pair, styled, command, allocators.opt) == 0);
    pair[0] = gpu[3]; pair[1] = styled[0];
    CHECK(original.stages[3]->forward(pair, result, command, allocators.opt) == 0);
    CHECK(original.stages[4]->forward_inplace(result[0], command, allocators.opt) == 0);
    CHECK(original.stages[5]->forward_inplace(result[0], command, allocators.opt) == 0);
    pair[0] = gpu[4]; pair[1] = result[0];
    CHECK(original.stages[6]->forward(pair, scaled, command, allocators.opt) == 0);
    pair[0] = styled[0]; pair[1] = scaled[0];
    CHECK(original.stages[7]->forward(pair, expected, command, allocators.opt) == 0);
    const uint64_t before_fused = command.pending_dispatch_total();
    CHECK(fused->forward(gpu, actual, command, allocators.opt) == 0);
    const uint64_t expected_groups = static_cast<uint64_t>((width + 31) / 32) * (count / 4);
    CHECK(command.pending_dispatch_total() - before_fused == expected_groups);
    std::printf("fused dispatch groups=%llu (local32x1x1, grid=%dx%d)\n",
                static_cast<unsigned long long>(expected_groups), (width + 31) / 32, count / 4);
    ncnn::Option download = allocators.opt;
    download.use_packing_layout = false;
    ncnn::Mat expected_cpu, actual_cpu;
    std::vector<ncnn::Mat> retained(5);
    command.record_download(expected[0], expected_cpu, download);
    command.record_download(actual[0], actual_cpu, download);
    for (size_t i = 0; i < gpu.size(); ++i) command.record_download(gpu[i], retained[i], download);
    CHECK(command.submit_and_wait() == 0);
    CHECK(compare(source, expected_cpu, actual_cpu, width, count, "Vulkan pack4") == 0);
    for (size_t i = 0; i < source.size(); ++i) CHECK(same(scalars(source[i]), scalars(retained[i])));
    std::puts("PASS: fused/eight-operation bit identity, independent reference and retained inputs");
    return 0;
}
#endif

int run(bool cpu, bool compile_only)
{
#if NCNN_VULKAN
    radish_ncnn::GpuInstance instance;
    const ncnn::VulkanDevice* device = 0;
    if (!cpu)
    {
        CHECK(ncnn::create_gpu_instance() == 0); instance.active = true;
        CHECK(ncnn::get_gpu_count() == 1);
        device = ncnn::get_gpu_device(0);
        CHECK(device && device->is_valid());
        std::printf("ncnn device: %s\n", device->info.device_name());
    }
#else
    CHECK(cpu);
#endif
    radish_ncnn::AffineSnakeContext context;
    ncnn::Net net;
    radish_ncnn::configure_options(net.opt, !cpu, true, false, 1);
#if NCNN_VULKAN
    if (!cpu) net.set_vulkan_device(device);
#endif
    CHECK(context.register_layer(net) == 0);
    const std::string param = parameters();
    CHECK(net.load_param_mem(param.c_str()) == 0);
    CHECK(net.layers().size() == 12 && net.blobs().size() == 12);
    ncnn::Layer* fused[2] = {net.mutable_layers()[5], net.mutable_layers()[11]};
    Original original[2];
    for (int i = 0; i < 2; ++i)
    {
        std::printf("compile AffineSnake case C%d W%d\n", channels[i], widths[i]);
        CHECK(fused[i] && fused[i]->type == "RadishAffineSnake");
        CHECK(fused[i]->create_pipeline(net.opt) == 0);
        CHECK(original[i].create(net.opt, widths[i], channels[i]
#if NCNN_VULKAN
                                 , device
#endif
                                 ) == 0);
    }
    CHECK(context.shader_compile_attempts() == (cpu ? 0u : 1u));
    CHECK(cpu ? context.compiled_spirv_words() == 0 : context.compiled_spirv_words() > 0);
    if (cpu)
        std::puts("CPU delegated pipelines ready; no Vulkan shader compilation");
    else
        std::printf("AffineSnake shader compile attempts=%u SPIR-V words=%zu; GPU pipeline gate: local32x1x1, shared=0, six SSBOs, eight push bytes, four FMul/three FAdd all NoContraction; inspect IR3 register/instruction output in loader logs\n",
                    context.shader_compile_attempts(), context.compiled_spirv_words());
    // Never call Net::load_model here: it may submit an empty GPU upload.
    // Both cases' fused and original pipelines exist before this early return.
    if (compile_only)
    {
        std::puts("PASS: both AffineSnake cases compiled; no model/input upload or command submission");
        return 0;
    }
    for (int i = 0; i < 2; ++i)
    {
        if (cpu)
        {
            CHECK(cpu_case(fused[i], original[i], widths[i], channels[i], 1) == 0);
            CHECK(cpu_case(fused[i], original[i], widths[i], channels[i], 4) == 0);
        }
#if NCNN_VULKAN
        else CHECK(gpu_case(fused[i], original[i], device, widths[i], channels[i]) == 0);
#endif
    }
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, 0, _IONBF, 0);
    bool cpu = false, compile_only = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--cpu")) cpu = true;
        else if (!std::strcmp(argv[i], "--compile-only")) compile_only = true;
        else
        {
            std::fprintf(stderr, "Usage: %s [--cpu] [--compile-only]\n", argv[0]);
            return 1;
        }
    }
    return run(cpu, compile_only);
}
