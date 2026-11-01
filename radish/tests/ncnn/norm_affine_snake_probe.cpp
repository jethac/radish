// SPDX-License-Identifier: MIT
#include "radish_affine_snake.h"
#include "radish_norm_affine_snake.h"
#include "runtime_helpers.h"
#if NCNN_VULKAN
#include "allocator.h"
#include "layer/vulkan/instancenorm_vulkan.h"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {
const int widths[2] = {33, 35};
const int channels[2] = {128, 256};
const char* input_names[5] = {"raw", "gamma", "beta", "alpha", "reciprocal"};
const double atol = 2e-5, rtol = 2e-5;

std::string parameters(int width, int count)
{
    const std::string s2 = "2," + std::to_string(width) + ',' + std::to_string(count) + ",1,1";
    const std::string sc = "2,1," + std::to_string(count) + ",1,1";
    const std::string s3 = "3," + std::to_string(width) + ",1,1," + std::to_string(count);
    std::ostringstream out;
    out << "7767517\n17 29\n";
    for (int i = 0; i < 5; ++i)
        out << "Input " << input_names[i] << " 0 1 " << input_names[i] << " -23330=5," << (i ? sc : s2) << '\n';
    out << "Split raw_split 1 5 raw raw_original raw_fused residual_original residual_fused raw_retained -23330=25,"
        << s2 << ',' << s2 << ',' << s2 << ',' << s2 << ',' << s2 << '\n';
    for (int i = 1; i < 5; ++i)
        out << "Split " << input_names[i] << "_split 1 3 " << input_names[i] << ' '
            << input_names[i] << "_original " << input_names[i] << "_fused " << input_names[i]
            << "_retained -23330=15," << sc << ',' << sc << ',' << sc << '\n';
    out << "Reshape radish_norm_4_to3d 1 1 raw_original rank3 0=-1 1=1 2=" << count << " -23330=5," << s3 << '\n'
        << "InstanceNorm radish_norm_4_instance 1 1 rank3 normalized 0=" << count << " 1=1.000000e-5 2=0 -23330=5," << s3 << '\n'
        << "Reshape radish_norm_4_to2d 1 1 normalized norm2 0=-1 1=" << count << " -23330=5," << s2 << '\n'
        << "RadishAffineSnake original 5 1 norm2 gamma_original beta_original alpha_original reciprocal_original original -23330=5," << s2 << '\n'
        << "RadishNormAffineSnake radish_norm_5_affine_snake 5 1 raw_fused gamma_fused beta_fused alpha_fused reciprocal_fused fused -23330=5," << s2 << '\n'
        << "BinaryOp original_add 2 1 original residual_original original_sum 0=0 -23330=5," << s2 << '\n'
        << "BinaryOp fused_add 2 1 fused residual_fused fused_sum 0=0 -23330=5," << s2 << '\n';
    return out.str();
}

std::vector<ncnn::Mat> inputs(int width, int count, int pack)
{
    const float gamma[] = {-1.f, -.75f, -.5f, -.25f, 0.f, .25f, .5f, .75f};
    const float beta[] = {-.5f, -.25f, 0.f, .25f, .5f, .125f, -.125f, .75f};
    const float alpha[] = {.5f, .75f, 1.f, 1.25f, 1.5f, 2.f, 2.5f, 3.f};
    const float reciprocal[] = {1.75f, 1.25f, .875f, .625f, .5f, .375f, .25f, .125f};
    std::vector<ncnn::Mat> result(5);
    for (int i = 0; i < 5; ++i)
        result[i].create(i ? 1 : width, count / pack, static_cast<size_t>(4 * pack), pack);
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
                = c % 16 == 0 ? .125f : ((x * 13 + c * 7) % 127 - 63) / 64.f + c / 256.f;
    }
    return result;
}

