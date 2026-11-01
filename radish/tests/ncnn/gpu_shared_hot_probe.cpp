#include "allocator.h"
#include "command.h"
#include "gpu.h"
#include "pipeline.h"
#include "layer_shader_type.h"
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
const int runs = 12;
const int warmups = 4;
const int schedule[runs] = {0,1,1,0,0,1,1,0,0,1,1,0};
const int width=213, channels=128, reduction=1408, output_vectors=6816;
const char* kinds[2]={"planar-baseline","shared-hot"};
const char* input_names[5]={"baseline-a","baseline-b","hot-a","hot-b","bias"};
const int input_scalars[5]={128*1408,128*268,128*32,32*256,128};
const char hot_shader[] = R"glsl(#version 450

layout(local_size_x = 16, local_size_y = 8, local_size_z = 1) in;

const int num_output = 128;
const int bias_term = 1;

layout(binding = 0) readonly buffer weights { vec4 weight_data[]; };
layout(binding = 1) readonly buffer bottom_blob { float bottom_blob_data[]; };
layout(binding = 2) writeonly buffer top_blob { vec4 top_blob_data[]; };
layout(binding = 3) readonly buffer bias_blob { float bias_data[]; };

layout(push_constant) uniform parameter
{
    int repeats;
    int outw;
} p;

// Exactly 16 KiB, filled once by the complete workgroup.
shared float sa[32][64];
shared vec4 sb[32][16];

vec4 load_input(int k, int x)
{
    const int base = k * 256 + x;
    return vec4(bottom_blob_data[base], bottom_blob_data[base + 1],
                bottom_blob_data[base + 2], bottom_blob_data[base + 3]);
}

