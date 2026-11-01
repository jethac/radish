#include "command.h"
#include "gpu.h"
#include "layer.h"
#include "modelbin.h"
#include "pipeline.h"
#include "layer/vulkan/convolution1d_vulkan.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {
const int width = 213;
const int channels = 128;
const int kernel = 11;
const int output_values = width * channels;
const double atol = 2e-5;
const double rtol = 2e-5;
const int warmup_runs = 4;
const int total_runs = 12;
const int schedule[total_runs] = {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0};

struct InstanceLifetime {
    ~InstanceLifetime() { ncnn::destroy_gpu_instance(); }
};

struct DiagnosticEnvironment {
    bool present[2];
    std::string previous[2];
    const char* keys[2] = {"RADISH_GPU_ELAPSED", "RADISH_TILED_STATE_REUSE"};
    DiagnosticEnvironment()
    {
        for (int i = 0; i < 2; ++i) {
            const char* value = std::getenv(keys[i]);
            present[i] = value != 0;
            previous[i] = value ? value : "";
        }
    }
    ~DiagnosticEnvironment()
    {
        for (int i = 0; i < 2; ++i) {
            if (present[i]) setenv(keys[i], previous[i].c_str(), 1);
            else unsetenv(keys[i]);
        }
    }
};

struct Allocators {
    const ncnn::VulkanDevice* device;
    ncnn::Option& opt;
    Allocators(const ncnn::VulkanDevice* d, ncnn::Option& o, bool active) : device(d), opt(o)
    {
        if (active) {
            opt.blob_vkallocator = device->acquire_blob_allocator();
            opt.workspace_vkallocator = opt.blob_vkallocator;
            opt.staging_vkallocator = device->acquire_staging_allocator();
        }
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
    LayerOwner(const ncnn::VulkanDevice* device, const ncnn::Option& o)
        : layer(ncnn::create_layer_vulkan("Convolution1D")), opt(o)
    {
        if (layer) layer->vkdev = device;
    }
    ~LayerOwner()
    {
        if (layer) {
            layer->destroy_pipeline(opt);
            delete layer;
        }
    }
};

struct OutputDirectory {
    int fd;
    OutputDirectory() : fd(-1) {}
    ~OutputDirectory() { if (fd >= 0) close(fd); }
    int create(const char* path)
    {
        CHECK(path && path[0] == '/' && path[1] != '\0');
        CHECK(mkdir(path, 0700) == 0);
        fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        CHECK(fd >= 0);
        FILE* file = exclusive("pending.json");
        CHECK(file);
        const int wrote = std::fputs("{\"format\":\"radish-state-reuse-probe-v1\",\"status\":\"pending\",\"raw_outputs_validated\":false}\n", file);
        const int closed = std::fclose(file);
        CHECK(wrote >= 0 && closed == 0);
        return 0;
    }
    FILE* exclusive(const char* name) const
    {
        const int out = openat(fd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (out < 0) return 0;
        FILE* file = fdopen(out, "wb");
        if (!file) close(out);
        return file;
    }
    int write_raw(const char* name, const ncnn::Mat& value) const
    {
        CHECK(value.dims == 2 && value.w == width && value.h == channels && value.d == 1 && value.c == 1);
        CHECK(value.elempack == 1 && value.elemsize == 4u);
        FILE* file = exclusive(name);
        CHECK(file);
        bool okay = true;
        for (int c = 0; c < channels; ++c)
            if (std::fwrite(value.row<const float>(c), sizeof(float), width, file) != static_cast<size_t>(width)) okay = false;
        const int closed = std::fclose(file);
        CHECK(okay && closed == 0);
        return 0;
    }
    int complete(const double* elapsed, const double* max_error) const
    {
        FILE* file = exclusive("result.json.tmp");
        CHECK(file);
        std::fprintf(file, "{\n  \"format\":\"radish-state-reuse-probe-v1\",\n  \"status\":\"passed\",\n  \"width\":213,\"channels\":128,\"kernel\":11,\"dilation\":1,\"padding\":5,\n  \"warmup_runs\":4,\"measured_runs\":8,\"total_runs\":12,\"expected_timestamp_samples\":12,\"expected_reuse_samples\":6,\n  \"output_values_per_run\":27264,\"output_bytes_per_run\":109056,\"total_validated_values\":327168,\n  \"absolute_tolerance\":2e-5,\"relative_tolerance\":2e-5,\n  \"all_outputs_bit_identical\":true,\"input_preserved\":true,\"per_run_output_poison\":true,\n  \"raw_capture_before_reference\":true,\n  \"timing_scope\":\"Forward construction, changing output poison/flush, submit, wait and download; excludes CPU reference, raw file writes, uploads and pipeline creation. GPU timestamps exclude the host poison; driver math/GPU sample validation is separate.\",\n  \"runs\":[\n");
        for (int i = 0; i < total_runs; ++i)
            std::fprintf(file, "    {\"index\":%d,\"phase\":\"%s\",\"state_reuse\":%d,\"output\":\"run-%02d.f32\",\"construct_submit_wait_download_seconds\":%.17g,\"max_abs_error\":%.17g}%s\n",
                         i, i < warmup_runs ? "warmup" : "measured", schedule[i], i, elapsed[i], max_error[i], i + 1 == total_runs ? "" : ",");
        std::fputs("  ]\n}\n", file);
        const bool okay = std::ferror(file) == 0;
        const int closed = std::fclose(file);
        CHECK(okay && closed == 0);
        CHECK(linkat(fd, "result.json.tmp", fd, "result.json", 0) == 0);
        CHECK(unlinkat(fd, "result.json.tmp", 0) == 0);
        CHECK(unlinkat(fd, "pending.json", 0) == 0);
        return 0;
    }
};

int check_math(const ncnn::Pipeline& pipeline)
{
    CHECK(pipeline.pipeline() != VK_NULL_HANDLE);
    CHECK(pipeline.local_size_x() == 16 && pipeline.local_size_y() == 8 && pipeline.local_size_z() == 1);
    CHECK(pipeline.shader_info().specialization_count == 5);
    CHECK(pipeline.shader_info().binding_count == 4 && pipeline.shader_info().push_constant_count == 2);
    for (int i = 0; i < 4; ++i) CHECK(pipeline.shader_info().binding_types[i] == 1);
    return 0;
}

int run_forward(int index, ncnn::Convolution1D_vulkan& conv, const ncnn::VkMat& input,
                const ncnn::VulkanDevice* device, const ncnn::Option& opt,
                ncnn::Mat& output, double& elapsed)
{
    CHECK(setenv("RADISH_TILED_STATE_REUSE", schedule[index] ? "1" : "0", 1) == 0);
    CHECK(setenv("RADISH_GPU_ELAPSED", "1", 1) == 0);
    ncnn::VkMat gpu_output;
    ncnn::VkCompute command(device);
    const auto weight_buffer = conv.weight_data_gpu.data;
    const auto bias_buffer = conv.bias_data_gpu.data;
    const auto input_buffer = input.data;
    const ncnn::Pipeline* pipeline = conv.pipeline_convolution1d_gemm;
    const char* phase = index < warmup_runs ? "warmup" : "measured";
    std::printf("GPU_STATE_REUSE_RUN_BEGIN index=%d phase=%s state_reuse=%d expected_native_length=27 grid=4,2,1 local=16,8,1 math_groups=8 expected_math_chunks=8 expected_math_submits=1 padding_groups=144 pitch=268\n", index, phase, schedule[index]);
    std::fflush(stdout);
    const auto begin = std::chrono::steady_clock::now();
    const auto before = command.pending_dispatch_total();
    CHECK(conv.forward(input, gpu_output, command, opt) == 0);
    CHECK(command.pending_dispatch_total() - before == 152);
    CHECK(conv.weight_data_gpu.data == weight_buffer && conv.bias_data_gpu.data == bias_buffer && input.data == input_buffer);
    CHECK(conv.pipeline_convolution1d_gemm == pipeline);
    CHECK(!gpu_output.empty() && gpu_output.dims == 2 && gpu_output.w == width);
    CHECK(gpu_output.h == channels / 4 && gpu_output.elempack == 4 && gpu_output.elemsize == 16u);
    CHECK(gpu_output.allocator == opt.blob_vkallocator && gpu_output.allocator->mappable);
    CHECK(gpu_output.data && gpu_output.data->mapped_ptr);
    CHECK(gpu_output.total() * gpu_output.elemsize == static_cast<size_t>(output_values) * sizeof(float));
    CHECK(gpu_output.buffer_capacity() >= static_cast<size_t>(output_values) * sizeof(float));
    unsigned char* mapped = static_cast<unsigned char*>(gpu_output.mapped_ptr());
    CHECK(mapped);
    for (int i = 0; i < output_values; ++i) {
        const uint32_t poison = 0x7fc00000u | (static_cast<uint32_t>(index + 1) << 12) | (static_cast<uint32_t>(i) & 0xfffu);
        std::memcpy(mapped + static_cast<size_t>(i) * sizeof(float), &poison, sizeof(poison));
    }
    CHECK(gpu_output.allocator->flush(gpu_output.data) == 0);
    std::printf("GPU_STATE_REUSE_OUTPUT_POISON index=%d values=27264 changing_nan=1 flushed=1 before_submit=1\n", index);
    std::fflush(stdout);
    ncnn::Option download = opt;
    download.use_packing_layout = false;
    command.record_download(gpu_output, output, download);
    CHECK(command.submit_and_wait() == 0);
    elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    CHECK(std::isfinite(elapsed) && elapsed > 0);
    CHECK(!output.empty() && output.dims == 2 && output.w == width && output.h == channels);
    CHECK(output.d == 1 && output.c == 1 && output.elempack == 1 && output.elemsize == 4u);
    std::printf("GPU_STATE_REUSE_RUN_END index=%d phase=%s state_reuse=%d status=SUBMITTED construct_submit_wait_download_seconds=%.9f\n", index, phase, schedule[index], elapsed);
    std::fflush(stdout);
    return 0;
}

int probe(bool compile_only, const OutputDirectory& destination,
          const ncnn::VulkanDevice* device, ncnn::Option& opt)
{
    Allocators allocators(device, opt, !compile_only);
    CHECK(compile_only || (opt.blob_vkallocator && opt.staging_vkallocator));
    LayerOwner owner(device, opt);
    CHECK(owner.layer && owner.layer->support_vulkan);
    ncnn::Mat input(width, channels);
    ncnn::Mat model[2] = {ncnn::Mat(channels * channels * kernel), ncnn::Mat(channels)};
    CHECK(!input.empty() && !model[0].empty() && !model[1].empty());
    for (int c = 0; c < channels; ++c)
        for (int x = 0; x < width; ++x)
            input.row<float>(c)[x] = ((x * 13 + c * 7) % 127 - 63) / 64.f;
    for (int o = 0; o < channels; ++o) {
        model[1][o] = ((o * 5) % 17 - 8) / 32.f;
        for (int c = 0; c < channels; ++c)
            for (int k = 0; k < kernel; ++k)
                model[0][(o * channels + c) * kernel + k] = ((o * 19 + c * 7 + k * 11) % 67 - 33) * 0.01f;
    }
    const ncnn::Mat input_before = input.clone();
    CHECK(!input_before.empty());
    ncnn::ParamDict pd;
    pd.set(0, channels); pd.set(1, kernel); pd.set(2, 1); pd.set(3, 1);
    pd.set(4, 5); pd.set(15, 5);
    pd.set(5, 1); pd.set(6, channels * channels * kernel); pd.set(9, 0);
    CHECK(owner.layer->load_param(pd) == 0);
    CHECK(owner.layer->load_model(ncnn::ModelBinFromMatArray(model)) == 0);
    std::puts("GPU_STATE_REUSE_PIPELINE_BEGIN accepted_spec=11,128,128,1,1 local=16,8,1");
    std::fflush(stdout);
    CHECK(owner.layer->create_pipeline(opt) == 0);
    ncnn::Convolution1D_vulkan& conv = *static_cast<ncnn::Convolution1D_vulkan*>(owner.layer);
    CHECK(conv.use_radish_tiled && !conv.radish_tiled32 && conv.radish_planar && !conv.radish_pad_channels);
    CHECK(!conv.use_cooperative_matrix && conv.pipeline_convolution1d_gemm);
    CHECK(check_math(*conv.pipeline_convolution1d_gemm) == 0);
    CHECK(conv.pipeline_radish_planar_pad && conv.pipeline_radish_planar_pad->pipeline());
    CHECK(conv.pipeline_radish_planar_pad->local_size_x() == 8 && conv.pipeline_radish_planar_pad->local_size_y() == 8 && conv.pipeline_radish_planar_pad->local_size_z() == 1);
    CHECK(conv.pipeline_radish_planar_pad->shader_info().binding_count == 2 && conv.pipeline_radish_planar_pad->shader_info().push_constant_count == 4);
    CHECK(!conv.weight_data_packed.empty());
    CHECK(conv.weight_data_packed.total() == model[0].total());
    CHECK(std::memcmp(conv.weight_data_packed.data, model[0].data, model[0].total() * sizeof(float)) == 0);
    std::puts("GPU_STATE_REUSE_PIPELINE_END status=PASS accepted_builtin=1");
    if (compile_only) {
        std::puts("PASS: GPU state reuse probe compile-only; no probe VkTransfer/VkCompute/tensor upload (device initialization may submit)");
        return 0;
    }
    {
        ncnn::VkTransfer transfer(device);
        CHECK(conv.upload_model(transfer, opt) == 0);
        CHECK(!conv.weight_data_gpu.empty() && !conv.bias_data_gpu.empty());
        CHECK(transfer.submit_and_wait() == 0);
    }
    ncnn::VkMat uploaded, gpu_input;
    {
        ncnn::VkCompute upload(device);
        upload.record_upload(input, uploaded, opt);
        CHECK(!uploaded.empty());
        device->convert_packing(uploaded, gpu_input, 4, upload, opt);
        CHECK(!gpu_input.empty() && gpu_input.dims == 2 && gpu_input.w == width);
        CHECK(gpu_input.h == channels / 4 && gpu_input.elempack == 4 && gpu_input.elemsize == 16u);
        CHECK(upload.submit_and_wait() == 0);
    }
    ncnn::Mat output[total_runs];
    double elapsed[total_runs] = {};
    for (int i = 0; i < total_runs; ++i)
        CHECK(run_forward(i, conv, gpu_input, device, opt, output[i], elapsed[i]) == 0);
    CHECK(setenv("RADISH_TILED_STATE_REUSE", "0", 1) == 0);
    CHECK(setenv("RADISH_GPU_ELAPSED", "0", 1) == 0);

    for (int i = 0; i < total_runs; ++i) {
        char filename[32];
        std::snprintf(filename, sizeof(filename), "run-%02d.f32", i);
        CHECK(destination.write_raw(filename, output[i]) == 0);
    }
    CHECK(destination.write_raw("input-before.f32", input_before) == 0);
    std::puts("GPU_STATE_REUSE_RAW_CAPTURE_COMPLETE runs=12 values=327168 validation=pending");
    std::fflush(stdout);

    double maximum[total_runs] = {};
    for (int o = 0; o < channels; ++o) {
        for (int i = 1; i < total_runs; ++i)
            CHECK(std::memcmp(output[0].row<const float>(o), output[i].row<const float>(o), width * sizeof(float)) == 0);
        for (int x = 0; x < width; ++x) {
            double expected = model[1][o];
            for (int c = 0; c < channels; ++c)
                for (int k = 0; k < kernel; ++k) {
                    const int source_x = x + k - 5;
                    const double value = source_x >= 0 && source_x < width ? input_before.row<const float>(c)[source_x] : 0.0;
                    expected += value * model[0][(o * channels + c) * kernel + k];
                }
            for (int v = 0; v < total_runs; ++v) {
                const double actual = output[v].row<const float>(o)[x];
                const double error = std::fabs(actual - expected);
                CHECK(std::isfinite(actual) && error <= atol + rtol * std::fabs(expected));
                if (error > maximum[v]) maximum[v] = error;
            }
        }
    }
    ncnn::Mat input_after;
    {
        ncnn::VkCompute readback(device);
        ncnn::Option download = opt;
        download.use_packing_layout = false;
        readback.record_download(gpu_input, input_after, download);
        CHECK(readback.submit_and_wait() == 0);
    }
    CHECK(!input_after.empty() && input_after.dims == 2 && input_after.w == width && input_after.h == channels);
    CHECK(input_after.d == 1 && input_after.c == 1 && input_after.elemsize == 4u && input_after.elempack == 1);
    CHECK(destination.write_raw("input-after.f32", input_after) == 0);
    for (int c = 0; c < channels; ++c) {
        CHECK(std::memcmp(input_before.row<const float>(c), input.row<const float>(c), width * sizeof(float)) == 0);
        CHECK(std::memcmp(input_before.row<const float>(c), input_after.row<const float>(c), width * sizeof(float)) == 0);
    }
    double measured_total[2] = {};
    int measured_count[2] = {};
    for (int i = 0; i < total_runs; ++i) {
        std::printf("GPU_STATE_REUSE_VALIDATED index=%d phase=%s state_reuse=%d outputs=%d bit_identical_to_run0=1 max_abs=%.9g\n", i, i < warmup_runs ? "warmup" : "measured", schedule[i], output_values, maximum[i]);
        if (i >= warmup_runs) {
            measured_total[schedule[i]] += elapsed[i];
            ++measured_count[schedule[i]];
        }
    }
    for (int mode = 0; mode < 2; ++mode) {
        CHECK(measured_count[mode] == 4);
        std::printf("GPU_STATE_REUSE_MEASURED_SUMMARY state_reuse=%d runs=%d construct_submit_wait_download_total_seconds=%.9f mean_seconds=%.9f\n", mode, measured_count[mode], measured_total[mode], measured_total[mode] / measured_count[mode]);
    }
    CHECK(destination.complete(elapsed, maximum) == 0);
    std::printf("PASS: GPU state reuse balanced probe warmup_runs=4 measured_runs=8 outputs_per_run=%d all_outputs=%d; every run bit-identical to OFF run0 and passes double reference atol=%g rtol=%g; all input bytes preserved; timestamp/profile qualification is separate\n", output_values, total_runs * output_values, atol, rtol);
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc != 2 || !argv[1][0]) {
        std::fprintf(stderr, "Usage: %s NEW_ABSOLUTE_OUTPUT_DIR | --compile-only\n", argv[0]);
        return 2;
    }
    const bool compile_only = std::strcmp(argv[1], "--compile-only") == 0;
    if (!compile_only && (argv[1][0] != '/' || argv[1][1] == '\0')) {
        std::fputs("output directory must be a new absolute path\n", stderr);
        return 2;
    }
    const char* keys[] = {"RADISH_NCNN_TILED", "RADISH_NCNN_TILED32", "RADISH_NCNN_PLANAR", "RADISH_NCNN_PAD_CHANNELS", "RADISH_NCNN_SHARED32", "RADISH_NCNN_PIPELINE_CACHE", "RADISH_TILED_GROUPS_X", "RADISH_BATCH_CHUNKS", "RADISH_TILED_BATCH_CHUNKS", "RADISH_SHARED_GROUPS", "RADISH_PROFILE"};
    const char* values[] = {"1", "0", "1", "0", "0", "1", "1", "8", "8", "8", "1"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const char* value = std::getenv(keys[i]);
        if (!value || std::strcmp(value, values[i])) {
            std::fprintf(stderr, "FAIL: require %s=%s\n", keys[i], values[i]);
            return 2;
        }
    }
    DiagnosticEnvironment environment;
    CHECK(setenv("RADISH_TILED_STATE_REUSE", "0", 1) == 0);
    CHECK(setenv("RADISH_GPU_ELAPSED", "0", 1) == 0);
    OutputDirectory destination;
    if (!compile_only) CHECK(destination.create(argv[1]) == 0);
    CHECK(ncnn::create_gpu_instance() == 0);
    InstanceLifetime instance;
    CHECK(ncnn::get_gpu_count() == 1);
    const ncnn::VulkanDevice* device = ncnn::get_gpu_device(0);
    CHECK(device && device->is_valid());
    CHECK(device->info.vendor_id() == 0x5143 && device->info.device_id() == 506);
    CHECK(std::strcmp(device->info.device_name(), "Radish Adreno 506 (experimental compute)") == 0);
    ncnn::Option opt;
    opt.num_threads = 1;
    opt.vulkan_device_index = 0;
    opt.use_vulkan_compute = opt.use_packing_layout = opt.use_sgemm_convolution = true;
    opt.use_fp16_packed = opt.use_fp16_storage = opt.use_fp16_arithmetic = opt.use_fp16_uniform = false;
    opt.use_bf16_packed = opt.use_bf16_storage = false;
    opt.use_int8_inference = opt.use_int8_packed = opt.use_int8_storage = opt.use_int8_arithmetic = opt.use_int8_uniform = false;
    opt.use_int16_packed = opt.use_int16_storage = false;
    opt.use_subgroup_ops = opt.use_shader_local_memory = opt.use_cooperative_matrix = opt.use_tensor_storage = false;
    opt.use_weights_in_host_memory = false;
    return probe(compile_only, destination, device, opt);
}