std::vector<float> scalars(const ncnn::Mat& m)
{
    if (m.empty() || m.dims != 2 || m.n != 1 || m.d != 1 || m.c != 1
        || (m.elempack != 1 && m.elempack != 4) || m.elemsize != static_cast<size_t>(4 * m.elempack))
        return std::vector<float>();
    std::vector<float> result(static_cast<size_t>(m.w) * m.h * m.elempack);
    const float* data = static_cast<const float*>(m.data);
    for (int c = 0; c < m.h * m.elempack; ++c)
        for (int x = 0; x < m.w; ++x)
            result[static_cast<size_t>(c) * m.w + x] = data[((c / m.elempack) * m.w + x) * m.elempack + c % m.elempack];
    return result;
}

bool same(const std::vector<float>& a, const std::vector<float>& b)
{
    return !a.empty() && a.size() == b.size() && !std::memcmp(a.data(), b.data(), a.size() * sizeof(float));
}

int compare(const std::vector<std::vector<float> >& saved, const std::vector<ncnn::Mat>& source, const std::vector<ncnn::Mat>& outputs,
            const std::vector<ncnn::Mat>& retained, int width, int count, const char* backend)
{
    CHECK(saved.size() == 5 && outputs.size() == 4 && retained.size() == 5);
    std::vector<std::vector<float> > s(5), y(4);
    for (int i = 0; i < 5; ++i)
    {
        s[i] = saved[i];
        CHECK(same(s[i], scalars(source[i])) && same(s[i], scalars(retained[i])));
        CHECK(s[i].size() == static_cast<size_t>(i ? count : width * count));
    }
    for (int i = 0; i < 4; ++i)
    {
        y[i] = scalars(outputs[i]);
        CHECK(y[i].size() == s[0].size());
    }
    CHECK(same(y[0], y[1]) && same(y[2], y[3]));
    double max_error = 0;
    size_t changed = 0;
    const double epsilon = static_cast<double>(1.e-5f);
    for (int c = 0; c < count; ++c)
    {
        double mean = 0, variance = 0;
        for (int x = 0; x < width; ++x) mean += s[0][static_cast<size_t>(c) * width + x];
        mean /= width;
        for (int x = 0; x < width; ++x)
        {
            const double delta = s[0][static_cast<size_t>(c) * width + x] - mean;
            variance += delta * delta;
        }
        variance /= width;
        for (int x = 0; x < width; ++x)
        {
            const size_t i = static_cast<size_t>(c) * width + x;
            const double normalized = (s[0][i] - mean) / std::sqrt(variance + epsilon);
            const double styled = (static_cast<double>(s[1][c]) + 1.) * normalized + s[2][c];
            const double sine = std::sin(static_cast<double>(s[3][c]) * styled);
            const double expected = styled + s[4][c] * sine * sine;
            for (int j = 0; j < 4; ++j)
            {
                const double ref = expected + (j >= 2 ? s[0][i] : 0.);
                const double error = std::fabs(y[j][i] - ref);
                max_error = std::fmax(max_error, error);
                CHECK(std::isfinite(ref) && std::isfinite(y[j][i]) && error <= atol + rtol * std::fabs(ref));
            }
            if (y[1][i] != s[0][i]) ++changed;
        }
    }
    CHECK(changed > s[0].size() / 2);
    std::printf("PASS: %s C%d W%d, all %zu final + %zu residual-sum pairs bit-identical, five retained inputs unchanged, double atol=%g rtol=%g max abs=%.9g\n",
        backend, count, width, y[0].size(), y[2].size(), atol, rtol, max_error);
    return 0;
}

#if NCNN_VULKAN
uint64_t groups(const ncnn::Pipeline* p, int w, int h, int c)
{
    if (!p || !p->pipeline() || !p->local_size_x() || !p->local_size_y() || !p->local_size_z()) return 0;
    return ((static_cast<uint64_t>(w) + p->local_size_x() - 1) / p->local_size_x())
        * ((static_cast<uint64_t>(h) + p->local_size_y() - 1) / p->local_size_y())
        * ((static_cast<uint64_t>(c) + p->local_size_z() - 1) / p->local_size_z());
}

