#include "command.h"
#include "gpu.h"
#include "layer.h"
#include "modelbin.h"
#include "pipeline.h"
#include "layer/vulkan/convolution1d_vulkan.h"
#include "layer/vulkan/flatten_vulkan.h"
#include "layer/vulkan/innerproduct_vulkan.h"
#include "layer/vulkan/padding_vulkan.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {

// Fixed before running either backend: bounded FP32 inputs and at most 1408
// products per output. The reference accumulates the original FP32 values in
// double, independently of ncnn's packing and reduction order.
const double absolute_tolerance = 2e-5;
const double relative_tolerance = 2e-5;
const double packed_rounding = 1.0 / 1024.0;
const double packed_small = 1.0 / 16384.0;

// Inputs and biases are exact half values; weights are rounded once. The
// sum8 path also rounds each partial before the final output round. Use one
// half ulp per normal conversion and allow subnormal results to flush. An
// extra e*S covers FP32 accumulation (at most 176 products) conservatively.
double packed_error_budget(double expected, double product_magnitude, int partial_count)
{
    const double e = packed_rounding;
    const double before_output = partial_count ? (1 + e) * (1 + e) - 1 : e;
    return ((1 + e) * before_output + e) * product_magnitude
           + e * std::fabs(expected) + ((1 + e) * partial_count + 1) * packed_small;
}

size_t scalar_size(const ncnn::Option& opt)
{
    return opt.use_fp16_packed || opt.use_fp16_storage || opt.use_bf16_packed || opt.use_bf16_storage ? 2u : 4u;
}

struct GpuInstance {
    ~GpuInstance() { ncnn::destroy_gpu_instance(); }
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

struct LayerOwner {
    ncnn::Layer* layer;
    const ncnn::Option& opt;

    LayerOwner(const char* name, const ncnn::VulkanDevice* device, const ncnn::Option& o)
        : layer(ncnn::create_layer_vulkan(name)), opt(o)
    {
        if (layer) layer->vkdev = device;
    }

    ~LayerOwner() { destroy(); }

