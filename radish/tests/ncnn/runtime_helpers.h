#ifndef RADISH_NCNN_RUNTIME_HELPERS_H
#define RADISH_NCNN_RUNTIME_HELPERS_H
#include "net.h"
#include "layer/innerproduct.h"
#if NCNN_VULKAN
#include "command.h"
#include "gpu.h"
#endif
#include <cstdio>
#include <string>
#include <vector>
#define RADISH_NCNN_CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
namespace radish_ncnn {
#if NCNN_VULKAN
struct GpuInstance {
    bool active = false;
    ~GpuInstance() { if(active) ncnn::destroy_gpu_instance(); }
};
struct Allocators {
    const ncnn::VulkanDevice* device;
    ncnn::Option opt;
    Allocators(const ncnn::VulkanDevice* d,const ncnn::Option& o):device(d),opt(o) {
        opt.blob_vkallocator=device->acquire_blob_allocator();
        opt.workspace_vkallocator=opt.blob_vkallocator;
        opt.staging_vkallocator=device->acquire_staging_allocator();
    }
    ~Allocators() {
        if(opt.blob_vkallocator) device->reclaim_blob_allocator(opt.blob_vkallocator);
        if(opt.staging_vkallocator) device->reclaim_staging_allocator(opt.staging_vkallocator);
    }
};
#endif
inline void configure_options(ncnn::Option& opt,bool vulkan,bool cpu_packed,bool fp16_packed,int threads) {
    opt.num_threads=threads;
    opt.lightmode=true;
    opt.use_vulkan_compute=vulkan;
    opt.use_packing_layout=vulkan || cpu_packed;
    opt.use_sgemm_convolution=true;
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
    opt.use_shader_local_memory=false;
    opt.use_cooperative_matrix=false;
    opt.use_tensor_storage=false;
    opt.use_weights_in_host_memory=false;
}
inline ncnn::Mat vector_shape(int count,bool packed,const ncnn::Option& opt,int featmask=0) {
    int pack=packed && count%4==0?4:1;
    // Match Net::load_param: Vulkan packed hints use two bytes per scalar,
    // including pack1; logical blob and CPU-layer hints remain FP32.
    bool fp16=(opt.use_fp16_packed || opt.use_fp16_storage) && !(featmask & (1<<1));
    bool bf16=(opt.use_bf16_packed || opt.use_bf16_storage) && !(featmask & (1<<2));
    size_t scalar_size=packed && (fp16 || bf16)?2u:4u;
    return ncnn::Mat(count/pack,static_cast<void*>(0),scalar_size*pack,pack);
}
inline int set_shape_hints(ncnn::Net& net,bool style,bool vulkan) {
    int inputs=0,crops=0,projections=0;
    for(ncnn::Layer* layer:net.mutable_layers()) {
        RADISH_NCNN_CHECK(layer);
        const bool packed=vulkan && !(layer->featmask & (1<<4)) && layer->support_vulkan && layer->support_vulkan_packing;
        if(style) {
            RADISH_NCNN_CHECK(layer->type=="Input" || layer->type=="Crop" || layer->type=="Split" || layer->type=="InnerProduct");
            if(vulkan) RADISH_NCNN_CHECK(layer->support_vulkan);
            int count=128;
            if(layer->type=="Input") { RADISH_NCNN_CHECK(layer->name=="in3"); count=256; ++inputs; }
            if(layer->type=="Crop") ++crops;
            if(layer->type=="InnerProduct") count=1028;
            for(int top:layer->tops) net.mutable_blobs()[top].shape=vector_shape(count,false,net.opt);
            layer->bottom_shapes.resize(layer->bottoms.size());
            for(size_t i=0;i<layer->bottoms.size();++i) {
                const ncnn::Mat& shape=net.blobs()[layer->bottoms[i]].shape;
                RADISH_NCNN_CHECK(shape.dims==1 && shape.w>0);
                layer->bottom_shapes[i]=vector_shape(shape.w,packed,net.opt,layer->featmask);
            }
            layer->top_shapes.resize(layer->tops.size());
            for(size_t i=0;i<layer->tops.size();++i)
                layer->top_shapes[i]=vector_shape(count,packed,net.opt,layer->featmask);
        }
        if(layer->type=="InnerProduct") {
            const ncnn::InnerProduct* inner=static_cast<const ncnn::InnerProduct*>(layer);
            // All 58 projections in this fixed core consume a 128-value style
            // vector. Known 1D shapes avoid unused batched GEMM compilation.
            RADISH_NCNN_CHECK(inner->num_output>0 && inner->weight_data_size==128*inner->num_output);
            RADISH_NCNN_CHECK(layer->bottoms.size()==1 && layer->tops.size()==1);
            if(style) RADISH_NCNN_CHECK(inner->num_output==1028);
            layer->bottom_shapes.assign(1,vector_shape(128,packed,net.opt,layer->featmask));
            layer->top_shapes.assign(1,vector_shape(inner->num_output,packed,net.opt,layer->featmask));
            ++projections;
        }
    }
    if(style) RADISH_NCNN_CHECK(inputs==1 && crops==1 && projections==1);
    else RADISH_NCNN_CHECK(projections==58);
    std::printf("shape hints: %d fixed 128-value style projections\n",projections);
    return 0;
}

#if NCNN_VULKAN
inline int extract_gpu(ncnn::Net& net,const std::vector<ncnn::Mat>& inputs,const std::vector<std::string>& names,
                const char* target,ncnn::Mat& output) {
    const ncnn::VulkanDevice* device=net.vulkan_device();
    RADISH_NCNN_CHECK(device && device->is_valid() && net.opt.use_vulkan_compute);
    Allocators allocators(device,net.opt);
    RADISH_NCNN_CHECK(allocators.opt.blob_vkallocator && allocators.opt.staging_vkallocator);
    // Extractor and VkMats must die before their allocators are reclaimed.
    ncnn::Extractor ex=net.create_extractor();
    ex.set_blob_vkallocator(allocators.opt.blob_vkallocator);
    ex.set_workspace_vkallocator(allocators.opt.workspace_vkallocator);
    ex.set_staging_vkallocator(allocators.opt.staging_vkallocator);
    ncnn::VkCompute command(device);
    std::vector<ncnn::VkMat> uploaded(inputs.size());
    for(size_t i=0;i<inputs.size();++i) {
        command.record_upload(inputs[i],uploaded[i],allocators.opt);
        RADISH_NCNN_CHECK(!uploaded[i].empty());
        RADISH_NCNN_CHECK(uploaded[i].elemsize==static_cast<size_t>(uploaded[i].elempack)*(net.opt.use_fp16_packed?2u:4u));
        RADISH_NCNN_CHECK(ex.input(names[i].c_str(),uploaded[i])==0);
    }
    RADISH_NCNN_CHECK(command.submit_and_wait()==0);
    RADISH_NCNN_CHECK(command.reset()==0);
    const auto before_forward=command.pending_dispatch_total();
    ncnn::VkMat result;
    RADISH_NCNN_CHECK(ex.extract(target,result,command)==0);
    // Net may submit/reset at CPU boundaries. This remaining count is not a
    // cumulative GPU-work claim; the opt-in trace records every actual path.
    RADISH_NCNN_CHECK(command.pending_dispatch_total()>before_forward);
    RADISH_NCNN_CHECK(!result.empty());
    std::printf("Vulkan extraction: %llu pending dispatch groups before scalar download; RADISH_NCNN_TRACE=1 records actual layer paths\n",
                static_cast<unsigned long long>(command.pending_dispatch_total()));
    ncnn::Option download_opt=allocators.opt;
    download_opt.use_packing_layout=false;
    command.record_download(result,output,download_opt);
    RADISH_NCNN_CHECK(command.submit_and_wait()==0);
    RADISH_NCNN_CHECK(!output.empty() && output.elempack==1 && output.elemsize==4u);
    return 0;
}
#endif
} // namespace radish_ncnn
#undef RADISH_NCNN_CHECK
#endif