int expected_groups(const ncnn::InstanceNorm_vulkan* norm, int width, int count, uint64_t& stats, uint64_t& final)
{
    CHECK(norm && norm->radish_norm_outplace && !norm->support_inplace);
    CHECK(norm->pipeline_instancenorm_radish_sum4_pack4 && norm->pipeline_instancenorm_radish_outplace_pack4);
    CHECK(!norm->pipeline_instancenorm_reduce_subgroup_pack4);
    const int packs = count / 4;
    stats = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        int width4 = (width + 3) / 4;
        const ncnn::Pipeline* first = pass ? norm->pipeline_instancenorm_radish_sum4_pack4 : norm->pipeline_instancenorm_reduce_sum4_fp16_to_fp32_pack4;
        uint64_t g = groups(first, width4, 1, packs);
        CHECK(g); stats += g;
        int pb = pass;
        while (width4 > 4)
        {
            width4 = (width4 + 3) / 4;
            g = groups(norm->pipeline_instancenorm_reduce_sum4_fp32_pack4[pb++ % 2], width4, 1, packs);
            CHECK(g); stats += g;
        }
        g = groups(norm->pipeline_instancenorm_reduce_mean_pack4, packs, 1, 1);
        CHECK(g); stats += g;
    }
    const uint64_t coeff = groups(norm->pipeline_instancenorm_coeffs_pack4, packs, 1, 1);
    CHECK(coeff); stats += coeff;
    final = groups(norm->pipeline_instancenorm_radish_outplace_pack4, width, 1, packs);
    CHECK(final);
    std::printf("original stats groups=%llu final groups=%llu actual final local=%ux%ux%u\n",
        static_cast<unsigned long long>(stats), static_cast<unsigned long long>(final),
        norm->pipeline_instancenorm_radish_outplace_pack4->local_size_x(),
        norm->pipeline_instancenorm_radish_outplace_pack4->local_size_y(),
        norm->pipeline_instancenorm_radish_outplace_pack4->local_size_z());
    return 0;
}

bool disjoint(const ncnn::VkMat& a, const ncnn::VkMat& b)
{
    if (a.empty() || b.empty()) return false;
    if (a.buffer() != b.buffer()) return true;
    const size_t ao = a.buffer_offset(), bo = b.buffer_offset();
    return ao <= bo ? a.buffer_capacity() <= bo - ao : b.buffer_capacity() <= ao - bo;
}

