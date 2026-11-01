#include "allocator.h"
#include "command.h"
#include "gpu.h"
#include "pipeline.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
const int runs = 5;
const int mad_vectors = 8192;
const int copy_vectors = 817952;
const unsigned iterations = 4096;
const unsigned segment = 102244;
const int steps[4] = {1, -1, 3, -3};
const char mad_shader[] = R"glsl(#version 450
layout(local_size_x=16,local_size_y=8,local_size_z=1) in;
layout(std430,binding=0) readonly buffer Seeds { vec4 seeds[]; };
layout(std430,binding=1) readonly buffer Coeff { vec4 coeff[]; };
layout(std430,binding=2) writeonly buffer Output { vec4 output_data[]; };
layout(push_constant) uniform parameter { uint iterations; } p;
void main() {
    uint t = (gl_WorkGroupID.y * 4u + gl_WorkGroupID.x) * 128u + gl_LocalInvocationID.y * 16u + gl_LocalInvocationID.x;
    uint base = t * 8u;
    vec4 scale = coeff[0];
    vec4 step = coeff[1];
    precise vec4 a0 = seeds[base+0u];
    precise vec4 a1 = seeds[base+1u];
    precise vec4 a2 = seeds[base+2u];
    precise vec4 a3 = seeds[base+3u];
    precise vec4 a4 = seeds[base+4u];
    precise vec4 a5 = seeds[base+5u];
    precise vec4 a6 = seeds[base+6u];
    precise vec4 a7 = seeds[base+7u];
    for (uint i=0u; i<p.iterations; ++i) {
        precise vec4 p0 = a0 * scale; a0 = p0 + step;
        precise vec4 p1 = a1 * scale; a1 = p1 + step;
        precise vec4 p2 = a2 * scale; a2 = p2 + step;
        precise vec4 p3 = a3 * scale; a3 = p3 + step;
        precise vec4 p4 = a4 * scale; a4 = p4 + step;
        precise vec4 p5 = a5 * scale; a5 = p5 + step;
        precise vec4 p6 = a6 * scale; a6 = p6 + step;
        precise vec4 p7 = a7 * scale; a7 = p7 + step;
    }
    output_data[base+0u]=a0; output_data[base+1u]=a1;
    output_data[base+2u]=a2; output_data[base+3u]=a3;
    output_data[base+4u]=a4; output_data[base+5u]=a5;
    output_data[base+6u]=a6; output_data[base+7u]=a7;
}
)glsl";
const char copy_shader[] = R"glsl(#version 450
layout(local_size_x=16,local_size_y=8,local_size_z=1) in;
layout(std430,binding=0) readonly buffer Input { vec4 input_data[]; };
layout(std430,binding=1) writeonly buffer Output { vec4 output_data[]; };
layout(push_constant) uniform parameter { uint segment; } p;
void main() {
    uint group = gl_WorkGroupID.y * 4u + gl_WorkGroupID.x;
    uint lane = gl_LocalInvocationID.y * 16u + gl_LocalInvocationID.x;
    uint base = group * p.segment;
    for (uint j=lane; j<p.segment; j+=128u)
        output_data[base+j] = input_data[base+j];
}
)glsl";

struct InstanceLifetime { ~InstanceLifetime() { ncnn::destroy_gpu_instance(); } };
struct Environment {
    const char* keys[2] = {"RADISH_GPU_ELAPSED", "RADISH_GPU_ELAPSED_CALIBRATION"};
    bool present[2];
    std::string previous[2];
    Environment() { for (int i=0;i<2;++i) { const char* v=std::getenv(keys[i]); present[i]=v!=0; previous[i]=v?v:""; } }
    ~Environment() { for (int i=0;i<2;++i) { if(present[i]) setenv(keys[i],previous[i].c_str(),1); else unsetenv(keys[i]); } }
};
struct Allocators {
    const ncnn::VulkanDevice* device;
    ncnn::Option& opt;
    Allocators(const ncnn::VulkanDevice* d,ncnn::Option& o):device(d),opt(o) {
        opt.blob_vkallocator=device->acquire_blob_allocator();
        opt.workspace_vkallocator=opt.blob_vkallocator;
        opt.staging_vkallocator=device->acquire_staging_allocator();
    }
    ~Allocators() {
        if(opt.blob_vkallocator) device->reclaim_blob_allocator(opt.blob_vkallocator);
        if(opt.staging_vkallocator) device->reclaim_staging_allocator(opt.staging_vkallocator);
    }
};

