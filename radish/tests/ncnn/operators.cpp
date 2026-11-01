#include "command.h"
#include "gpu.h"
#include "layer.h"
#include "modelbin.h"
#include "net.h"
#include "pipeline.h"
#include "layer/vulkan/binaryop_vulkan.h"
#include "layer/vulkan/convolution1d_vulkan.h"
#include "layer/vulkan/flatten_vulkan.h"
#include "layer/vulkan/innerproduct_vulkan.h"
#include "layer/vulkan/instancenorm_vulkan.h"
#include "layer/vulkan/padding_vulkan.h"
#include "layer/vulkan/reshape_vulkan.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

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

int check_reshape_pipelines(const ncnn::Reshape_vulkan* layer)
{
    CHECK(layer->pipeline_reshape || layer->pipeline_reshape_pack4);
    PIPELINE(layer, pipeline_reshape);
    PIPELINE(layer, pipeline_reshape_pack4);
    PIPELINE(layer, pipeline_reshape_pack1to4);
    PIPELINE(layer, pipeline_reshape_pack4to1);
#if NCNN_BATCH
    CHECK(!layer->pipeline_reshape_batch_reorder && !layer->pipeline_reshape_batch_reorder_pack4);
    CHECK(!layer->pipeline_reshape_batch_reorder_pack1to4 && !layer->pipeline_reshape_batch_reorder_pack4to1);
#endif
    return 0;
}

int check_norm_pipelines(const ncnn::InstanceNorm_vulkan* layer)
{
    CHECK(layer->pipeline_instancenorm_norm || layer->pipeline_instancenorm_norm_pack4);
    PIPELINE(layer, pipeline_instancenorm_reduce_sum4_fp16_to_fp32);
    PIPELINE(layer, pipeline_instancenorm_reduce_sum4_fp16_to_fp32_pack4);
    for (int i = 0; i < 2; ++i) {
        PIPELINE(layer, pipeline_instancenorm_reduce_sum4_fp32[i]);
        PIPELINE(layer, pipeline_instancenorm_reduce_sum4_fp32_pack4[i]);
    }
    PIPELINE(layer, pipeline_instancenorm_reduce_mean);
    PIPELINE(layer, pipeline_instancenorm_reduce_mean_pack4);
    PIPELINE(layer, pipeline_instancenorm_sub_mean_square);
    PIPELINE(layer, pipeline_instancenorm_sub_mean_square_pack4);
    PIPELINE(layer, pipeline_instancenorm_coeffs);
    PIPELINE(layer, pipeline_instancenorm_coeffs_pack4);
    PIPELINE(layer, pipeline_instancenorm_norm);
    PIPELINE(layer, pipeline_instancenorm_norm_pack4);
    CHECK(!layer->pipeline_instancenorm_reduce_subgroup && !layer->pipeline_instancenorm_reduce_subgroup_pack4);
    return 0;
}