int gpu_case(ncnn::Net& net, const ncnn::VulkanDevice* device, int width, int count, uint64_t stats, uint64_t final)
{
    const std::vector<ncnn::Mat> source = inputs(width, count, 4);
    CHECK(source.size() == 5);
    std::vector<std::vector<float> > saved;
    for (const ncnn::Mat& m : source) saved.push_back(scalars(m));
    ncnn::Extractor ex = net.create_extractor();
    ncnn::VkCompute command(device);
    std::vector<ncnn::VkMat> uploaded(5), kept(5), output(4);
    for (int i = 0; i < 5; ++i)
    {
        command.record_upload(source[i], uploaded[i], net.opt);
        CHECK(!uploaded[i].empty() && uploaded[i].elempack == 4 && uploaded[i].elemsize == 16u);
        CHECK(ex.input(input_names[i], uploaded[i]) == 0);
    }
    CHECK(command.submit_and_wait() == 0 && command.reset() == 0);
    const uint64_t before_keep = command.pending_dispatch_total();
    for (int i = 0; i < 5; ++i)
    {
        CHECK(ex.extract((std::string(input_names[i]) + "_retained").c_str(), kept[i], command) == 0);
        CHECK(kept[i].data == uploaded[i].data && kept[i].refcount == uploaded[i].refcount
              && kept[i].buffer_offset() == uploaded[i].buffer_offset());
    }
    CHECK(command.pending_dispatch_total() == before_keep);
    const uint64_t affine = static_cast<uint64_t>((width + 31) / 32) * (count / 4);
    const uint64_t before_original = command.pending_dispatch_total();
    CHECK(ex.extract("original", output[0], command) == 0);
    CHECK(command.pending_dispatch_total() - before_original == stats + final + affine);
    const uint64_t before_fused = command.pending_dispatch_total();
    CHECK(ex.extract("fused", output[1], command) == 0);
    CHECK(command.pending_dispatch_total() - before_fused == stats + affine);
    CHECK(!output[1].empty() && output[1].dims == 2 && output[1].w == width
          && output[1].h == count / 4 && output[1].elempack == 4 && output[1].elemsize == 16u);
    for (int i = 0; i < 5; ++i) CHECK(disjoint(output[1], kept[i]));
    CHECK(disjoint(output[0], output[1]));
    CHECK(ex.extract("original_sum", output[2], command) == 0);
    CHECK(ex.extract("fused_sum", output[3], command) == 0);
    ncnn::Option download = net.opt;
    download.use_packing_layout = false;
    std::vector<ncnn::Mat> result(4), retained(5);
    for (int i = 0; i < 4; ++i) command.record_download(output[i], result[i], download);
    for (int i = 0; i < 5; ++i) command.record_download(kept[i], retained[i], download);
    CHECK(command.submit_and_wait() == 0);
    std::printf("group proof: original=%llu composite=%llu removed-normalization-final=%llu; retained forks add zero groups\n",
        static_cast<unsigned long long>(stats + final + affine), static_cast<unsigned long long>(stats + affine), static_cast<unsigned long long>(final));
    return compare(saved, source, result, retained, width, count, "Vulkan pack4");
}
#endif

int cpu_case(ncnn::Net& net, int width, int count, int pack)
{
    const std::vector<ncnn::Mat> source = inputs(width, count, pack);
    CHECK(source.size() == 5);
    std::vector<std::vector<float> > saved;
    for (const ncnn::Mat& m : source) saved.push_back(scalars(m));
    ncnn::Extractor ex = net.create_extractor();
    for (int i = 0; i < 5; ++i) CHECK(ex.input(input_names[i], source[i]) == 0);
    std::vector<ncnn::Mat> result(4), retained(5);
    for (int i = 0; i < 5; ++i)
        CHECK(ex.extract((std::string(input_names[i]) + "_retained").c_str(), retained[i], 1) == 0);
    const char* targets[4] = {"original", "fused", "original_sum", "fused_sum"};
    for (int i = 0; i < 4; ++i) CHECK(ex.extract(targets[i], result[i], 1) == 0);
    CHECK(result[1].elempack == pack && result[1].data != source[0].data);
    for (int i = 0; i < 5; ++i) CHECK(same(scalars(source[i]), scalars(retained[i])));
    return compare(saved, source, result, retained, width, count, pack == 1 ? "CPU pack1" : "CPU pack4");
}