uint32_t bits(float value) { uint32_t x; std::memcpy(&x,&value,4); return x; }
void put(ncnn::Mat& m,size_t i,uint32_t value) { std::memcpy(static_cast<unsigned char*>(m.data)+4*i,&value,4); }
uint32_t get(const ncnn::Mat& m,size_t i) { uint32_t x; std::memcpy(&x,static_cast<const unsigned char*>(m.data)+4*i,4); return x; }
int seed_numerator(size_t i) { return static_cast<int>((i*37u+(i/32u)*13u)%257u)-128; }
uint32_t copy_value(size_t i) { return 0x3f000000u | static_cast<uint32_t>((i*2654435761ull+17u)&0x7fffffu) | ((i&1u)?0x80000000u:0); }
uint32_t guard(int tag,int lane) { return 0x3e800000u | static_cast<uint32_t>(tag*131+lane*17+3); }
size_t bytes(int vectors) { return static_cast<size_t>(vectors)*16u; }

bool host_shape(const ncnn::Mat& m,int vectors) {
    return !m.empty() && m.dims==1 && m.w==vectors && m.h==1 && m.d==1 && m.c==1 && m.n==1
        && m.elempack==4 && m.elemsize==16u && m.total()==static_cast<size_t>(vectors);
}
bool allocation(const ncnn::VkMat& m,int vectors,const ncnn::VulkanDevice* device) {
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

bool spirv_contract(const std::vector<uint32_t>& spv,bool mad) {
    if(spv.size()<5 || spv[0]!=0x07230203u) return false;
    unsigned adds=0,muls=0,loops=0;
    std::set<uint32_t> precise;
    std::vector<uint32_t> arithmetic;
    for(size_t i=5;i<spv.size();) {
        const uint32_t wc=spv[i]>>16,op=spv[i]&65535u;
        if(!wc || wc>spv.size()-i) return false;
        if(op==71 && wc==3 && spv[i+2]==42) precise.insert(spv[i+1]);
        if(op==129 || op==133) {
            if(wc!=5) return false;
            arithmetic.push_back(spv[i+2]);
            if(op==129) ++adds; else ++muls;
        }
        if(op==246) ++loops;
        if(op==12 || op==127 || op==131 || op==136 || op==140 || op==141 || op==224 || op==225 || (op==59 && wc>=4 && spv[i+3]==4)) return false;
        if(op==22 && (wc!=3 || spv[i+2]!=32)) return false;
        i+=wc;
    }
    if(loops!=1 || adds!=(mad?8u:0u) || muls!=(mad?8u:0u)) return false;
    for(uint32_t id:arithmetic) if(!precise.count(id)) return false;
    return true;
}
int compile(ncnn::Pipeline& pipeline,bool mad,const ncnn::Option& opt) {
    const char* name=mad?"mad":"copy";
    std::printf("GPU_CALIBRATION_PIPELINE_BEGIN kind=%s local=16,8,1 descriptors=%d pushes=1 shared=0 native=unknown\n",name,mad?3:2); std::fflush(stdout);
    std::vector<uint32_t> spv;
    const char* source=mad?mad_shader:copy_shader;
    const int length=static_cast<int>(mad?sizeof(mad_shader)-1:sizeof(copy_shader)-1);
    CHECK(ncnn::compile_spirv_module(source,length,opt,spv)==0 && spirv_contract(spv,mad));
    pipeline.set_local_size_xyz(16,8,1);
    const std::vector<ncnn::vk_specialization_type> spec;
    CHECK(pipeline.create(spv.data(),spv.size()*4u,spec)==0);
    CHECK(pipeline.pipeline()!=VK_NULL_HANDLE && pipeline.local_size_x()==16 && pipeline.local_size_y()==8 && pipeline.local_size_z()==1);
    CHECK(pipeline.shader_info().binding_count==(mad?3:2) && pipeline.shader_info().push_constant_count==1);
    for(int i=0;i<(mad?3:2);++i) CHECK(pipeline.shader_info().binding_types[i]==1);
    std::printf("GPU_CALIBRATION_PIPELINE_END kind=%s status=PASS spirv_words=%zu fmul=%d fadd=%d loops=1 no_contraction=1 actual_native_resource_admission=pending\n",name,spv.size(),mad?8:0,mad?8:0); std::fflush(stdout);
    return 0;
}

struct OutputDirectory {
    int fd=-1;
    ~OutputDirectory() { if(fd>=0) close(fd); }
    FILE* open_file(const char* name) const {
        int out=openat(fd,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(out<0) return 0;
        FILE* f=fdopen(out,"wb"); if(!f) close(out); return f;
    }
    int create(const char* path) {
        CHECK(mkdir(path,0700)==0);
        fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); CHECK(fd>=0);
        FILE* f=open_file("pending.json"); CHECK(f);
        const int wrote=std::fputs("{\"format\":\"radish-gpu-calibration-v2\",\"status\":\"pending\"}\n",f);
        const int closed=std::fclose(f); CHECK(wrote>=0 && closed==0); return 0;
    }
    int raw(const char* name,const ncnn::Mat& value,int vectors) const {
        CHECK(host_shape(value,vectors)); FILE* f=open_file(name); CHECK(f);
        const bool wrote=std::fwrite(value.data,1,bytes(vectors),f)==bytes(vectors);
        const int closed=std::fclose(f); CHECK(wrote && closed==0); return 0;
    }
    int complete(const double times[2][runs]) const {
        FILE* f=open_file("result.json.tmp"); CHECK(f);
        std::fprintf(f,"{\n\"format\":\"radish-gpu-calibration-v2\",\"status\":\"passed\",\n\"warmup_per_kind\":2,\"measured_per_kind\":3,\"total_runs\":10,\"expected_samples\":10,\n\"mad_operations_per_run\":268435456,\"mad_payload_bytes\":131072,\"copy_payload_bytes\":13087232,\"copy_read_write_bytes\":26174464,\"tail_guard_bytes\":16,\n\"all_outputs_exact\":true,\"all_guards_unchanged\":true,\"all_inputs_read_back_unchanged\":true,\"raw_before_reference\":true,\n\"timing_scope\":\"Record, submit and wait only; all ten output downloads are deferred until after the timed sequence. CPU uploads, reference and file writes excluded. Actual native admission and timestamp/profile qualification are separate.\",\n\"runs\":[\n");
        for(int kind=0;kind<2;++kind) for(int i=0;i<runs;++i)
            std::fprintf(f,"{\"kind\":\"%s\",\"index\":%d,\"phase\":\"%s\",\"output\":\"%s-%02d.f32\",\"bytes\":%zu,\"record_submit_wait_seconds\":%.17g}%s\n",kind?"copy":"mad",i,i<2?"warmup":"measured",kind?"copy":"mad",i,bytes((kind?copy_vectors:mad_vectors)+1),times[kind][i],kind==1 && i==runs-1?"":",");
        std::fputs("]\n}\n",f);const bool okay=std::ferror(f)==0;const int closed=std::fclose(f);CHECK(okay && closed==0);
        CHECK(linkat(fd,"result.json.tmp",fd,"result.json",0)==0);CHECK(unlinkat(fd,"result.json.tmp",0)==0);CHECK(unlinkat(fd,"pending.json",0)==0);return 0;
    }
};
int upload(const ncnn::Mat& host,ncnn::VkMat& gpu,int vectors,const ncnn::VulkanDevice* device,const ncnn::Option& opt) {
    CHECK(host_shape(host,vectors));
    gpu.create(vectors,static_cast<size_t>(16),4,opt.blob_vkallocator);
    CHECK(allocation(gpu,vectors,device) && gpu.allocator->mappable && gpu.mapped_ptr());
    ncnn::VkTransfer command(device);
    command.record_upload(host,gpu,opt,false); CHECK(command.pending_upload_total()==bytes(vectors));
    CHECK(command.submit_and_wait()==0); return 0;
}
int readback(const ncnn::VkMat& gpu,ncnn::Mat& host,int vectors,const ncnn::VulkanDevice* device,const ncnn::Option& opt) {
    CHECK(allocation(gpu,vectors,device) && gpu.allocator->mappable && gpu.mapped_ptr());
    ncnn::VkCompute command(device); command.record_clone(gpu,host,opt);
    CHECK(command.pending_dispatch_total()==0);
    CHECK(command.submit_and_wait()==0 && host_shape(host,vectors)); return 0;
}
int run(bool mad,int index,ncnn::Pipeline& pipeline,const std::vector<ncnn::VkMat>& bindings,
        double& seconds,const ncnn::VulkanDevice* device) {
    CHECK(setenv("RADISH_GPU_ELAPSED_CALIBRATION",mad?"mad":"copy",1)==0);
    CHECK(setenv("RADISH_GPU_ELAPSED","1",1)==0);
    std::printf("GPU_CALIBRATION_RUN_BEGIN kind=%s index=%d phase=%s grid=4,2,1 local=16,8,1 groups=8 expected_chunks=1 expected_submits=1\n",mad?"mad":"copy",index,index<2?"warmup":"measured");std::fflush(stdout);
    ncnn::VkCompute command(device);std::vector<ncnn::vk_constant_type> constants(1);
    constants[0].u32=mad?iterations:segment;
    ncnn::VkMat dispatcher;dispatcher.w=64;dispatcher.h=16;dispatcher.c=1;
    const auto begin=std::chrono::steady_clock::now();const auto before=command.pending_dispatch_total();
    command.record_pipeline(&pipeline,bindings,constants,dispatcher);
    CHECK(command.pending_dispatch_total()-before==8);
    CHECK(command.submit_and_wait()==0);
    seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    CHECK(std::isfinite(seconds) && seconds>0);
    CHECK(setenv("RADISH_GPU_ELAPSED","0",1)==0);
    std::printf("GPU_CALIBRATION_RUN_END kind=%s index=%d phase=%s status=SUBMITTED record_submit_wait_seconds=%.9f\n",mad?"mad":"copy",index,index<2?"warmup":"measured",seconds);std::fflush(stdout);return 0;
}

int execute(OutputDirectory& destination,ncnn::Pipeline& mad,ncnn::Pipeline& copy,const ncnn::VulkanDevice* device,ncnn::Option& opt) {
    Allocators pool(device,opt);CHECK(opt.blob_vkallocator && opt.staging_vkallocator);
    ncnn::Mat seeds(mad_vectors+1,static_cast<size_t>(16),4),coef(2,static_cast<size_t>(16),4),source(copy_vectors+1,static_cast<size_t>(16),4);
    CHECK(host_shape(seeds,mad_vectors+1) && host_shape(coef,2) && host_shape(source,copy_vectors+1));
    for(int i=0;i<mad_vectors*4;++i) put(seeds,i,bits(static_cast<float>(seed_numerator(i))/4096.f));
    for(int i=0;i<4;++i) { put(coef,i,bits(1.f));put(coef,4+i,bits(static_cast<float>(steps[i])/4096.f));put(seeds,mad_vectors*4+i,guard(1,i)); }
    for(int i=0;i<copy_vectors*4;++i) put(source,i,copy_value(i));
    for(int i=0;i<4;++i) put(source,copy_vectors*4+i,guard(2,i));
    ncnn::VkMat gpu_seeds,gpu_coef,gpu_source,mad_dst[runs],copy_dst[runs];
    CHECK(upload(seeds,gpu_seeds,mad_vectors+1,device,opt)==0);
    CHECK(upload(coef,gpu_coef,2,device,opt)==0);
    CHECK(upload(source,gpu_source,copy_vectors+1,device,opt)==0);
    for(int kind=0;kind<2;++kind) {
        const int vectors=kind?copy_vectors:mad_vectors;
        ncnn::Mat poison(vectors+1,static_cast<size_t>(16),4);CHECK(host_shape(poison,vectors+1));
        for(int i=0;i<runs;++i) {
            for(int j=0;j<vectors*4;++j) put(poison,j,0x7fc00100u+static_cast<unsigned>(kind*16+i));
            for(int lane=0;lane<4;++lane) put(poison,vectors*4+lane,guard(10+kind*5+i,lane));
            CHECK(upload(poison,kind?copy_dst[i]:mad_dst[i],vectors+1,device,opt)==0);
        }
    }
    std::vector<ncnn::VkMat> all={gpu_seeds,gpu_coef,gpu_source};
    for(int i=0;i<runs;++i) { all.push_back(mad_dst[i]);all.push_back(copy_dst[i]); }
    for(size_t i=0;i<all.size();++i) for(size_t j=0;j<i;++j) CHECK(disjoint(all[i],all[j]));
    ncnn::Mat output[2][runs];double times[2][runs]={};
    for(int kind=0;kind<2;++kind) for(int i=0;i<runs;++i) {
        std::vector<ncnn::VkMat> bindings;
        if(kind==0) bindings={gpu_seeds,gpu_coef,mad_dst[i]};else bindings={gpu_source,copy_dst[i]};
        CHECK(run(kind==0,i,kind==0?mad:copy,bindings,times[kind][i],device)==0);
    }
    CHECK(setenv("RADISH_GPU_ELAPSED","0",1)==0);
    std::puts("GPU_CALIBRATION_COMPUTE_SEQUENCE_COMPLETE runs=10 output_readbacks=deferred");std::fflush(stdout);
    for(int kind=0;kind<2;++kind) for(int i=0;i<runs;++i)
        CHECK(readback(kind?copy_dst[i]:mad_dst[i],output[kind][i],(kind?copy_vectors:mad_vectors)+1,device,opt)==0);
    for(int kind=0;kind<2;++kind) for(int i=0;i<runs;++i) {
        char name[32];std::snprintf(name,sizeof(name),"%s-%02d.f32",kind?"copy":"mad",i);
        CHECK(destination.raw(name,output[kind][i],(kind?copy_vectors:mad_vectors)+1)==0);
    }
    std::puts("GPU_CALIBRATION_RAW_CAPTURE_COMPLETE runs=10 validation=pending");std::fflush(stdout);
    ncnn::Mat seeds_after,coef_after,source_after;
    CHECK(readback(gpu_seeds,seeds_after,mad_vectors+1,device,opt)==0);
    CHECK(readback(gpu_coef,coef_after,2,device,opt)==0);
    CHECK(readback(gpu_source,source_after,copy_vectors+1,device,opt)==0);
    CHECK(destination.raw("seeds-before.f32",seeds,mad_vectors+1)==0 && destination.raw("seeds-after.f32",seeds_after,mad_vectors+1)==0);
    CHECK(destination.raw("coeff-before.f32",coef,2)==0 && destination.raw("coeff-after.f32",coef_after,2)==0);
    CHECK(destination.raw("copy-source-before.f32",source,copy_vectors+1)==0 && destination.raw("copy-source-after.f32",source_after,copy_vectors+1)==0);
    CHECK(std::memcmp(seeds.data,seeds_after.data,bytes(mad_vectors+1))==0);
    CHECK(std::memcmp(coef.data,coef_after.data,32)==0);
    CHECK(std::memcmp(source.data,source_after.data,bytes(copy_vectors+1))==0);
    for(int i=0;i<runs;++i) {
        for(int j=0;j<mad_vectors*4;++j) {
            const int numerator=seed_numerator(j)+4096*steps[j%4];
            CHECK(numerator!=0 && get(output[0][i],j)==bits(static_cast<float>(static_cast<double>(numerator)/4096.0)));
        }
        for(int j=0;j<copy_vectors*4;++j) CHECK(get(output[1][i],j)==copy_value(j));
        for(int lane=0;lane<4;++lane) {
            CHECK(get(output[0][i],mad_vectors*4+lane)==guard(10+i,lane));
            CHECK(get(output[1][i],copy_vectors*4+lane)==guard(15+i,lane));
        }
        std::printf("GPU_CALIBRATION_VALIDATED kind=mad index=%d payload_values=32768 guard_values=4 exact=1\n",i);
        std::printf("GPU_CALIBRATION_VALIDATED kind=copy index=%d payload_bytes=13087232 guard_bytes=16 exact=1\n",i);
    }
    CHECK(destination.complete(times)==0);
    std::puts("PASS: GPU calibration v2 deferred readbacks: ten full outputs and tail guards exact; all seed/coefficient/source bytes read back unchanged; native/timestamp qualification separate");return 0;
}
}
int main(int argc,char** argv) {
    if(argc!=2 || !argv[1][0]) { std::fprintf(stderr,"Usage: %s NEW_ABSOLUTE_OUTPUT_DIR | --compile-only\n",argv[0]);return 2; }
    const bool compile_only=std::strcmp(argv[1],"--compile-only")==0;
    if(!compile_only && (argv[1][0]!='/' || argv[1][1]=='\0')) return 2;
    const char* keys[]={"RADISH_PROFILE","RADISH_BATCH_CHUNKS","RADISH_TILED_GROUPS_X"};
    const char* values[]={"1","8","1"};
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);++i) { const char* v=std::getenv(keys[i]);if(!v || std::strcmp(v,values[i])) { std::fprintf(stderr,"FAIL require %s=%s\n",keys[i],values[i]);return 2; } }
    Environment environment;CHECK(setenv("RADISH_GPU_ELAPSED","0",1)==0);CHECK(unsetenv("RADISH_GPU_ELAPSED_CALIBRATION")==0);
    OutputDirectory destination;if(!compile_only) CHECK(destination.create(argv[1])==0);
    CHECK(ncnn::create_gpu_instance()==0);InstanceLifetime instance;
    CHECK(ncnn::get_gpu_count()==1);const ncnn::VulkanDevice* device=ncnn::get_gpu_device(0);
    CHECK(device && device->is_valid() && device->info.vendor_id()==0x5143 && device->info.device_id()==506);
    CHECK(!std::strcmp(device->info.device_name(),"Radish Adreno 506 (experimental compute)"));
    ncnn::Option opt;opt.num_threads=1;opt.vulkan_device_index=0;
    opt.use_vulkan_compute=opt.use_packing_layout=true;
    opt.use_fp16_packed=opt.use_fp16_storage=opt.use_fp16_arithmetic=opt.use_fp16_uniform=false;
    opt.use_bf16_packed=opt.use_bf16_storage=false;
    opt.use_int8_inference=opt.use_int8_packed=opt.use_int8_storage=opt.use_int8_arithmetic=opt.use_int8_uniform=false;
    opt.use_int16_packed=opt.use_int16_storage=false;
    opt.use_subgroup_ops=opt.use_shader_local_memory=opt.use_cooperative_matrix=opt.use_tensor_storage=false;
    opt.use_weights_in_host_memory=false;
    ncnn::Pipeline mad(device),copy(device);CHECK(compile(mad,true,opt)==0 && compile(copy,false,opt)==0);
    if(compile_only) { std::puts("PASS: GPU calibration compile-only; no probe tensors, allocators, uploads or VkCompute work (device initialization may submit)");return 0; }
    return execute(destination,mad,copy,device,opt);
}
