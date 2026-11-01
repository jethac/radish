#include "allocator.h"
#include "command.h"
#include "gpu.h"
#include "pipeline.h"
#include "layer.h"
#include "modelbin.h"
#include "layer/vulkan/convolution1d_vulkan.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
const int runs=12, warmups=4;
const int schedule[runs]={0,1,1,0,0,1,1,0,0,1,1,0};
const int width=213, channels=128, kernel=11, reduction=1408, output_vectors=6816;
const int weight_scalars=channels*reduction, input_scalars=channels*268;
const char* kinds[2]={"baseline","packed-a"};
const char* raw_model_names[2]={"baseline","packed"};
struct InstanceLifetime { ~InstanceLifetime() { ncnn::destroy_gpu_instance(); } };
struct Environment {
    const char* keys[3]={"RADISH_GPU_ELAPSED","RADISH_GPU_ELAPSED_CALIBRATION","RADISH_NCNN_PLANAR_PACKED_A"};
    bool present[3]; std::string previous[3];
    Environment() { for(int i=0;i<3;++i) { const char* v=std::getenv(keys[i]);present[i]=v!=0;previous[i]=v?v:""; } }
    ~Environment() { for(int i=0;i<3;++i) { if(present[i]) setenv(keys[i],previous[i].c_str(),1);else unsetenv(keys[i]); } }
};
struct Allocators {
    const ncnn::VulkanDevice* device; ncnn::Option& opt;
    Allocators(const ncnn::VulkanDevice* d,ncnn::Option& o):device(d),opt(o) {}
    void acquire() {
        opt.blob_vkallocator=device->acquire_blob_allocator();opt.workspace_vkallocator=opt.blob_vkallocator;
        opt.staging_vkallocator=device->acquire_staging_allocator();
    }
    ~Allocators() {
        if(opt.blob_vkallocator) device->reclaim_blob_allocator(opt.blob_vkallocator);
        if(opt.staging_vkallocator) device->reclaim_staging_allocator(opt.staging_vkallocator);
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

uint32_t bits(float value) { uint32_t x; std::memcpy(&x,&value,4); return x; }
void put(ncnn::Mat& m,size_t i,uint32_t value) { std::memcpy(static_cast<unsigned char*>(m.data)+4*i,&value,4); }
uint32_t get(const ncnn::Mat& m,size_t i) { uint32_t x; std::memcpy(&x,static_cast<const unsigned char*>(m.data)+4*i,4); return x; }
int a_integer(int c,int k) { return ((13*c+7*(k%32)+3)%31)-15; }
int b_integer(int k,int x) { return ((11*k+5*x+1)%29)-14; }
int bias_integer(int c) { return ((17*c+9)%255)-127; }
int baseline_b_integer(int ci,int xpad) { return xpad>=5 && xpad<218 ? b_integer(ci,xpad-5) : 0; }
uint32_t guard(int tag,int lane) { return 0x3e800000u | static_cast<uint32_t>(tag*131+lane*17+3); }
size_t bytes(int vectors) { return static_cast<size_t>(vectors)*16u; }

bool vec4_host_shape(const ncnn::Mat& m,int vectors) {
    return !m.empty() && m.dims==1 && m.w==vectors && m.h==1 && m.d==1 && m.c==1 && m.n==1
        && m.elempack==4 && m.elemsize==16u && m.total()==static_cast<size_t>(vectors);
}
bool vec4_allocation(const ncnn::VkMat& m,int vectors,const ncnn::VulkanDevice* device) {
    if(m.empty() || m.dims!=1 || m.w!=vectors || m.h!=1 || m.d!=1 || m.c!=1 || m.n!=1
       || m.elempack!=4 || m.elemsize!=16u || m.total()!=static_cast<size_t>(vectors)
       || !m.data || !m.refcount || !m.allocator || m.allocator->vkdev!=device || !m.data->buffer || !m.data->memory) return false;
    const size_t count=bytes(vectors), align=device->info.buffer_offset_alignment();
    if(m.offset>m.data->capacity || count>m.data->capacity-m.offset || m.data->offset>std::numeric_limits<size_t>::max()-m.data->capacity) return false;
    return align && m.buffer_offset()%align==0 && m.buffer_offset()%16==0 && m.buffer_capacity()==count
        && count<=device->info.physicalDeviceProperties().limits.maxStorageBufferRange;
}
bool disjoint(const ncnn::VkMat& a,const ncnn::VkMat& b) {
    if(a.buffer()!=b.buffer()) return true;
    const size_t x=a.buffer_offset(),y=b.buffer_offset();
    return x<=y ? a.buffer_capacity()<=y-x : b.buffer_capacity()<=x-y;
}

bool scalar_host_shape(const ncnn::Mat& m,int count) {
    return !m.empty() && m.dims==1 && m.w==count && m.h==1 && m.d==1 && m.c==1 && m.n==1
        && m.elempack==1 && m.elemsize==4u && m.total()==static_cast<size_t>(count)
        && m.cstep==static_cast<size_t>(count);
}
bool scalar_allocation(const ncnn::VkMat& m,int count,const ncnn::VulkanDevice* device) {
    if(m.empty() || m.dims!=1 || m.w!=count || m.h!=1 || m.d!=1 || m.c!=1 || m.n!=1
       || m.elempack!=1 || m.elemsize!=4u || m.total()!=static_cast<size_t>(count)
       || m.cstep!=static_cast<size_t>(count) || !m.data || !m.refcount || !m.allocator
       || m.allocator->vkdev!=device || !m.data->buffer || !m.data->memory) return false;
    const size_t count_bytes=static_cast<size_t>(count)*4u,align=device->info.buffer_offset_alignment();
    if(m.offset>m.data->capacity || count_bytes>m.data->capacity-m.offset
       || m.data->offset>std::numeric_limits<size_t>::max()-m.data->capacity) return false;
    return align && m.buffer_offset()%align==0 && m.buffer_offset()%16==0
        && m.buffer_capacity()==count_bytes
        && count_bytes<=device->info.physicalDeviceProperties().limits.maxStorageBufferRange;
}
struct OutputDirectory {
    int fd=-1;
    ~OutputDirectory() { if(fd>=0) close(fd); }
    FILE* open_file(const char* name) const {
        int out=openat(fd,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(out<0) return 0;
        FILE* f=fdopen(out,"wb");if(!f) close(out);return f;
    }
    int create(const char* path) {
        CHECK(mkdir(path,0700)==0);
        fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);CHECK(fd>=0);
        FILE* f=open_file("pending.json");CHECK(f);
        const int wrote=std::fputs("{\"format\":\"radish-gpu-packed-a-v1\",\"status\":\"pending\"}\n",f);
        const int closed=std::fclose(f);CHECK(wrote>=0 && closed==0);return 0;
    }
    int raw(const char* name,const ncnn::Mat& value,int vectors) const {
        CHECK(vec4_host_shape(value,vectors));FILE* f=open_file(name);CHECK(f);
        const bool wrote=std::fwrite(value.data,1,bytes(vectors),f)==bytes(vectors);
        const int closed=std::fclose(f);CHECK(wrote && closed==0);return 0;
    }
    int raw_scalar(const char* name,const ncnn::Mat& value,int count) const {
        CHECK(scalar_host_shape(value,count));FILE* f=open_file(name);CHECK(f);
        const size_t count_bytes=static_cast<size_t>(count)*4u;
        const bool wrote=std::fwrite(value.data,1,count_bytes,f)==count_bytes;
        const int closed=std::fclose(f);CHECK(wrote && closed==0);return 0;
    }
    int complete(const double times[runs],const double creation[2]) const {
        FILE* f=open_file("result.json.tmp");CHECK(f);
        std::fprintf(f,"{\n\"format\":\"radish-gpu-packed-a-v1\",\n\"status\":\"passed\",\n\"warmup_runs\":4,\"measured_runs\":8,\"total_runs\":12,\"expected_samples\":12,\n\"width\":213,\"channels\":128,\"reduction\":1408,\"pushes\":[268,213],\"grid\":[4,2,1],\"local\":[16,8,1],\n\"raw_files\":22,\"raw_bytes\":4468960,\"total_validated_values\":327168,\n\"all_outputs_exact\":true,\"all_output_payloads_bit_identical\":true,\"output_guard_words\":48,\"input_guard_words\":4,\"all_guards_unchanged\":true,\"all_model_and_input_bytes_read_back_unchanged\":true,\n\"production_model_tail_guards\":false,\"raw_before_reference\":true,\"distinct_gpu_destinations\":true,\n\"native_admission\":\"pending independent exact resources/native/timer/chunk/submit qualification; no assumed native length\",\n\"timing_scope\":\"record_pipeline, submit and wait only; all GPU reads, CPU oracle and file writes follow all twelve waits. Uploads and create_pipeline excluded.\",\n\"create_pipeline_scope\":\"wall intervals include packing and shader/pipeline creation; not isolated packing time\",\n\"create_pipeline_seconds\":[%.17g,%.17g],\n\"runs\":[\n",creation[0],creation[1]);
        for(int i=0;i<runs;++i)
            std::fprintf(f,"{\"kind\":\"%s\",\"index\":%d,\"phase\":\"%s\",\"output\":\"run-%02d.f32\",\"bytes\":109072,\"record_submit_wait_seconds\":%.17g}%s\n",kinds[schedule[i]],i,i<warmups?"warmup":"measured",i,times[i],i==runs-1?"":",");
        std::fputs("]\n}\n",f);const bool okay=std::ferror(f)==0;const int closed=std::fclose(f);CHECK(okay && closed==0);
        CHECK(unlinkat(fd,"pending.json",0)==0);
        CHECK(linkat(fd,"result.json.tmp",fd,"result.json",0)==0);
        // Publication is the last fallible step; temporary-link cleanup does not revoke success.
        (void)unlinkat(fd,"result.json.tmp",0);return 0;
    }
};
int upload(const ncnn::Mat& host,ncnn::VkMat& gpu,int vectors,const ncnn::VulkanDevice* device,const ncnn::Option& opt) {
    CHECK(vec4_host_shape(host,vectors));
    gpu.create(vectors,static_cast<size_t>(16),4,opt.blob_vkallocator);
    CHECK(vec4_allocation(gpu,vectors,device) && gpu.allocator->mappable && gpu.mapped_ptr());
    ncnn::VkTransfer command(device);
    command.record_upload(host,gpu,opt,false); CHECK(command.pending_upload_total()==bytes(vectors));
    CHECK(command.submit_and_wait()==0); return 0;
}
int readback(const ncnn::VkMat& gpu,ncnn::Mat& host,int vectors,const ncnn::VulkanDevice* device,const ncnn::Option& opt) {
    CHECK(vec4_allocation(gpu,vectors,device) && gpu.allocator->mappable && gpu.mapped_ptr());
    ncnn::VkCompute command(device); command.record_clone(gpu,host,opt);
    CHECK(command.pending_dispatch_total()==0);
    CHECK(command.submit_and_wait()==0 && vec4_host_shape(host,vectors)); return 0;
}
int readback_scalar(const ncnn::VkMat& gpu,ncnn::Mat& host,int count,const ncnn::VulkanDevice* device,const ncnn::Option& opt) {
    CHECK(scalar_allocation(gpu,count,device));
    ncnn::VkCompute command(device);command.record_clone(gpu,host,opt);
    CHECK(command.pending_dispatch_total()==0);
    CHECK(command.submit_and_wait()==0 && scalar_host_shape(host,count));return 0;
}
int run(int index,ncnn::Pipeline& pipeline,const std::vector<ncnn::VkMat>& bindings,
        double& seconds,const ncnn::VulkanDevice* device) {
    const bool packed=schedule[index]!=0;
    if(packed) CHECK(setenv("RADISH_GPU_ELAPSED_CALIBRATION","packed-a",1)==0);
    else CHECK(unsetenv("RADISH_GPU_ELAPSED_CALIBRATION")==0);
    CHECK(setenv("RADISH_GPU_ELAPSED","1",1)==0);
    std::printf("GPU_PACKED_A_RUN_BEGIN kind=%s index=%d phase=%s push0=%d outw=213 updates_per_accumulator=1408 grid=4,2,1 local=16,8,1 groups=8 actual_chunks=pending actual_submits=pending native_admission=pending\n",kinds[packed?1:0],index,index<warmups?"warmup":"measured",268);std::fflush(stdout);
    ncnn::VkCompute command(device);std::vector<ncnn::vk_constant_type> constants(2);
    constants[0].i=268;constants[1].i=width;
    ncnn::VkMat dispatcher;dispatcher.w=64;dispatcher.h=16;dispatcher.c=1;
    const auto begin=std::chrono::steady_clock::now();const auto before=command.pending_dispatch_total();
    command.record_pipeline(&pipeline,bindings,constants,dispatcher);
    CHECK(command.pending_dispatch_total()-before==8);
    CHECK(command.submit_and_wait()==0);
    seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    CHECK(std::isfinite(seconds) && seconds>0);
    CHECK(setenv("RADISH_GPU_ELAPSED","0",1)==0);
    std::printf("GPU_PACKED_A_RUN_END kind=%s index=%d phase=%s status=SUBMITTED record_submit_wait_seconds=%.9f\n",kinds[packed?1:0],index,index<warmups?"warmup":"measured",seconds);std::fflush(stdout);return 0;
}
int check_math(const ncnn::Pipeline& pipeline)
{
    CHECK(pipeline.pipeline() != VK_NULL_HANDLE);
    CHECK(pipeline.local_size_x() == 16 && pipeline.local_size_y() == 8 && pipeline.local_size_z() == 1);
    CHECK(pipeline.shader_info().specialization_count == 5);
    CHECK(pipeline.shader_info().binding_count == 4 && pipeline.shader_info().push_constant_count == 2);
    for (int i = 0; i < 4; ++i) CHECK(pipeline.shader_info().binding_types[i] == 1);
    return 0;
}

int initialize_model(ncnn::Mat model[2]) {
    model[0].create(weight_scalars,static_cast<size_t>(4),1);model[1].create(channels,static_cast<size_t>(4),1);
    CHECK(scalar_host_shape(model[0],weight_scalars) && scalar_host_shape(model[1],channels));
    for(int co=0;co<channels;++co) {
        put(model[1],co,bits(static_cast<float>(bias_integer(co))/4096.f));
        for(int k=0;k<reduction;++k) put(model[0],co*reduction+k,bits(static_cast<float>(a_integer(co,k))/64.f));
    }
    return 0;
}
int original_unchanged(const ncnn::Mat model[2]) {
    for(int co=0;co<channels;++co) {
        CHECK(get(model[1],co)==bits(static_cast<float>(bias_integer(co))/4096.f));
        for(int k=0;k<reduction;++k) CHECK(get(model[0],co*reduction+k)==bits(static_cast<float>(a_integer(co,k))/64.f));
    }
    return 0;
}
int create_layer(int variant,LayerOwner& owner,const ncnn::Mat model[2],const ncnn::Option& opt,
                 ncnn::Mat& weight_before,ncnn::Mat& bias_before,double& seconds) {
    CHECK(owner.layer && owner.layer->support_vulkan);
    ncnn::ParamDict pd;pd.set(0,channels);pd.set(1,kernel);pd.set(2,1);pd.set(3,1);
    pd.set(4,5);pd.set(15,5);pd.set(5,1);pd.set(6,weight_scalars);pd.set(9,0);
    CHECK(owner.layer->load_param(pd)==0);
    CHECK(owner.layer->load_model(ncnn::ModelBinFromMatArray(model))==0);
    CHECK(setenv("RADISH_NCNN_PLANAR_PACKED_A",variant?"1":"0",1)==0);
    std::printf("GPU_PACKED_A_PIPELINE_BEGIN kind=%s selector=%d local=16,8,1 specializations=5 descriptors=4 pushes=2 native_length=unknown\n",kinds[variant],variant);std::fflush(stdout);
    const auto begin=std::chrono::steady_clock::now();
    CHECK(owner.layer->create_pipeline(opt)==0);
    seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    CHECK(std::isfinite(seconds) && seconds>0);
    ncnn::Convolution1D_vulkan& conv=*static_cast<ncnn::Convolution1D_vulkan*>(owner.layer);
    CHECK(conv.use_radish_tiled && conv.radish_planar && !conv.radish_tiled32 && !conv.radish_pad_channels);
    CHECK(!conv.use_cooperative_matrix && conv.pipeline_convolution1d_gemm);
    CHECK(check_math(*conv.pipeline_convolution1d_gemm)==0);
    CHECK(scalar_host_shape(conv.weight_data_packed,weight_scalars));
    if(variant) {
        CHECK(conv.weight_data_packed.refcount && conv.weight_data_packed.data!=model[0].data);
        for(int k=0;k<reduction;++k) for(int co=0;co<channels;++co)
            CHECK(get(conv.weight_data_packed,k*channels+co)==get(model[0],co*reduction+k));
    } else CHECK(std::memcmp(conv.weight_data_packed.data,model[0].data,static_cast<size_t>(weight_scalars)*4u)==0);
    CHECK(scalar_host_shape(conv.bias_data,channels));
    CHECK(std::memcmp(conv.bias_data.data,model[1].data,static_cast<size_t>(channels)*4u)==0);
    // upload_model releases both host members; preserve the actual payloads first.
    weight_before=conv.weight_data_packed.clone();bias_before=conv.bias_data.clone();
    CHECK(scalar_host_shape(weight_before,weight_scalars) && scalar_host_shape(bias_before,channels));
    CHECK(original_unchanged(model)==0);
    std::printf("GPU_PACKED_A_PIPELINE_END kind=%s status=HOST_LAYOUT_PASS elements=180224 create_pipeline_seconds=%.9f scope=packing_and_shader_pipeline_creation native_admission=pending\n",kinds[variant],seconds);std::fflush(stdout);
    return 0;
}
int initialize_input(ncnn::Mat& input) {
    input.create(input_scalars/4+1,static_cast<size_t>(16),4);CHECK(vec4_host_shape(input,input_scalars/4+1));
    for(int i=0;i<input_scalars;++i) put(input,i,bits(static_cast<float>(baseline_b_integer(i/268,i%268))/64.f));
    for(int lane=0;lane<4;++lane) put(input,input_scalars+lane,guard(1,lane));return 0;
}
int make_oracle(std::vector<uint32_t>& oracle) {
    oracle.resize(channels*width);
    for(int co=0;co<channels;++co) for(int x=0;x<width;++x) {
        int64_t numerator=bias_integer(co);
        for(int ci=0;ci<channels;++ci) for(int tap=0;tap<kernel;++tap) {
            numerator+=a_integer(co,ci*kernel+tap)*baseline_b_integer(ci,x+tap);
            CHECK(numerator>=-295807 && numerator<=295807);
        }
        const size_t packed=(static_cast<size_t>(co/4)*width+x)*4+co%4;
        oracle[packed]=bits(static_cast<float>(static_cast<double>(numerator)/4096.0));
    }
    return 0;
}
int probe(bool compile_only,OutputDirectory& destination,const ncnn::VulkanDevice* device,ncnn::Option& opt) {
    // Declare allocator ownership first so layer buffers die before the pool is reclaimed.
    Allocators pool(device,opt);
    ncnn::Mat model[2],weight_before[2],bias_before[2];CHECK(initialize_model(model)==0);
    LayerOwner baseline_owner(device,opt),packed_owner(device,opt);double creation[2]={};
    CHECK(create_layer(0,baseline_owner,model,opt,weight_before[0],bias_before[0],creation[0])==0);
    CHECK(create_layer(1,packed_owner,model,opt,weight_before[1],bias_before[1],creation[1])==0);
    ncnn::Convolution1D_vulkan* conv[2]={static_cast<ncnn::Convolution1D_vulkan*>(baseline_owner.layer),static_cast<ncnn::Convolution1D_vulkan*>(packed_owner.layer)};
    ncnn::Pipeline* pipeline[2]={conv[0]->pipeline_convolution1d_gemm,conv[1]->pipeline_convolution1d_gemm};
    CHECK(pipeline[0]->pipeline()!=pipeline[1]->pipeline() && pipeline[0]->shader_module()!=pipeline[1]->shader_module());
    if(compile_only) {
        std::puts("PASS: GPU packed-A compile-only; both real layer creations and complete host-layout checks passed; no probe Vulkan allocators, tensor uploads, VkCompute or CPU output reference (device initialization may submit; host model/snapshot arrays allocated)");return 0;
    }
    pool.acquire();CHECK(opt.blob_vkallocator && opt.staging_vkallocator);
    for(int v=0;v<2;++v) {
        ncnn::VkTransfer transfer(device);CHECK(conv[v]->upload_model(transfer,opt)==0);
        CHECK(transfer.pending_upload_total()==static_cast<size_t>(weight_scalars+channels)*4u);
        CHECK(transfer.submit_and_wait()==0);
        CHECK(scalar_allocation(conv[v]->weight_data_gpu,weight_scalars,device));
        CHECK(scalar_allocation(conv[v]->bias_data_gpu,channels,device));
        CHECK(conv[v]->weight_data_packed.empty() && conv[v]->bias_data.empty());
    }
    ncnn::Mat input; ncnn::VkMat gpu_input,outputs[runs];CHECK(initialize_input(input)==0);
    CHECK(upload(input,gpu_input,input_scalars/4+1,device,opt)==0);
    ncnn::Mat poison(output_vectors+1,static_cast<size_t>(16),4);CHECK(vec4_host_shape(poison,output_vectors+1));
    for(int i=0;i<runs;++i) {
        for(int j=0;j<output_vectors*4;++j) put(poison,j,0x7fc00100u+static_cast<unsigned>(i));
        for(int lane=0;lane<4;++lane) put(poison,output_vectors*4+lane,guard(10+i,lane));
        CHECK(upload(poison,outputs[i],output_vectors+1,device,opt)==0);
        std::printf("GPU_PACKED_A_OUTPUT_POISON index=%d payload_values=27264 guard_values=4 changing_nan=1 uploaded_before_sequence=1\n",i);
    }
    std::fflush(stdout);
    std::vector<ncnn::VkMat> all={conv[0]->weight_data_gpu,conv[0]->bias_data_gpu,conv[1]->weight_data_gpu,conv[1]->bias_data_gpu,gpu_input};
    for(int i=0;i<runs;++i) all.push_back(outputs[i]);CHECK(all.size()==17);
    for(size_t i=0;i<all.size();++i) for(size_t j=0;j<i;++j) CHECK(disjoint(all[i],all[j]));
    const VkPipeline handles[2]={pipeline[0]->pipeline(),pipeline[1]->pipeline()};
    ncnn::Mat output[runs];double times[runs]={};
    for(int i=0;i<runs;++i) {
        for(int v=0;v<2;++v) {
            CHECK(pipeline[v]==conv[v]->pipeline_convolution1d_gemm && pipeline[v]->pipeline()==handles[v]);
            CHECK(scalar_allocation(conv[v]->weight_data_gpu,weight_scalars,device));
            CHECK(scalar_allocation(conv[v]->bias_data_gpu,channels,device));
            CHECK(conv[v]->weight_data_gpu.data==all[v*2].data && conv[v]->bias_data_gpu.data==all[v*2+1].data);
        }
        CHECK(vec4_allocation(gpu_input,input_scalars/4+1,device));
        for(int j=0;j<runs;++j) CHECK(vec4_allocation(outputs[j],output_vectors+1,device));
        const int v=schedule[i];
        std::vector<ncnn::VkMat> bindings={conv[v]->weight_data_gpu,gpu_input,outputs[i],conv[v]->bias_data_gpu};
        CHECK(run(i,*pipeline[v],bindings,times[i],device)==0);
    }
    CHECK(setenv("RADISH_GPU_ELAPSED","0",1)==0);
    std::puts("GPU_PACKED_A_COMPUTE_SEQUENCE_COMPLETE runs=12 all_gpu_readbacks=deferred");std::fflush(stdout);
    for(int i=0;i<runs;++i) CHECK(readback(outputs[i],output[i],output_vectors+1,device,opt)==0);
    for(int i=0;i<runs;++i) {
        char name[32];std::snprintf(name,sizeof(name),"run-%02d.f32",i);
        CHECK(destination.raw(name,output[i],output_vectors+1)==0);
    }
    ncnn::Mat weight_after[2],bias_after[2],input_after;
    for(int v=0;v<2;++v) {
        char name[64];
        CHECK(readback_scalar(conv[v]->weight_data_gpu,weight_after[v],weight_scalars,device,opt)==0);
        CHECK(readback_scalar(conv[v]->bias_data_gpu,bias_after[v],channels,device,opt)==0);
        std::snprintf(name,sizeof(name),"%s-weights-before.f32",raw_model_names[v]);CHECK(destination.raw_scalar(name,weight_before[v],weight_scalars)==0);
        std::snprintf(name,sizeof(name),"%s-weights-after.f32",raw_model_names[v]);CHECK(destination.raw_scalar(name,weight_after[v],weight_scalars)==0);
        std::snprintf(name,sizeof(name),"%s-bias-before.f32",raw_model_names[v]);CHECK(destination.raw_scalar(name,bias_before[v],channels)==0);
        std::snprintf(name,sizeof(name),"%s-bias-after.f32",raw_model_names[v]);CHECK(destination.raw_scalar(name,bias_after[v],channels)==0);
    }
    CHECK(readback(gpu_input,input_after,input_scalars/4+1,device,opt)==0);
    CHECK(destination.raw("input-before.f32",input,input_scalars/4+1)==0);
    CHECK(destination.raw("input-after.f32",input_after,input_scalars/4+1)==0);
    std::puts("GPU_PACKED_A_RAW_CAPTURE_COMPLETE runs=12 files=22 bytes=4468960 validation=pending");std::fflush(stdout);
    CHECK(original_unchanged(model)==0);
    for(int v=0;v<2;++v) {
        CHECK(std::memcmp(weight_before[v].data,weight_after[v].data,static_cast<size_t>(weight_scalars)*4u)==0);
        CHECK(std::memcmp(bias_before[v].data,bias_after[v].data,static_cast<size_t>(channels)*4u)==0);
    }
    CHECK(std::memcmp(input.data,input_after.data,bytes(input_scalars/4+1))==0);
    std::vector<uint32_t> oracle;CHECK(make_oracle(oracle)==0);
    double measured_seconds[2]={};int measured_count[2]={};
    for(int i=0;i<runs;++i) {
        for(int j=0;j<output_vectors*4;++j) CHECK(get(output[i],j)==oracle[j]);
        CHECK(std::memcmp(output[0].data,output[i].data,bytes(output_vectors))==0);
        for(int lane=0;lane<4;++lane) CHECK(get(output[i],output_vectors*4+lane)==guard(10+i,lane));
        std::printf("GPU_PACKED_A_VALIDATED kind=%s index=%d payload_values=27264 guard_values=4 exact=1 cross_variant_payload_bit_identical=1\n",kinds[schedule[i]],i);
        if(i>=warmups) { measured_seconds[schedule[i]]+=times[i];++measured_count[schedule[i]]; }
    }
    for(int v=0;v<2;++v) {
        CHECK(measured_count[v]==4);
        std::printf("GPU_PACKED_A_MEASURED_SUMMARY kind=%s runs=4 record_submit_wait_total_seconds=%.9f mean_seconds=%.9f\n",kinds[v],measured_seconds[v],measured_seconds[v]/4.0);
    }
    CHECK(destination.complete(times,creation)==0);
    std::puts("PASS: GPU packed-A paired probe warmup_runs=4 measured_runs=8 outputs_per_run=27264 all_outputs=327168; exact oracle and cross-variant payload identity, 52 guard words and all actual GPU model/input bytes preserved; native/timestamp/chunk/submit qualification separate");return 0;
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
    Environment environment;
    CHECK(setenv("RADISH_GPU_ELAPSED", "0", 1) == 0);
    CHECK(unsetenv("RADISH_GPU_ELAPSED_CALIBRATION") == 0);
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
    opt.lightmode = true;
    return probe(compile_only, destination, device, opt);
}