void main()
{
    const int tx = int(gl_LocalInvocationID.x);
    const int ty = int(gl_LocalInvocationID.y);
    const int tid = ty * 16 + tx;
    const int mb = int(gl_WorkGroupID.y) * 64;
    const int nb = int(gl_WorkGroupID.x) * 64;
    const int K = 32;
    const int K4 = K >> 2;

    precise vec4 acc[8];
    for (int r = 0; r < 8; r++)
    {
        const int co = mb + ty * 8 + r;
        acc[r] = vec4(bias_term != 0 && co < num_output ? bias_data[co] : 0.f);
    }

    for (int i = tid; i < 64 * 8; i += 128)
    {
        const int r = i >> 3;
        const int q = i % 8;
        vec4 a = mb + r < num_output ? weight_data[(mb + r) * K4 + q] : vec4(0.f);
        sa[q * 4][r] = a.x;
        sa[q * 4 + 1][r] = a.y;
        sa[q * 4 + 2][r] = a.z;
        sa[q * 4 + 3][r] = a.w;
    }
    for (int i = tid; i < 32 * 16; i += 128)
    {
        const int r = i >> 4;
        const int q = i % 16;
        sb[r][q] = load_input(r, nb + q * 4);
    }
    barrier();

    for (int repeat_index = 0; repeat_index < p.repeats; repeat_index++)
    {
        for (int k = 0; k < 32; k++)
        {
            vec4 b = sb[k][tx];
            for (int r = 0; r < 8; r++)
            {
                float a = sa[k][ty * 8 + r];
                acc[r] += a * b;
            }
        }
    }

    // Transpose four channel rows into ncnn's pack4 at each spatial position.
    const int x = nb + tx * 4;
    for (int r = 0; r < 8; r += 4)
    {
        const int co = mb + ty * 8 + r;
        if (co + 3 >= num_output)
            continue;
        const int base = (co >> 2) * p.outw + x;
        if (x < p.outw)
            top_blob_data[base] = vec4(acc[r].x, acc[r + 1].x, acc[r + 2].x, acc[r + 3].x);
        if (x + 1 < p.outw)
            top_blob_data[base + 1] = vec4(acc[r].y, acc[r + 1].y, acc[r + 2].y, acc[r + 3].y);
        if (x + 2 < p.outw)
            top_blob_data[base + 2] = vec4(acc[r].z, acc[r + 1].z, acc[r + 2].z, acc[r + 3].z);
        if (x + 3 < p.outw)
            top_blob_data[base + 3] = vec4(acc[r].w, acc[r + 1].w, acc[r + 2].w, acc[r + 3].w);
    }
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
int a_integer(int c,int k) { return ((13*c+7*k+3)%31)-15; }
int b_integer(int k,int x) { return ((11*k+5*x+1)%29)-14; }
int bias_integer(int c) { return ((17*c+9)%255)-127; }
int baseline_b_integer(int ci,int xpad) { return xpad>=5 && xpad<218 ? b_integer(ci,xpad-5) : 0; }
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

bool spirv_contract(const std::vector<uint32_t>& spv) {
    if(spv.size()<5 || spv[0]!=0x07230203u) return false;
    unsigned adds=0,muls=0,loops=0,barriers=0,shared_variables=0;
    std::set<uint32_t> precise;
    std::vector<uint32_t> arithmetic;
    for(size_t i=5;i<spv.size();) {
        const uint32_t wc=spv[i]>>16,op=spv[i]&65535u;
        if(!wc || wc>spv.size()-i) return false;
        if(op==71 && wc==3 && spv[i+2]==42) precise.insert(spv[i+1]);
        if(op==129 || op==133 || op==142) {
            if(wc!=5) return false;
            arithmetic.push_back(spv[i+2]);
            if(op==129) ++adds; else ++muls;
        }
        if(op==246) ++loops;
        if(op==224) ++barriers;
        if(op==59 && wc>=4 && spv[i+3]==4) ++shared_variables;
        if(op==12 || op==127 || op==131 || op==136 || op==140 || op==141 || op==225) return false;
        if(op==22 && (wc!=3 || spv[i+2]!=32)) return false;
        i+=wc;
    }
    if(loops!=7 || adds!=1 || muls!=1 || barriers!=1 || shared_variables!=2) return false;
    for(uint32_t id:arithmetic) if(!precise.count(id)) return false;
    return true;
}
int compile(ncnn::Pipeline& pipeline,bool hot,const ncnn::Option& opt) {
    std::printf("GPU_SHARED_HOT_PIPELINE_BEGIN kind=%s local=16,8,1 descriptors=4 pushes=2 shared=16384 native=unknown\n",kinds[hot?1:0]);std::fflush(stdout);
    pipeline.set_local_size_xyz(16,8,1);
    size_t words=0;
    if(hot) {
        std::vector<uint32_t> spv;
        CHECK(ncnn::compile_spirv_module(hot_shader,static_cast<int>(sizeof(hot_shader)-1),opt,spv)==0 && spirv_contract(spv));
        words=spv.size();
        const std::vector<ncnn::vk_specialization_type> spec;
        CHECK(pipeline.create(spv.data(),spv.size()*4u,spec)==0);
    } else {
        std::vector<ncnn::vk_specialization_type> spec(5);
        spec[0].i=11;spec[1].i=128;spec[2].i=128;spec[3].i=1;spec[4].i=1;
        CHECK(pipeline.create(ncnn::LayerShaderType::convolution1d_radish_planar,opt,spec)==0);
    }
    CHECK(pipeline.pipeline()!=VK_NULL_HANDLE && pipeline.local_size_x()==16 && pipeline.local_size_y()==8 && pipeline.local_size_z()==1);
    CHECK(pipeline.shader_info().specialization_count==(hot?0:5));
    CHECK(pipeline.shader_info().binding_count==4 && pipeline.shader_info().push_constant_count==2);
    for(int i=0;i<4;++i) CHECK(pipeline.shader_info().binding_types[i]==1);
    std::printf("GPU_SHARED_HOT_PIPELINE_END kind=%s status=PASS builtin=%d spirv_words=%zu actual_native_resource_admission=pending\n",kinds[hot?1:0],hot?0:1,words);std::fflush(stdout);return 0;
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
        const int wrote=std::fputs("{\"format\":\"radish-gpu-shared-hot-v1\",\"status\":\"pending\"}\n",f);
        const int closed=std::fclose(f);CHECK(wrote>=0 && closed==0);return 0;
    }
    int raw(const char* name,const ncnn::Mat& value,int vectors) const {
        CHECK(host_shape(value,vectors));FILE* f=open_file(name);CHECK(f);
        const bool wrote=std::fwrite(value.data,1,bytes(vectors),f)==bytes(vectors);
        const int closed=std::fclose(f);CHECK(wrote && closed==0);return 0;
    }
    int complete(const double times[runs]) const {
        FILE* f=open_file("result.json.tmp");CHECK(f);
        std::fprintf(f,"{\n\"format\":\"radish-gpu-shared-hot-v1\",\n\"status\":\"passed\",\n\"warmup_runs\":4,\"measured_runs\":8,\"total_runs\":12,\"expected_samples\":12,\n\"operations_per_run\":92274688,\"useful_operations_per_run\":76775424,\"payload_bytes\":109056,\"tail_guard_bytes\":16,\"total_validated_values\":327168,\n\"all_outputs_exact\":true,\"same_kind_output_payloads_bit_identical\":true,\"all_guards_unchanged\":true,\"all_inputs_read_back_unchanged\":true,\"per_run_poison\":true,\"distinct_gpu_destinations\":true,\"raw_before_reference\":true,\n\"timing_scope\":\"Record, submit and wait only; all twelve output downloads are deferred until after the timed sequence. CPU uploads, poison, reference and file writes excluded. Actual native admission and timestamp/profile qualification are separate.\",\n\"runs\":[\n");
        for(int i=0;i<runs;++i)
            std::fprintf(f,"{\"kind\":\"%s\",\"index\":%d,\"phase\":\"%s\",\"push0\":%d,\"push1\":213,\"output\":\"run-%02d.f32\",\"bytes\":%zu,\"record_submit_wait_seconds\":%.17g}%s\n",kinds[schedule[i]],i,i<warmups?"warmup":"measured",schedule[i]?44:268,i,bytes(output_vectors+1),times[i],i==runs-1?"":",");
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
int run(int index,ncnn::Pipeline& pipeline,const std::vector<ncnn::VkMat>& bindings,
        double& seconds,const ncnn::VulkanDevice* device) {
    const bool hot=schedule[index]!=0;
    if(hot) CHECK(setenv("RADISH_GPU_ELAPSED_CALIBRATION","shared-hot",1)==0);
    else CHECK(unsetenv("RADISH_GPU_ELAPSED_CALIBRATION")==0);
    CHECK(setenv("RADISH_GPU_ELAPSED","1",1)==0);
    std::printf("GPU_SHARED_HOT_RUN_BEGIN kind=%s index=%d phase=%s push0=%d outw=213 updates_per_accumulator=1408 grid=4,2,1 local=16,8,1 groups=8 expected_chunks=8 expected_submits=1\n",kinds[hot?1:0],index,index<warmups?"warmup":"measured",hot?44:268);std::fflush(stdout);
    ncnn::VkCompute command(device);std::vector<ncnn::vk_constant_type> constants(2);
    constants[0].i=hot?44:268;constants[1].i=width;
    ncnn::VkMat dispatcher;dispatcher.w=64;dispatcher.h=16;dispatcher.c=1;
    const auto begin=std::chrono::steady_clock::now();const auto before=command.pending_dispatch_total();
    command.record_pipeline(&pipeline,bindings,constants,dispatcher);
    CHECK(command.pending_dispatch_total()-before==8);
    CHECK(command.submit_and_wait()==0);
    seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    CHECK(std::isfinite(seconds) && seconds>0);
    CHECK(setenv("RADISH_GPU_ELAPSED","0",1)==0);
    std::printf("GPU_SHARED_HOT_RUN_END kind=%s index=%d phase=%s status=SUBMITTED record_submit_wait_seconds=%.9f\n",kinds[hot?1:0],index,index<warmups?"warmup":"measured",seconds);std::fflush(stdout);return 0;
}
int initialize_input(ncnn::Mat& input,int index) {
    const int scalars=input_scalars[index],vectors=scalars/4+1;
    input.create(vectors,static_cast<size_t>(16),4);CHECK(host_shape(input,vectors));
    for(int i=0;i<scalars;++i) {
        int numerator=0,denominator=64;
        if(index==0) numerator=a_integer(i/reduction,(i%reduction)%32);
        else if(index==1) numerator=baseline_b_integer(i/268,i%268);
        else if(index==2) numerator=a_integer(i/32,i%32);
        else if(index==3) numerator=b_integer(i/256,i%256);
        else { numerator=bias_integer(i);denominator=4096; }
        put(input,i,bits(static_cast<float>(numerator)/static_cast<float>(denominator)));
    }
    for(int lane=0;lane<4;++lane) put(input,scalars+lane,guard(index+1,lane));
    return 0;
}
int make_oracle(bool hot,std::vector<uint32_t>& oracle) {
    oracle.resize(channels*width);
    for(int c=0;c<channels;++c) for(int x=0;x<width;++x) {
        int64_t numerator=bias_integer(c);
        if(hot) {
            int64_t tile=0;
            for(int k=0;k<32;++k) tile+=a_integer(c,k)*b_integer(k,x);
            numerator+=44*tile;
        } else {
            for(int ci=0;ci<channels;++ci) for(int tap=0;tap<11;++tap)
                numerator+=a_integer(c,(ci*11+tap)%32)*baseline_b_integer(ci,x+tap);
        }
        CHECK(numerator>=-295807 && numerator<=295807);
        const size_t packed=(static_cast<size_t>(c/4)*width+x)*4+c%4;
        oracle[packed]=bits(static_cast<float>(static_cast<double>(numerator)/4096.0));
    }
    return 0;
}
int execute(OutputDirectory& destination,ncnn::Pipeline& baseline,ncnn::Pipeline& hot,
            const ncnn::VulkanDevice* device,ncnn::Option& opt) {
    Allocators pool(device,opt);CHECK(opt.blob_vkallocator && opt.staging_vkallocator);
    ncnn::Mat input[5];ncnn::VkMat gpu_input[5],outputs[runs];
    for(int i=0;i<5;++i) {
        CHECK(initialize_input(input[i],i)==0);
        CHECK(upload(input[i],gpu_input[i],input_scalars[i]/4+1,device,opt)==0);
    }
    ncnn::Mat poison(output_vectors+1,static_cast<size_t>(16),4);CHECK(host_shape(poison,output_vectors+1));
    for(int i=0;i<runs;++i) {
        for(int j=0;j<output_vectors*4;++j) put(poison,j,0x7fc00100u+static_cast<unsigned>(i));
        for(int lane=0;lane<4;++lane) put(poison,output_vectors*4+lane,guard(10+i,lane));
        CHECK(upload(poison,outputs[i],output_vectors+1,device,opt)==0);
        std::printf("GPU_SHARED_HOT_OUTPUT_POISON index=%d payload_values=27264 guard_values=4 changing_nan=1 uploaded_before_sequence=1\n",i);
    }
    std::fflush(stdout);
    std::vector<ncnn::VkMat> all;
    for(int i=0;i<5;++i) all.push_back(gpu_input[i]);
    for(int i=0;i<runs;++i) all.push_back(outputs[i]);
    for(size_t i=0;i<all.size();++i) for(size_t j=0;j<i;++j) CHECK(disjoint(all[i],all[j]));
    const VkPipeline baseline_handle=baseline.pipeline(),hot_handle=hot.pipeline();
    CHECK(baseline_handle!=VK_NULL_HANDLE && hot_handle!=VK_NULL_HANDLE && baseline_handle!=hot_handle);
    ncnn::Mat output[runs];double times[runs]={};
    for(int i=0;i<runs;++i) {
        CHECK(baseline.pipeline()==baseline_handle && hot.pipeline()==hot_handle);
        for(int j=0;j<5;++j) CHECK(allocation(gpu_input[j],input_scalars[j]/4+1,device));
        CHECK(allocation(outputs[i],output_vectors+1,device));
        const int first=schedule[i]?2:0;
        std::vector<ncnn::VkMat> bindings={gpu_input[first],gpu_input[first+1],outputs[i],gpu_input[4]};
        CHECK(run(i,schedule[i]?hot:baseline,bindings,times[i],device)==0);
        CHECK(baseline.pipeline()==baseline_handle && hot.pipeline()==hot_handle);
    }
    CHECK(setenv("RADISH_GPU_ELAPSED","0",1)==0);
    std::puts("GPU_SHARED_HOT_COMPUTE_SEQUENCE_COMPLETE runs=12 output_readbacks=deferred");std::fflush(stdout);
    for(int i=0;i<runs;++i) CHECK(readback(outputs[i],output[i],output_vectors+1,device,opt)==0);
    for(int i=0;i<runs;++i) {
        char name[32];std::snprintf(name,sizeof(name),"run-%02d.f32",i);
        CHECK(destination.raw(name,output[i],output_vectors+1)==0);
    }
    ncnn::Mat input_after[5];
    for(int i=0;i<5;++i) {
        const int vectors=input_scalars[i]/4+1;char name[64];
        CHECK(readback(gpu_input[i],input_after[i],vectors,device,opt)==0);
        std::snprintf(name,sizeof(name),"%s-before.f32",input_names[i]);CHECK(destination.raw(name,input[i],vectors)==0);
        std::snprintf(name,sizeof(name),"%s-after.f32",input_names[i]);CHECK(destination.raw(name,input_after[i],vectors)==0);
    }
    std::puts("GPU_SHARED_HOT_RAW_CAPTURE_COMPLETE runs=12 files=22 bytes=3124576 validation=pending");std::fflush(stdout);
    for(int i=0;i<5;++i) CHECK(std::memcmp(input[i].data,input_after[i].data,bytes(input_scalars[i]/4+1))==0);
    std::vector<uint32_t> oracle[2];CHECK(make_oracle(false,oracle[0])==0 && make_oracle(true,oracle[1])==0);
    double measured_seconds[2]={};int measured_count[2]={};
    for(int i=0;i<runs;++i) {
        for(int j=0;j<output_vectors*4;++j) CHECK(get(output[i],j)==oracle[schedule[i]][j]);
        CHECK(std::memcmp(output[schedule[i]?1:0].data,output[i].data,bytes(output_vectors))==0);
        for(int lane=0;lane<4;++lane) CHECK(get(output[i],output_vectors*4+lane)==guard(10+i,lane));
        std::printf("GPU_SHARED_HOT_VALIDATED kind=%s index=%d payload_values=27264 guard_values=4 exact=1 same_kind_payload_bit_identical=1\n",kinds[schedule[i]],i);
        if(i>=warmups) { measured_seconds[schedule[i]]+=times[i];++measured_count[schedule[i]]; }
    }
    for(int variant=0;variant<2;++variant) {
        CHECK(measured_count[variant]==4);
        std::printf("GPU_SHARED_HOT_MEASURED_SUMMARY kind=%s runs=4 record_submit_wait_total_seconds=%.9f mean_seconds=%.9f\n",kinds[variant],measured_seconds[variant],measured_seconds[variant]/4.0);
    }
    CHECK(destination.complete(times)==0);
    std::puts("PASS: GPU shared-hot balanced probe warmup_runs=4 measured_runs=8 outputs_per_run=27264 all_outputs=327168; each output exact, same-kind payloads bit-identical, per-run guards unchanged, five GPU inputs read back unchanged; native/timestamp qualification separate");return 0;
}
}
int main(int argc,char** argv) {
    if(argc!=2 || !argv[1][0]) { std::fprintf(stderr,"Usage: %s NEW_ABSOLUTE_OUTPUT_DIR | --compile-only\n",argv[0]);return 2; }
    const bool compile_only=std::strcmp(argv[1],"--compile-only")==0;
    if(!compile_only && (argv[1][0]!='/' || argv[1][1]=='\0')) return 2;
    const char* keys[]={"RADISH_PROFILE","RADISH_BATCH_CHUNKS","RADISH_TILED_BATCH_CHUNKS","RADISH_TILED_GROUPS_X"};
    const char* values[]={"1","8","8","1"};
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
    ncnn::Pipeline baseline(device),hot(device);CHECK(compile(baseline,false,opt)==0 && compile(hot,true,opt)==0);
    CHECK(baseline.pipeline()!=hot.pipeline() && baseline.shader_module()!=hot.shader_module());
    if(compile_only) { std::puts("PASS: GPU shared-hot compile-only; no probe tensors, allocators, uploads or VkCompute work (device initialization may submit)");return 0; }
    return execute(destination,baseline,hot,device,opt);
}