int check_binary_pipelines(const ncnn::BinaryOp_vulkan* layer)
{
    CHECK(layer->pipeline_binaryop || layer->pipeline_binaryop_pack4);
    PIPELINE(layer, pipeline_binaryop);
    PIPELINE(layer, pipeline_binaryop_pack4);
    for (int i = 0; i < 2; ++i) {
        PIPELINE(layer, pipeline_binaryop_broadcast[i]);
        PIPELINE(layer, pipeline_binaryop_broadcast_pack4[i]);
        PIPELINE(layer, pipeline_binaryop_broadcast_pack1to4[i]);
    }
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
              int width, int input_channels, int output_channels, int kernel, bool compile_only,
              bool tiled_case = false, int dilation = 1)
{
    std::printf("CASE Convolution1D: width=%d in=%d out=%d kernel=%d valid stride=1 dilation=%d\n",
                width, input_channels, output_channels, kernel, dilation);
    const int output_width = width - dilation * (kernel - 1);
    CHECK(output_width > 0);
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
    params.set(2, dilation);
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
                    const double product = static_cast<double>(input.row<float>(c)[x + k * dilation]) * model[0][(o * input_channels + c) * kernel + k];
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

int reshape_norm_case(const ncnn::VulkanDevice* device, const ncnn::Option& opt,
                      int width, int channels, bool compile_only)
{
    std::printf("CASE Reshape/InstanceNorm roundtrip: width=%d channels=%d retained Split residual\n", width, channels);
    const int pack = channels % 4 == 0 ? 4 : 1;
    const char* alias_env = std::getenv("RADISH_NCNN_RESHAPE_ALIAS");
    const bool alias = pack == 4 && alias_env && !std::strcmp(alias_env, "1");
    CHECK(device->info.vendor_id() == 0x5143 && device->info.device_id() == 506);
    CHECK(!std::strcmp(device->info.device_name(), "Radish Adreno 506 (experimental compute)"));
    CHECK(!opt.use_fp16_packed && scalar_size(opt) == 4u);

    std::ostringstream graph;
    const std::string shape2 = "2," + std::to_string(width) + "," + std::to_string(channels) + ",1,1";
    const std::string shape3 = "3," + std::to_string(width) + ",1,1," + std::to_string(channels);
    graph << "7767517\n6 8\n"
          << "Input input 0 1 input -23330=5," << shape2 << "\n"
          << "Split split 1 3 input norm_input residual retained -23330=15," << shape2 << ',' << shape2 << ',' << shape2 << "\n"
          << "Reshape to3d 1 1 norm_input rank3 0=-1 1=1 2=" << channels << " -23330=5," << shape3 << "\n"
          << "InstanceNorm norm 1 1 rank3 normalized 0=" << channels << " 1=1.000000e-5 2=0 -23330=5," << shape3 << "\n"
          << "Reshape to2d 1 1 normalized norm2 0=-1 1=" << channels << " -23330=5," << shape2 << "\n"
          << "BinaryOp add 2 1 norm2 residual output 0=0 -23330=5," << shape2 << "\n";

    ncnn::Net net;
    net.opt = opt;
    net.set_vulkan_device(device);
    CHECK(net.load_param_mem(graph.str().c_str()) == 0 && net.opt.use_vulkan_compute);
    CHECK(net.layers().size() == 6);
    ncnn::Mat no_weights;
    for (size_t i = 0; i < net.layers().size(); ++i) {
        ncnn::Layer* layer = net.mutable_layers()[i];
        CHECK(layer && (i == 0 || layer->support_vulkan));
        CHECK(layer->load_model(ncnn::ModelBinFromMatArray(&no_weights)) == 0);
        CHECK(layer->create_pipeline(net.opt) == 0);
        if (i == 2 || i == 4)
            CHECK(check_reshape_pipelines(static_cast<const ncnn::Reshape_vulkan*>(layer)) == 0);
        if (i == 3)
            CHECK(check_norm_pipelines(static_cast<const ncnn::InstanceNorm_vulkan*>(layer)) == 0);
        if (i == 5)
            CHECK(check_binary_pipelines(static_cast<const ncnn::BinaryOp_vulkan*>(layer)) == 0);
    }
    if (compile_only) {
        std::puts("PASS: normalization roundtrip pipeline compilation; no upload or submission");
        return 0;
    }

    ncnn::Mat input(width, channels);
    CHECK(!input.empty());
    for (int c = 0; c < channels; ++c)
        for (int x = 0; x < width; ++x)
            input.row<float>(c)[x] = ((x * 13 + c * 7) % 127 - 63) / 64.f + c / 16.f;

    ncnn::Extractor ex = net.create_extractor();
    ex.set_light_mode(true);
    ex.set_blob_vkallocator(opt.blob_vkallocator);
    ex.set_workspace_vkallocator(opt.workspace_vkallocator);
    ex.set_staging_vkallocator(opt.staging_vkallocator);
    ncnn::VkCompute command(device);
    ncnn::VkMat uploaded;
    command.record_upload(input, uploaded, opt);
    CHECK(!uploaded.empty() && uploaded.elempack == pack && uploaded.elemsize == 4u * pack);
    CHECK(ex.input("input", uploaded) == 0);
    uploaded.release();
    CHECK(command.submit_and_wait() == 0 && command.reset() == 0);

    ncnn::VkMat rank3, retained;
    const auto before3 = command.pending_dispatch_total();
    CHECK(ex.extract("rank3", rank3, command) == 0);
    CHECK(ex.extract("retained", retained, command) == 0);
    CHECK(rank3.dims == 3 && rank3.w == width && rank3.h == 1 && rank3.c * pack == channels);
    CHECK(rank3.elempack == pack && rank3.elemsize == 4u * pack);
    CHECK((command.pending_dispatch_total() == before3) == alias);
    CHECK((rank3.data == retained.data) == alias);
    if (alias) {
        CHECK(rank3.cstep == static_cast<size_t>(width));
        CHECK(rank3.total() == retained.total() && rank3.buffer_capacity() == retained.buffer_capacity());
        CHECK(rank3.buffer_offset() == retained.buffer_offset() && rank3.refcount == retained.refcount);
        CHECK(rank3.data->access_flags == retained.data->access_flags && rank3.data->stage_flags == retained.data->stage_flags);
#if NCNN_BATCH
        CHECK(rank3.n == 1 && rank3.nstep == rank3.total() && retained.nstep == rank3.nstep);
#endif
    } else if (pack == 1) {
        CHECK(rank3.cstep > static_cast<size_t>(width));
    }
    ncnn::VkBufferMemory* retained_data = retained.data;
    rank3.release();
    retained.release();

    ncnn::VkMat normalized, norm2, gpu_output;
    const auto before_norm = command.pending_dispatch_total();
    CHECK(ex.extract("normalized", normalized, command) == 0);
    CHECK(command.pending_dispatch_total() > before_norm);
    CHECK(normalized.data != retained_data);
    const auto before2 = command.pending_dispatch_total();
    CHECK(ex.extract("norm2", norm2, command) == 0);
    CHECK((command.pending_dispatch_total() == before2) == alias);
    CHECK((norm2.data == normalized.data) == alias);
    CHECK(norm2.dims == 2 && norm2.w == width && norm2.h * pack == channels);
    if (alias) {
        CHECK(norm2.cstep == static_cast<size_t>(width) * channels / pack);
        CHECK(norm2.total() == normalized.total() && norm2.buffer_capacity() == normalized.buffer_capacity());
        CHECK(norm2.buffer_offset() == normalized.buffer_offset() && norm2.refcount == normalized.refcount);
#if NCNN_BATCH
        CHECK(norm2.nstep == norm2.total() && norm2.nstep == normalized.nstep);
#endif
    }
    CHECK(ex.extract("output", gpu_output, command) == 0);
    CHECK(ex.extract("retained", retained, command) == 0 && retained.data == retained_data);
    ncnn::Option download = opt;
    download.use_packing_layout = false;
    ncnn::Mat output, norm_output, residual;
    command.record_download(gpu_output, output, download);
    command.record_download(norm2, norm_output, download);
    command.record_download(retained, residual, download);
    CHECK(command.submit_and_wait() == 0);
    CHECK(!output.empty() && !norm_output.empty() && !residual.empty());
    CHECK(output.dims == 2 && output.w == width && output.h == channels && output.elempack == 1 && output.elemsize == 4u);
    CHECK(norm_output.dims == 2 && norm_output.w == width && norm_output.h == channels && norm_output.elempack == 1 && norm_output.elemsize == 4u);
    CHECK(residual.dims == 2 && residual.w == width && residual.h == channels && residual.elempack == 1 && residual.elemsize == 4u);
    double max_error = 0;
    const double epsilon = static_cast<double>(1e-5f);
    for (int c = 0; c < channels; ++c) {
        const float* original = input.row<float>(c);
        double mean = 0, variance = 0;
        for (int x = 0; x < width; ++x) mean += original[x];
        mean /= width;
        for (int x = 0; x < width; ++x) {
            const double delta = original[x] - mean;
            variance += delta * delta;
        }
        variance /= width;
        for (int x = 0; x < width; ++x) {
            const double expected = (original[x] - mean) / std::sqrt(variance + epsilon);
            CHECK(matches(norm_output.row<float>(c)[x], expected, c, x, max_error, 0));
            CHECK(matches(output.row<float>(c)[x], original[x] + expected, c, x, max_error, 0));
        }
        CHECK(!std::memcmp(residual.row<float>(c), original, static_cast<size_t>(width) * sizeof(float)));
    }
    std::printf("PASS: %s roundtrip, all %d normalized + %d residual-sum outputs, retained residual bit-exact, max abs error %.9g\n",
                alias ? "pack4 alias" : "copy fallback", width * channels, width * channels, max_error);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    // Flush each stage so a driver hang still leaves a useful device-side log.
    std::setvbuf(stdout, 0, _IONBF, 0);
    bool conv = false, inner = false, compile_only = false, smoke = false, fp16_packed = false, tiled_only = false, reshape_only = false;
    int dilation = 1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--conv")) conv = true;
        else if (!std::strcmp(argv[i], "--inner")) inner = true;
        else if (!std::strcmp(argv[i], "--compile-only")) compile_only = true;
        else if (!std::strcmp(argv[i], "--smoke")) smoke = true;
        else if (!std::strcmp(argv[i], "--fp16-packed")) fp16_packed = true;
        else if (!std::strcmp(argv[i], "--tiled-only")) tiled_only = true;
        else if (!std::strcmp(argv[i], "--reshape-only")) reshape_only = true;
        else if (!std::strcmp(argv[i], "--dilation3") || !std::strcmp(argv[i], "--dilation5")) {
            if (dilation != 1) {
                std::fputs("select at most one of --dilation3 or --dilation5\n", stderr);
                return 1;
            }
            dilation = !std::strcmp(argv[i], "--dilation3") ? 3 : 5;
        }
        else {
            std::fprintf(stderr, "Usage: %s [--conv] [--inner] [--compile-only] [--smoke] [--fp16-packed] | --tiled-only [--compile-only] [--dilation3 | --dilation5] | --reshape-only [--compile-only]\n", argv[0]);
            return 1;
        }
    }
    if (dilation != 1 && !tiled_only) {
        std::fputs("--dilation3 and --dilation5 require --tiled-only\n", stderr);
        return 1;
    }
    if (tiled_only && (conv || inner || smoke || fp16_packed)) {
        std::fputs("--tiled-only is FP32 and cannot be combined with --conv, --inner, --smoke or --fp16-packed\n", stderr);
        return 1;
    }
    if (reshape_only && (conv || inner || smoke || fp16_packed || tiled_only || dilation != 1)) {
        std::fputs("--reshape-only is FP32 and cannot be combined with another case selector\n", stderr);
        return 1;
    }
    if (!conv && !inner && !tiled_only && !reshape_only) conv = inner = true;
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

    if (reshape_only) {
        CHECK(reshape_norm_case(device, opt, 33, 8, compile_only) == 0);
        CHECK(reshape_norm_case(device, opt, 213, 8, compile_only) == 0);
        CHECK(reshape_norm_case(device, opt, 213, 3, compile_only) == 0);
    }
    if (tiled_only) CHECK(conv_case(device, opt, 213, 128, 128, 11, compile_only, true, dilation) == 0);
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
