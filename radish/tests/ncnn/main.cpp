#include "gpu.h"
#include "layer.h"
#include "command.h"
#include "pipeline.h"
#include "layer_shader_type.h"
#include "layer/vulkan/reduction_vulkan.h"
#include "layer/vulkan/relu_vulkan.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)

int main(int argc, char **argv)
{
    bool init=false,reduction=false,compile_only=false,reduction_smoke=false,fp16_packed=false;
    for(int i=1;i<argc;i++) {
        if(!std::strcmp(argv[i],"--init")) init=true;
        else if(!std::strcmp(argv[i],"--reduce")) reduction=true;
        else if(!std::strcmp(argv[i],"--reduce-compile")) reduction=compile_only=true;
        else if(!std::strcmp(argv[i],"--reduce-smoke")) reduction=reduction_smoke=true;
        else if(!std::strcmp(argv[i],"--compile-only")) compile_only=true;
        else if(!std::strcmp(argv[i],"--fp16-packed")) fp16_packed=true;
        else {
            std::fprintf(stderr,"Usage: %s [--init | --reduce | --reduce-compile | --reduce-smoke] [--compile-only] [--fp16-packed]\n",argv[0]);
            return 1;
        }
    }
    CHECK(!init || (!reduction && !compile_only && !fp16_packed));
    CHECK(ncnn::create_gpu_instance() == 0);
    CHECK(ncnn::get_gpu_count() == 1);
    const ncnn::VulkanDevice *device=ncnn::get_gpu_device(0);
    CHECK(device && device->is_valid());
    std::printf("ncnn device: %s\n",device->info.device_name());
    if (init) {
        ncnn::destroy_gpu_instance();
        std::puts("PASS: ncnn Vulkan device initialization");
        return 0;
    }
    CHECK(device->info.subgroup_size()>0);
    // Inputs are exactly representable in binary16. FP32 mean accumulation keeps
    // its existing allowance; a packed result adds one binary16 ulp, or the
    // smallest normal half when a subnormal conversion flushes to zero.
    const double mean_atol=0.00001+(fp16_packed?1.0/16384.0:0.0);
    const double mean_rtol=0.000002+(fp16_packed?1.0/1024.0:0.0);
    std::printf("storage: %s; arithmetic: FP32; mean atol=%.12g rtol=%.12g; ReLU comparison exact\n",
                fp16_packed?"FP16 packed":"FP32",mean_atol,mean_rtol);
    {
        ncnn::Option opt;
        opt.use_vulkan_compute=true;
        opt.use_fp16_packed=fp16_packed;
        opt.use_fp16_storage=false;
        opt.use_fp16_arithmetic=false;
        opt.use_fp16_uniform=false;
        opt.use_bf16_packed=false;
        opt.use_bf16_storage=false;
        opt.use_int8_inference=false;
        opt.use_int8_packed=false;
        opt.use_int8_storage=false;
        opt.use_int8_arithmetic=false;
        opt.use_int8_uniform=false;
        opt.use_int16_packed=false;
        opt.use_int16_storage=false;
        opt.use_subgroup_ops=false;
        opt.use_cooperative_matrix=false;
        opt.use_shader_local_memory=false;
        opt.use_tensor_storage=false;
        opt.use_weights_in_host_memory=false;
        if(reduction) opt.use_packing_layout=false;
        opt.blob_vkallocator=device->acquire_blob_allocator();
        opt.workspace_vkallocator=opt.blob_vkallocator;
        opt.staging_vkallocator=device->acquire_staging_allocator();
        ncnn::Layer *relu=ncnn::create_layer_vulkan(reduction?"Reduction":"ReLU");
        CHECK(relu && relu->support_vulkan);
        relu->vkdev=device;
        ncnn::ParamDict params;
        if(reduction) {
            params.set(0,3);
            params.set(1,0);
            ncnn::Mat axes(1);
            ((int*)axes.data)[0]=1;
            params.set(3,axes);
            params.set(4,1);
            params.set(5,1);
        } else params.set(0,0.125f);
        CHECK(relu->load_param(params)==0);
        const char *local_override=std::getenv("RADISH_NCNN_LOCAL_SIZE");
        if (reduction && local_override) {
            CHECK(!std::strcmp(local_override,"32") || !std::strcmp(local_override,"64"));
            unsigned local_size=(unsigned)std::atoi(local_override);
            unsigned logical_size=local_size;
            if (const char *stride=std::getenv("RADISH_NCNN_REDUCTION_STRIDE")) {
                CHECK(local_size==64 && !std::strcmp(stride,"32"));
                logical_size=32;
            }
            auto *reduction_layer=static_cast<ncnn::Reduction_vulkan*>(relu);
            reduction_layer->pipeline_reduction=new ncnn::Pipeline(device);
            reduction_layer->pipeline_reduction->set_local_size_xyz(local_size,1,1);
            std::vector<ncnn::vk_specialization_type> constants(2);
            constants[0].i=3;
            constants[1].i=logical_size;
            std::printf("reduction logical stride: %u\n",logical_size);
            CHECK(reduction_layer->pipeline_reduction->create(ncnn::LayerShaderType::reduction,opt,constants)==0);
        } else CHECK(relu->create_pipeline(opt)==0);
        {
            const ncnn::Pipeline *pipeline=reduction?static_cast<ncnn::Reduction_vulkan*>(relu)->pipeline_reduction
                                                    :static_cast<ncnn::ReLU_vulkan*>(relu)->pipeline_relu;
            CHECK(pipeline && pipeline->pipeline()!=VK_NULL_HANDLE);
            std::printf("%s local size: %u x %u x %u\n",reduction?"reduction":"ReLU",pipeline->local_size_x(),pipeline->local_size_y(),pipeline->local_size_z());
        }
        if(compile_only) {
            std::printf("PASS: ncnn %s pipeline compilation, no operator submission\n",reduction?"reduction":"ReLU");
        } else if(reduction) {
            for(int width : {1,31,32,33,213,4099}) for(int channels : {3,4,7}) {
                if (reduction_smoke && (width!=1 || channels!=3)) continue;
                ncnn::Mat input(width,channels);
                for(int c=0;c<channels;c++) for(int i=0;i<width;i++)
                    input.row<float>(c)[i]=(float)((i*7+c*11)%101-50)*0.125f;
                ncnn::VkMat uploaded,gpu,gpu_output;
                ncnn::Mat output;
                ncnn::VkCompute command(device);
                command.record_upload(input,uploaded,opt);
                device->convert_packing(uploaded,gpu,1,command,opt);
                CHECK(gpu.w==width && gpu.h==channels && gpu.elempack==1);
                CHECK(gpu.elemsize==(fp16_packed?2u:4u));
                const auto before_forward=command.pending_dispatch_total();
                CHECK(relu->forward(gpu,gpu_output,command,opt)==0);
                CHECK(command.pending_dispatch_total()>before_forward);
                CHECK(gpu_output.elempack==1 && gpu_output.elemsize==(fp16_packed?2u:4u));
                command.record_download(gpu_output,output,opt);
                CHECK(command.pending_dispatch_total()>0);
                CHECK(command.submit_and_wait()==0);
                CHECK(output.w==1 && output.h==channels && output.elempack==1 && output.elemsize==4u);
                double max_error=0;
                for(int c=0;c<channels;c++) {
                    double sum=0;
                    for(int i=0;i<width;i++) sum+=input.row<float>(c)[i];
                    double expected=sum/width;
                    double value=output.row<float>(c)[0];
                    double error=std::fabs(value-expected),limit=mean_atol+mean_rtol*std::fabs(expected);
                    if(error>max_error) max_error=error;
                    if(!std::isfinite(value) || error>limit) {
                        std::fprintf(stderr,"FAIL mean[%d]: got %.9g expected %.17g abs error %.9g limit %.9g\n",c,value,expected,error,limit);
                        return 1;
                    }
                }
                std::printf("PASS: ncnn Vulkan mean %d x %d, all outputs, max abs error %.9g\n",channels,width,max_error);
            }
        } else for (int n : {128,508,16396}) {
            ncnn::Mat input(n);
            for (int i=0;i<n;i++) ((float*)input.data)[i]=(float)((i%101)-50)*0.25f;
            ncnn::VkMat gpu;
            ncnn::Mat output;
            ncnn::VkCompute command(device);
            command.record_upload(input,gpu,opt);
            CHECK(gpu.elemsize==static_cast<size_t>(gpu.elempack)*(fp16_packed?2u:4u));
            const auto before_forward=command.pending_dispatch_total();
            CHECK(relu->forward_inplace(gpu,command,opt)==0);
            CHECK(command.pending_dispatch_total()>before_forward);
            command.record_download(gpu,output,opt);
            CHECK(command.pending_dispatch_total()>0);
            CHECK(command.submit_and_wait()==0);
            CHECK(output.total()*output.elempack==(size_t)n);
            CHECK(output.elemsize==static_cast<size_t>(4*output.elempack));
            for (int i=0;i<n;i++) {
                float x=((float*)input.data)[i];
                float expected=x<0?x*0.125f:x;
                CHECK(((float*)output.data)[i]==expected);
            }
            std::printf("PASS: ncnn Vulkan ReLU %d elements, all outputs\n",n);
        }
        CHECK(relu->destroy_pipeline(opt)==0);
        delete relu;
        device->reclaim_blob_allocator(opt.blob_vkallocator);
        device->reclaim_staging_allocator(opt.staging_vkallocator);
    }
    ncnn::destroy_gpu_instance();
    return 0;
}