int run_case(bool cpu, bool compile_only, int width, int count, int pack
#if NCNN_VULKAN
             , const ncnn::VulkanDevice* device
#endif
             )
{
    ncnn::Option opt;
    radish_ncnn::configure_options(opt, !cpu, pack == 4, false, 1);
#if NCNN_VULKAN
    std::unique_ptr<radish_ncnn::Allocators> allocators;
    if (!cpu)
    {
        allocators.reset(new radish_ncnn::Allocators(device, opt));
        opt = allocators->opt;
        CHECK(opt.blob_vkallocator && opt.workspace_vkallocator && opt.staging_vkallocator);
    }
#endif
    radish_ncnn::AffineSnakeContext affine_context;
    radish_ncnn::NormAffineSnakeContext norm_context;
    ncnn::Net net;
    net.opt = opt;
#if NCNN_VULKAN
    if (!cpu) net.set_vulkan_device(device);
#endif
    CHECK(affine_context.register_layer(net) == 0 && norm_context.register_layer(net) == 0);
    CHECK(net.load_param_mem(parameters(width, count).c_str()) == 0);
    CHECK(net.layers().size() == 17 && net.blobs().size() == 29);
    ncnn::Mat no_weights;
    for (ncnn::Layer* layer : net.mutable_layers())
    {
        CHECK(layer && (cpu || layer->type == "Input" || layer->support_vulkan));
        CHECK(layer->load_model(ncnn::ModelBinFromMatArray(&no_weights)) == 0);
        CHECK(layer->create_pipeline(net.opt) == 0);
        if (layer->type == "RadishNormAffineSnake") CHECK(!layer->support_inplace);
    }
    CHECK(affine_context.shader_compile_attempts() == (cpu ? 0u : 1u));
    CHECK(norm_context.shader_compile_attempts() == (cpu ? 0u : 1u));
    CHECK(cpu ? norm_context.compiled_spirv_words() == 0 : norm_context.compiled_spirv_words() > 0);
    std::printf("CASE C%d W%d %s pack%d; original and composite pipelines ready\n", count, width, cpu ? "CPU" : "Vulkan", pack);
#if NCNN_VULKAN
    uint64_t stats = 0, final = 0;
    if (!cpu)
    {
        CHECK(net.layers()[11]->type == "InstanceNorm");
        CHECK(expected_groups(static_cast<const ncnn::InstanceNorm_vulkan*>(net.layers()[11]), width, count, stats, final) == 0);
        std::printf("composite validated create_pipeline: seven SSBOs, eight push bytes, local32x1x1; shader attempts=%u SPIR-V words=%zu; inspect actual IR3 before execution\n",
            norm_context.shader_compile_attempts(), norm_context.compiled_spirv_words());
    }
#endif
    if (compile_only)
    {
        std::puts("PASS: pipeline-only case; no probe tensor upload or dispatch (device initialization may submit)");
        return 0;
    }
    if (cpu) return cpu_case(net, width, count, pack);
#if NCNN_VULKAN
    return gpu_case(net, device, width, count, stats, final);
#else
    return 1;
#endif
}

int run(bool cpu, bool compile_only)
{
#if NCNN_VULKAN
    radish_ncnn::GpuInstance instance;
    const ncnn::VulkanDevice* device = 0;
    if (!cpu)
    {
        for (const char* flag : {"RADISH_NCNN_NORM_SUM4", "RADISH_NCNN_NORM_OUTPLACE", "RADISH_NCNN_RESHAPE_ALIAS"})
        {
            const char* value = std::getenv(flag);
            if (!value || std::strcmp(value, "1")) { std::fprintf(stderr, "Vulkan comparison requires %s=1\n", flag); return 1; }
        }
        CHECK(ncnn::create_gpu_instance() == 0); instance.active = true;
        CHECK(ncnn::get_gpu_count() == 1);
        device = ncnn::get_gpu_device(0);
        CHECK(device && device->is_valid() && device->info.vendor_id() == 0x5143 && device->info.device_id() == 506);
        CHECK(!std::strcmp(device->info.device_name(), "Radish Adreno 506 (experimental compute)"));
    }
#else
    CHECK(cpu);
#endif
    for (int i = 0; i < 2; ++i)
        for (int pack = cpu ? 1 : 4; pack <= 4; pack *= 4)
            CHECK(run_case(cpu, compile_only, widths[i], channels[i], pack
#if NCNN_VULKAN
                           , device
#endif
                           ) == 0);
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, 0, _IONBF, 0);
    bool cpu = false, compile_only = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--cpu") && !cpu) cpu = true;
        else if (!std::strcmp(argv[i], "--compile-only") && !compile_only) compile_only = true;
        else { std::fprintf(stderr, "Usage: %s [--cpu] [--compile-only]\n", argv[0]); return 1; }
    }
    return run(cpu, compile_only);
}