    int destroy()
    {
        if (!layer) return 0;
        const int result = layer->destroy_pipeline(opt);
        delete layer;
        layer = 0;
        return result;
    }
};

// create_pipeline() in these ncnn layers does not propagate Pipeline::create()
// errors. Inspect every allocated operator and child-layer pipeline explicitly.
int check_pipeline(const ncnn::Pipeline* pipeline, const char* name)
{
    if (!pipeline) return 0; // This shader variant was not selected.
    if (pipeline->pipeline() == VK_NULL_HANDLE) {
        std::fprintf(stderr, "FAIL: %s has no Vulkan pipeline\n", name);
        return 1;
    }
    const unsigned x = pipeline->local_size_x();
    const unsigned y = pipeline->local_size_y();
    const unsigned z = pipeline->local_size_z();
    CHECK(x && y && z && static_cast<unsigned long long>(x) * y * z <= 128);
    std::printf("  pipeline %s: local %u x %u x %u\n", name, x, y, z);
    return 0;
}

#define PIPELINE(layer, member) CHECK(check_pipeline((layer)->member, #member) == 0)

int check_conv_pipelines(const ncnn::Convolution1D_vulkan* layer)
{
    CHECK(layer->pipeline_convolution1d || layer->pipeline_convolution1d_1x1s1d1 || layer->pipeline_convolution1d_gemm);
    PIPELINE(layer, pipeline_convolution1d);
    PIPELINE(layer, pipeline_convolution1d_1x1s1d1);
    PIPELINE(layer, pipeline_convolution1d_gemm);
    CHECK(!layer->use_cooperative_matrix);
    CHECK(layer->padding && layer->padding->support_vulkan);
    const ncnn::Padding_vulkan* padding = static_cast<const ncnn::Padding_vulkan*>(layer->padding);
    PIPELINE(padding, pipeline_padding);
    PIPELINE(padding, pipeline_padding_pack4);
    PIPELINE(padding, pipeline_padding_pack1to4);
    PIPELINE(padding, pipeline_padding_pack4to1);
    PIPELINE(padding, pipeline_padding_3d);
    PIPELINE(padding, pipeline_padding_3d_pack4);
#if NCNN_INT8
    CHECK(!padding->pipeline_padding_int8 && !padding->pipeline_padding_pack4_int8);
    CHECK(!padding->pipeline_padding_pack1to4_int8 && !padding->pipeline_padding_pack4to1_int8);
    CHECK(!padding->pipeline_padding_3d_int8 && !padding->pipeline_padding_3d_pack4_int8);
#endif
    return 0;
}

int check_inner_pipelines(const ncnn::InnerProduct_vulkan* layer)
{
    CHECK(layer->pipeline_innerproduct || (layer->pipeline_innerproduct_sum8 && layer->pipeline_innerproduct_reduce_sum8));
    PIPELINE(layer, pipeline_innerproduct);
    PIPELINE(layer, pipeline_innerproduct_sum8);
    PIPELINE(layer, pipeline_innerproduct_reduce_sum8);
    PIPELINE(layer, pipeline_innerproduct_gemm);
    CHECK(layer->flatten && layer->flatten->support_vulkan);
    const ncnn::Flatten_vulkan* flatten = static_cast<const ncnn::Flatten_vulkan*>(layer->flatten);
    PIPELINE(flatten, pipeline_flatten);
    PIPELINE(flatten, pipeline_flatten_pack4);
    PIPELINE(flatten, pipeline_flatten_pack1to4);
#if NCNN_INT8
    CHECK(!layer->quantize && !layer->pipeline_innerproduct_int8);
    CHECK(!layer->pipeline_innerproduct_sum8_int8 && !layer->pipeline_innerproduct_reduce_sum8_int8);
    CHECK(!layer->pipeline_innerproduct_gemm_int8);
    CHECK(!flatten->pipeline_flatten_int8 && !flatten->pipeline_flatten_pack4_int8 && !flatten->pipeline_flatten_pack1to4_int8);
#endif
    return 0;
}

#undef PIPELINE

bool matches(double actual, double expected, int channel, int position, double& max_error, double packed_budget)
{
    const double error = std::fabs(actual - expected);
    const double limit = absolute_tolerance + relative_tolerance * std::fabs(expected) + packed_budget;
    if (error > max_error) max_error = error;
    if (!std::isfinite(actual) || error > limit) {
        std::fprintf(stderr, "FAIL output[%d,%d]: got %.9g, expected %.17g, abs error %.9g, limit %.9g\n",
                     channel, position, actual, expected, error, limit);
        return false;
    }
    return true;
}

int conv_case(const ncnn::VulkanDevice* device, const ncnn::Option& opt,
              int width, int input_channels, int output_channels, int kernel, bool compile_only, bool tiled_case = false)
{
    std::printf("CASE Convolution1D: width=%d in=%d out=%d kernel=%d valid stride=1 dilation=1\n",
                width, input_channels, output_channels, kernel);
    const int output_width = width - kernel + 1;
    ncnn::Mat input(width, input_channels);
    ncnn::Mat model[2] = {ncnn::Mat(output_channels * input_channels * kernel), ncnn::Mat(output_channels)};
    CHECK(!input.empty() && !model[0].empty() && !model[1].empty());
    for (int c = 0; c < input_channels; ++c)
        for (int x = 0; x < width; ++x)
            input.row<float>(c)[x] = ((x * 13 + c * 7) % 127 - 63) / 64.f;
    for (int o = 0; o < output_channels; ++o) {
        model[1][o] = ((o * 5) % 17 - 8) / 32.f;
        for (int c = 0; c < input_channels; ++c)
            for (int k = 0; k < kernel; ++k)
                model[0][(o * input_channels + c) * kernel + k] = ((o * 19 + c * 7 + k * 11) % 67 - 33) * 0.01f;
    }

    LayerOwner owner("Convolution1D", device, opt);
    CHECK(owner.layer && owner.layer->support_vulkan);
    ncnn::ParamDict params;
    params.set(0, output_channels);
    params.set(1, kernel);
    params.set(2, 1); // dilation
    params.set(3, 1); // stride
    params.set(4, 0); // left padding
    params.set(15, 0); // right padding
    params.set(5, 1); // bias
    params.set(6, output_channels * input_channels * kernel);
    params.set(9, 0); // no fused activation
    CHECK(owner.layer->load_param(params) == 0 && owner.layer->support_vulkan);
    CHECK(owner.layer->load_model(ncnn::ModelBinFromMatArray(model)) == 0);
    CHECK(owner.layer->create_pipeline(opt) == 0);
    const ncnn::Convolution1D_vulkan* conv = static_cast<const ncnn::Convolution1D_vulkan*>(owner.layer);
    CHECK(check_conv_pipelines(conv) == 0);
    if (tiled_case) {
        const char* tiled = std::getenv("RADISH_NCNN_TILED");
        const bool requested = tiled && !std::strcmp(tiled, "1");
        CHECK(conv->use_radish_tiled == requested);
        std::printf("  model-shaped variant: %s; K=%d; tiled grid=%d x %d\n",
                    conv->use_radish_tiled ? "Radish tiled" : "default", input_channels * kernel,
                    (output_width + 63) / 64, output_channels / 64);
        if (conv->use_radish_tiled) {
            CHECK(conv->pipeline_convolution1d_gemm);
            CHECK(conv->pipeline_convolution1d_gemm->local_size_x() == 16);
            CHECK(conv->pipeline_convolution1d_gemm->local_size_y() == 8);
            CHECK(conv->pipeline_convolution1d_gemm->local_size_z() == 1);
        }
    }
    if (compile_only) {
        CHECK(owner.destroy() == 0);
        std::puts("PASS: Convolution1D pipeline compilation; no model/input upload or operator submission");
        return 0;
    }

    {
        ncnn::VkTransfer transfer(device);
        CHECK(owner.layer->upload_model(transfer, opt) == 0);
        CHECK(!conv->weight_data_gpu.empty() && !conv->bias_data_gpu.empty());
        CHECK(transfer.submit_and_wait() == 0);
    }

    ncnn::VkMat uploaded, gpu_input, gpu_output;
    ncnn::Mat output;
    ncnn::VkCompute command(device);
    command.record_upload(input, uploaded, opt);
    CHECK(!uploaded.empty());
    const int input_pack = owner.layer->support_vulkan_packing && input_channels % 4 == 0 ? 4 : 1;
    device->convert_packing(uploaded, gpu_input, input_pack, command, opt);
    CHECK(!gpu_input.empty() && gpu_input.dims == 2 && gpu_input.w == width);
    CHECK(gpu_input.h * gpu_input.elempack == input_channels && gpu_input.elempack == input_pack);
    CHECK(gpu_input.elemsize == scalar_size(opt) * input_pack);
    const auto before_forward = command.pending_dispatch_total();
    CHECK(owner.layer->forward(gpu_input, gpu_output, command, opt) == 0);
    CHECK(command.pending_dispatch_total() > before_forward); // Upload/download alone cannot pass.
    CHECK(!gpu_output.empty() && gpu_output.dims == 2 && gpu_output.w == output_width);
    CHECK(gpu_output.h * gpu_output.elempack == output_channels);
    CHECK(gpu_output.elemsize == scalar_size(opt) * gpu_output.elempack);
    ncnn::Option download_opt = opt;
    download_opt.use_packing_layout = false;
    command.record_download(gpu_output, output, download_opt);
    CHECK(command.pending_dispatch_total() > 0);
    CHECK(command.submit_and_wait() == 0);
    CHECK(!output.empty() && output.dims == 2 && output.w == output_width && output.h == output_channels);
    CHECK(output.d == 1 && output.c == 1 && output.elempack == 1 && output.elemsize == 4u);
    // Mat::total() includes tail alignment, so validate logical shape above and
    // compare only its actual output elements below.
    CHECK(output.total() >= static_cast<size_t>(output_width * output_channels));
    double max_error = 0;
    bool correct = true;
    for (int o = 0; o < output_channels; ++o) {
        for (int x = 0; x < output_width; ++x) {
            double expected = model[1][o];
            double product_magnitude = 0;
            for (int c = 0; c < input_channels; ++c)
                for (int k = 0; k < kernel; ++k) {
                    const double product = static_cast<double>(input.row<float>(c)[x + k]) * model[0][(o * input_channels + c) * kernel + k];
                    expected += product;
                    product_magnitude += std::fabs(product);
                }
            const double budget = opt.use_fp16_packed ? packed_error_budget(expected, product_magnitude, 0) : 0;
            if (!matches(output.row<float>(o)[x], expected, o, x, max_error, budget)) correct = false;
        }
    }
    CHECK(correct);
    CHECK(owner.destroy() == 0);
    std::printf("PASS: Vulkan Convolution1D %d x %d -> %d x %d K%d, all %d outputs, max abs error %.9g\n",
                input_channels, width, output_channels, output_width, kernel, output_channels * output_width, max_error);
    return 0;
}

int inner_case(const ncnn::VulkanDevice* device, const ncnn::Option& opt,
               int input_count, int output_count, bool compile_only)
{
    std::printf("CASE InnerProduct: %d -> %d with bias\n", input_count, output_count);
    ncnn::Mat input(input_count);
    ncnn::Mat model[2] = {ncnn::Mat(input_count * output_count), ncnn::Mat(output_count)};
    CHECK(!input.empty() && !model[0].empty() && !model[1].empty());
    for (int i = 0; i < input_count; ++i) input[i] = ((i * 13) % 127 - 63) / 64.f;
    for (int o = 0; o < output_count; ++o) {
        model[1][o] = ((o * 5) % 17 - 8) / 32.f;
        for (int i = 0; i < input_count; ++i)
            model[0][o * input_count + i] = ((o * 19 + i * 7) % 67 - 33) * 0.01f;
    }

    LayerOwner owner("InnerProduct", device, opt);
    CHECK(owner.layer && owner.layer->support_vulkan);
    ncnn::ParamDict params;
    params.set(0, output_count);
    params.set(1, 1); // bias
    params.set(2, input_count * output_count);
    params.set(8, 0); // no int8 scales
    params.set(9, 0); // no fused activation
    CHECK(owner.layer->load_param(params) == 0 && owner.layer->support_vulkan);
    CHECK(owner.layer->load_model(ncnn::ModelBinFromMatArray(model)) == 0);
    const int input_pack = owner.layer->support_vulkan_packing && input_count % 4 == 0 ? 4 : 1;
    const int output_pack = output_count % 4 == 0 ? 4 : 1;
    // Match the packed 1D input/output produced by the Vulkan path. These hints
    // avoid compiling the unused batched (2D) GEMM branch.
    owner.layer->bottom_shapes.push_back(ncnn::Mat(input_count / input_pack, static_cast<void*>(0), scalar_size(opt) * input_pack, input_pack));
    owner.layer->top_shapes.push_back(ncnn::Mat(output_count / output_pack, static_cast<void*>(0), scalar_size(opt) * output_pack, output_pack));
    CHECK(owner.layer->create_pipeline(opt) == 0);
    const ncnn::InnerProduct_vulkan* inner = static_cast<const ncnn::InnerProduct_vulkan*>(owner.layer);
    CHECK(check_inner_pipelines(inner) == 0);
    if (compile_only) {
        CHECK(owner.destroy() == 0);
        std::puts("PASS: InnerProduct pipeline compilation; no model/input upload or operator submission");
        return 0;
    }

    {
        ncnn::VkTransfer transfer(device);
        CHECK(owner.layer->upload_model(transfer, opt) == 0);
        CHECK(!inner->weight_data_gpu.empty() && !inner->bias_data_gpu.empty());
        CHECK(transfer.submit_and_wait() == 0);
    }

    ncnn::VkMat uploaded, gpu_input, gpu_output;
    ncnn::Mat output;
    ncnn::VkCompute command(device);
    command.record_upload(input, uploaded, opt);
    CHECK(!uploaded.empty());
    device->convert_packing(uploaded, gpu_input, input_pack, command, opt);
    CHECK(!gpu_input.empty() && gpu_input.dims == 1 && gpu_input.w * gpu_input.elempack == input_count);
    CHECK(gpu_input.elempack == input_pack && gpu_input.elemsize == scalar_size(opt) * input_pack);
    const auto before_forward = command.pending_dispatch_total();
    CHECK(owner.layer->forward(gpu_input, gpu_output, command, opt) == 0);
    CHECK(command.pending_dispatch_total() > before_forward);
    CHECK(!gpu_output.empty() && gpu_output.dims == 1 && gpu_output.w * gpu_output.elempack == output_count);
    CHECK(gpu_output.elemsize == scalar_size(opt) * gpu_output.elempack);
    ncnn::Option download_opt = opt;
    download_opt.use_packing_layout = false;
    command.record_download(gpu_output, output, download_opt);
    CHECK(command.pending_dispatch_total() > 0);
    CHECK(command.submit_and_wait() == 0);
    CHECK(!output.empty() && output.dims == 1 && output.w == output_count && output.h == 1 && output.d == 1 && output.c == 1);
    CHECK(output.elempack == 1 && output.elemsize == 4u && output.total() >= static_cast<size_t>(output_count));
    double max_error = 0;
    bool correct = true;
    const int partial_count = inner->pipeline_innerproduct_sum8 ? (input_count / input_pack + 7) / 8 : 0;
    for (int o = 0; o < output_count; ++o) {
        double expected = model[1][o];
        double product_magnitude = 0;
        for (int i = 0; i < input_count; ++i) {
            const double product = static_cast<double>(input[i]) * model[0][o * input_count + i];
            expected += product;
            product_magnitude += std::fabs(product);
        }
        const double budget = opt.use_fp16_packed ? packed_error_budget(expected, product_magnitude, partial_count) : 0;
        if (!matches(output[o], expected, o, 0, max_error, budget)) correct = false;
    }
    CHECK(correct);
    CHECK(owner.destroy() == 0);
    std::printf("PASS: Vulkan InnerProduct %d -> %d, all %d outputs, max abs error %.9g\n", input_count, output_count, output_count, max_error);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    // Flush each stage so a driver hang still leaves a useful device-side log.
    std::setvbuf(stdout, 0, _IONBF, 0);
    bool conv = false, inner = false, compile_only = false, smoke = false, fp16_packed = false, tiled_only = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--conv")) conv = true;
        else if (!std::strcmp(argv[i], "--inner")) inner = true;
        else if (!std::strcmp(argv[i], "--compile-only")) compile_only = true;
        else if (!std::strcmp(argv[i], "--smoke")) smoke = true;
        else if (!std::strcmp(argv[i], "--fp16-packed")) fp16_packed = true;
        else if (!std::strcmp(argv[i], "--tiled-only")) tiled_only = true;
        else {
            std::fprintf(stderr, "Usage: %s [--conv] [--inner] [--compile-only] [--smoke] [--fp16-packed] | --tiled-only [--compile-only]\n", argv[0]);
            return 1;
        }
    }
    if (tiled_only && (conv || inner || smoke || fp16_packed)) {
        std::fputs("--tiled-only is FP32 and cannot be combined with --conv, --inner, --smoke or --fp16-packed\n", stderr);
        return 1;
    }
    if (!conv && !inner && !tiled_only) conv = inner = true;
    CHECK(ncnn::create_gpu_instance() == 0);
    GpuInstance instance;
    CHECK(ncnn::get_gpu_count() == 1);
    const ncnn::VulkanDevice* device = ncnn::get_gpu_device(0);
    CHECK(device && device->is_valid());
    std::printf("ncnn device: %s; FP32 atol=%g rtol=%g\n", device->info.device_name(), absolute_tolerance, relative_tolerance);
    std::printf("storage: %s; arithmetic: FP32\n", fp16_packed ? "FP16 packed" : "FP32");
    if (fp16_packed)
        std::printf("packed comparison adds [(1+e)*((1+e)^r-1)+e]*S + e*abs(ref) + [(1+e)*m+1]*a; e=%.12g a=%.12g, S=sum(abs(original input*weight)), r=1 weights or 2 weights+sum8, m=sum8 partial count\n",
                    packed_rounding, packed_small);

    ncnn::Option opt;
    opt.use_vulkan_compute = true;
    opt.use_packing_layout = true;
    opt.use_sgemm_convolution = true;
    opt.use_fp16_packed = fp16_packed;
    opt.use_fp16_storage = false;
    opt.use_fp16_arithmetic = false;
    opt.use_fp16_uniform = false;
    opt.use_bf16_packed = false;
    opt.use_bf16_storage = false;
    opt.use_int8_inference = false;
    opt.use_int8_packed = false;
    opt.use_int8_storage = false;
    opt.use_int8_arithmetic = false;
    opt.use_int8_uniform = false;
    opt.use_int16_packed = false;
    opt.use_int16_storage = false;
    opt.use_subgroup_ops = false;
    opt.use_shader_local_memory = false;
    opt.use_cooperative_matrix = false;
    opt.use_tensor_storage = false;
    opt.use_weights_in_host_memory = false;
    Allocators allocators(device, opt);
    CHECK(opt.blob_vkallocator && opt.staging_vkallocator);

    if (tiled_only) CHECK(conv_case(device, opt, 213, 128, 128, 11, compile_only, true) == 0);
    if (conv) {
        CHECK(conv_case(device, opt, 33, 3, 5, 3, compile_only) == 0);
        if (!smoke) {
            CHECK(conv_case(device, opt, 33, 4, 8, 7, compile_only) == 0);
            CHECK(conv_case(device, opt, 213, 16, 32, 11, compile_only) == 0);
        }
    }
    if (inner) {
        CHECK(inner_case(device, opt, 13, 7, compile_only) == 0);
        if (!smoke) CHECK(inner_case(device, opt, 128, 1028, compile_only) == 0);
    }
    return 0;
}
