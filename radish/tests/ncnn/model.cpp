#include "runtime_helpers.h"
#include "net.h"
#include "layer/innerproduct.h"
#if NCNN_VULKAN
#include "command.h"
#include "gpu.h"
#include "pipeline.h"
#include "pipelinecache.h"
#include "layer/vulkan/crop_vulkan.h"
#include "layer/vulkan/flatten_vulkan.h"
#include "layer/vulkan/innerproduct_vulkan.h"
#endif
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)

namespace {
using namespace radish_ncnn;
typedef std::chrono::steady_clock Clock;
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"requires IEEE FP32");
struct CoreShape {
    int time=0,frames=0;
    size_t values=0;
};
bool float_file_count(const std::string& path,size_t& count) {
    struct stat info;
    if(stat(path.c_str(),&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<=0
       || static_cast<uintmax_t>(info.st_size)%sizeof(float)!=0
       || static_cast<uintmax_t>(info.st_size)>std::numeric_limits<size_t>::max()
       || static_cast<uintmax_t>(info.st_size)/sizeof(float)>static_cast<uintmax_t>(std::numeric_limits<int>::max())) {
        std::fprintf(stderr,"invalid, oversized, or missing FP32 file: %s\n",path.c_str());
        return false;
    }
    count=static_cast<size_t>(info.st_size)/sizeof(float);
    return true;
}
bool check_float_file(const std::string& path,size_t expected) {
    size_t count=0;
    if(!float_file_count(path,count)) return false;
    if(count!=expected) {
        std::fprintf(stderr,"wrong FP32 count in %s: expected %zu, got %zu\n",path.c_str(),expected,count);
        return false;
    }
    return true;
}
int read_core_shape(const std::string& dir,bool style,CoreShape& shape) {
    CHECK(check_float_file(dir+"/style.f32",256));
    if(style) return 0;
    size_t count=0;
    CHECK(float_file_count(dir+"/asr.f32",count));
    if(count%512!=0) {
        std::fprintf(stderr,"asr.f32 must contain 512 channels with a positive integral runtime T\n");
        return 1;
    }
    const size_t time=count/512;
    const size_t max_values=std::numeric_limits<size_t>::max()/sizeof(float)<static_cast<size_t>(std::numeric_limits<int>::max())
        ?std::numeric_limits<size_t>::max()/sizeof(float):static_cast<size_t>(std::numeric_limits<int>::max());
    if(time==0 || time>(max_values/22-1)/120) {
        std::fprintf(stderr,"runtime T exceeds the checked ncnn tensor size limit\n");
        return 1;
    }
    shape.time=static_cast<int>(time);
    shape.frames=static_cast<int>(120*time+1);
    shape.values=22*static_cast<size_t>(shape.frames);
    CHECK(check_float_file(dir+"/f0.f32",2*time));
    CHECK(check_float_file(dir+"/n.f32",2*time));
    CHECK(check_float_file(dir+"/har.f32",shape.values));
    std::printf("runtime dimensions: T=%d, core=22 x %d, waveform=%zu samples\n",shape.time,shape.frames,600*time);
    return 0;
}
int check_shape_hints(const ncnn::Net& net,bool style,const CoreShape& shape) {
    if(style) return 0;
    bool hinted=false;
    for(const ncnn::Blob& blob:net.blobs()) hinted=hinted || blob.shape.dims!=0;
    if(!hinted) return 0;
    const char* names[]={"in0","in1","in2","in3","in4","out0"};
    const int widths[]={shape.time,2*shape.time,2*shape.time,256,shape.frames,shape.frames};
    const int heights[]={512,1,1,1,22,22};
    for(size_t i=0;i<6;++i) {
        bool matched=false;
        for(const ncnn::Blob& blob:net.blobs()) {
            if(blob.name!=names[i]) continue;
            const ncnn::Mat& hint=blob.shape;
            matched=hint.dims==(heights[i]==1?1:2) && hint.w==widths[i] && hint.h==heights[i]
                && hint.d==1 && hint.c==1 && hint.elempack==1 && hint.elemsize==4u;
            break;
        }
        if(!matched) {
            std::fprintf(stderr,"stale or incomplete shape hints for %s at runtime T=%d; use an unhinted graph or recapture all hints for these inputs\n",names[i],shape.time);
            return 1;
        }
    }
    return 0;
}

bool read_floats(const std::string& path,float* data,size_t count) {
    FILE* f=std::fopen(path.c_str(),"rb");
    if(!f) return false;
    bool ok=std::fread(data,sizeof(float),count,f)==count && std::fgetc(f)==EOF;
    std::fclose(f);
    return ok;
}
bool write_floats(const std::string& path,const float* data,size_t count) {
    FILE* f=std::fopen(path.c_str(),"wb");
    if(!f) return false;
    bool written=std::fwrite(data,sizeof(float),count,f)==count;
    int closed=std::fclose(f);
    return written && closed==0;
}
void print_peak_rss(const char* stage) {
    struct rusage usage;
    if(getrusage(RUSAGE_SELF,&usage)==0)
        std::printf("%s: peak process RSS %ld KiB (GPU allocation accounting is separate)\n",stage,usage.ru_maxrss);
}
double cpu_seconds(const struct rusage& usage) {
    return usage.ru_utime.tv_sec+usage.ru_stime.tv_sec
        +(usage.ru_utime.tv_usec+usage.ru_stime.tv_usec)*1e-6;
}
struct BlobShape {
    int layer,blob,dims,w,h,d,c;
};
int capture_shapes(ncnn::Net& net,ncnn::Extractor& ex,std::vector<BlobShape>& shapes) {
    std::vector<bool> seen(net.blobs().size(),false);
    for(size_t i=0;i<net.layers().size();++i) {
        for(int top:net.layers()[i]->tops) {
            CHECK(top>=0 && static_cast<size_t>(top)<seen.size() && !seen[top]);
            ncnn::Mat value;
            CHECK(ex.extract(top,value)==0);
            CHECK(!value.empty() && value.elempack==1 && value.elemsize==4u);
            CHECK(value.dims>=1 && value.dims<=4 && value.w>0 && value.h>0 && value.d>0 && value.c>0);
            shapes.push_back({static_cast<int>(i),top,value.dims,value.w,value.h,value.d,value.c});
            seen[top]=true;
        }
    }
    CHECK(shapes.size()==net.blobs().size());
    std::printf("captured %zu logical scalar blob shapes; intermediate tensors released after each extraction\n",shapes.size());
    return 0;
}
bool write_shapes(const std::string& path,const ncnn::Net& net,const std::vector<BlobShape>& shapes) {
    FILE* f=std::fopen(path.c_str(),"wx");
    if(!f) return false;
    bool ok=std::fprintf(f,"ncnn-logical-shapes-v1\n")>0;
    for(const BlobShape& s:shapes)
        ok=(std::fprintf(f,"%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\n",s.layer,s.blob,
                        net.blobs()[s.blob].name.c_str(),s.dims,s.w,s.h,s.d,s.c)>0) && ok;
    return std::fclose(f)==0 && ok;
}
#if NCNN_VULKAN
int check_pipeline(const ncnn::Pipeline* p,const char* name) {
    if(!p) return 0;
    if(p->pipeline()==VK_NULL_HANDLE) { std::fprintf(stderr,"FAIL: %s has no Vulkan pipeline\n",name); return 1; }
    unsigned x=p->local_size_x(),y=p->local_size_y(),z=p->local_size_z();
    CHECK(x && y && z && static_cast<unsigned long long>(x)*y*z<=128);
    std::printf("  pipeline %s: local %u x %u x %u\n",name,x,y,z);
    return 0;
}
#define PIPELINE(layer,member) CHECK(check_pipeline((layer)->member,#member)==0)
int check_style_pipelines(const ncnn::Net& net) {
    for(const ncnn::Layer* layer:net.layers()) {
        CHECK(layer->support_vulkan);
        if(layer->type=="Crop") {
            const ncnn::Crop_vulkan* crop=static_cast<const ncnn::Crop_vulkan*>(layer);
            CHECK(crop->pipeline_crop || crop->pipeline_crop_pack4 || crop->pipeline_crop_pack1to4 || crop->pipeline_crop_pack4to1);
            PIPELINE(crop,pipeline_crop);
            PIPELINE(crop,pipeline_crop_pack4);
            PIPELINE(crop,pipeline_crop_pack1to4);
            PIPELINE(crop,pipeline_crop_pack4to1);
        }
        if(layer->type=="InnerProduct") {
            const ncnn::InnerProduct_vulkan* inner=static_cast<const ncnn::InnerProduct_vulkan*>(layer);
            CHECK(!inner->weight_data_gpu.empty() && !inner->bias_data_gpu.empty());
            CHECK(inner->pipeline_innerproduct_sum8 && inner->pipeline_innerproduct_reduce_sum8);
            CHECK(!inner->pipeline_innerproduct_gemm);
            PIPELINE(inner,pipeline_innerproduct);
            PIPELINE(inner,pipeline_innerproduct_sum8);
            PIPELINE(inner,pipeline_innerproduct_reduce_sum8);
            CHECK(inner->flatten && inner->flatten->support_vulkan);
            const ncnn::Flatten_vulkan* flatten=static_cast<const ncnn::Flatten_vulkan*>(inner->flatten);
            PIPELINE(flatten,pipeline_flatten);
            PIPELINE(flatten,pipeline_flatten_pack4);
            PIPELINE(flatten,pipeline_flatten_pack1to4);
#if NCNN_INT8
            CHECK(!inner->quantize && !inner->pipeline_innerproduct_int8 && !inner->pipeline_innerproduct_gemm_int8);
            CHECK(!inner->pipeline_innerproduct_sum8_int8 && !inner->pipeline_innerproduct_reduce_sum8_int8);
            CHECK(!flatten->pipeline_flatten_int8 && !flatten->pipeline_flatten_pack4_int8 && !flatten->pipeline_flatten_pack1to4_int8);
#endif
        }
    }
    return 0;
}
#undef PIPELINE

#endif
int compare(const ncnn::Mat& output,const std::string& dir,bool style,bool vulkan,bool cpu_packed,bool fp16_packed,int threads,double seconds,const CoreShape& shape) {
    const size_t count=style?1028:shape.values;
    CHECK(output.elempack==1 && output.elemsize==4u && output.d==1 && output.c==1);
    if(style) CHECK(output.dims==1 && output.w==1028 && output.h==1);
    else CHECK(output.dims==2 && output.w==shape.frames && output.h==22);
    CHECK(output.total()>=count); // total() includes storage alignment.
    std::vector<float> reference(count);
    CHECK(read_floats(dir+"/reference.f32",reference.data(),count));
    const double atol=style?2e-5:0.002,rtol=style?2e-5:0.002;
    double error_power=0,reference_power=0,max_error=0;
    size_t failed=0;
    const float* values=static_cast<const float*>(output.data);
    for(size_t i=0;i<count;++i) {
        double error=std::fabs(static_cast<double>(values[i])-reference[i]);
        error_power+=error*error;
        reference_power+=static_cast<double>(reference[i])*reference[i];
        if(error>max_error) max_error=error;
        if(!std::isfinite(values[i]) || !std::isfinite(reference[i]) || error>atol+rtol*std::fabs(reference[i])) {
            if(failed<5) std::fprintf(stderr,"mismatch %zu got %.9g ref %.9g\n",i,values[i],reference[i]);
            ++failed;
        }
    }
    std::printf("ncnn Kokoro %s %s: %.3f s, %zu outputs, max_abs %.9g, relative_rms %.9g, mismatches %zu, atol=%g rtol=%g, packing=%s threads=%d\n",
                style?"style projection":"neural core",vulkan?(fp16_packed?"Vulkan FP16-packed storage / FP32 arithmetic (fallback paths available in trace)":"Vulkan (fallback paths available in trace)"):cpu_packed?"CPU packed":"CPU",
                seconds,count,max_error,std::sqrt(error_power/reference_power),failed,atol,rtol,vulkan || cpu_packed?"on":"off",threads);
    CHECK(failed==0);
    std::puts(style?"PASS: real Kokoro style projection, full FP32-reference output comparison"
                   :"PASS: Kokoro neural-core runtime inputs, full FP32-reference output comparison; source/iSTFT outside graph");
    return 0;
}
} // namespace

int main(int argc,char** argv) {
    std::setvbuf(stdout,0,_IONBF,0);
    bool vulkan=false,style=false,load_only=false,cpu_packed=false,fp16_packed=false;
    int repeat=1,threads=4;
    std::string shapes_path;
    std::vector<std::string> positional;
    for(int i=1;i<argc;++i) {
        if(!std::strcmp(argv[i],"--vulkan")) vulkan=true;
        else if(!std::strcmp(argv[i],"--fp16-packed")) fp16_packed=true;
        else if(!std::strcmp(argv[i],"--cpu-packed")) cpu_packed=true;
        else if(!std::strcmp(argv[i],"--style")) style=true;
        else if(!std::strcmp(argv[i],"--load-only")) load_only=true;
        else if(!std::strcmp(argv[i],"--capture-shapes")) {
            if(++i==argc || !shapes_path.empty()) { std::fprintf(stderr,"--capture-shapes requires one new output path\n"); return 1; }
            shapes_path=argv[i];
        }
        else if(!std::strcmp(argv[i],"--repeat")) {
            if(++i==argc) { std::fprintf(stderr,"--repeat requires an integer from 1 to 10\n"); return 1; }
            char* end=0;
            long value=std::strtol(argv[i],&end,10);
            if(end==argv[i] || *end || value<1 || value>10) {
                std::fprintf(stderr,"--repeat requires an integer from 1 to 10\n");
                return 1;
            }
            repeat=static_cast<int>(value);
        }
        else if(!std::strcmp(argv[i],"--threads")) {
            if(++i==argc) { std::fprintf(stderr,"--threads requires an integer from 1 to 6\n"); return 1; }
            char* end=0;
            long value=std::strtol(argv[i],&end,10);
            if(end==argv[i] || *end || value<1 || value>6) {
                std::fprintf(stderr,"--threads requires an integer from 1 to 6\n");
                return 1;
            }
            threads=static_cast<int>(value);
        }
        else if(std::strncmp(argv[i],"--",2)) positional.push_back(argv[i]);
        else { std::fprintf(stderr,"unknown option: %s\n",argv[i]); return 1; }
    }
    if(cpu_packed && (vulkan || !shapes_path.empty())) {
        std::fprintf(stderr,"--cpu-packed cannot be combined with --vulkan or --capture-shapes\n");
        return 1;
    }
    if(fp16_packed && (!vulkan || cpu_packed || !shapes_path.empty())) {
        std::fprintf(stderr,"--fp16-packed requires --vulkan and cannot be combined with --cpu-packed or --capture-shapes\n");
        return 1;
    }
    if(positional.empty() || positional.size()>2 || (load_only && positional.size()==2)
       || (repeat>1 && (load_only || positional.size()==2))
       || (!shapes_path.empty() && (vulkan || style || load_only || repeat!=1 || positional.size()!=1))) {
        std::fprintf(stderr,"Usage: %s [--vulkan [--fp16-packed] | --cpu-packed] [--threads 1..6] [--style] [--load-only] [--repeat 1..10] [--capture-shapes NEW_FILE] DIR [BLOB]\nRepeated runs require inference with full reference comparison; shape capture requires one full-core scalar CPU run.\n",argv[0]);
        return 1;
    }
    const std::string dir=positional[0];
    const bool dump=positional.size()==2;
    const std::string target=dump?positional[1]:style?"68":"out0";
    CoreShape shape;
    CHECK(read_core_shape(dir,style,shape)==0);
    if(!dump && !load_only) CHECK(check_float_file(dir+"/reference.f32",style?1028:shape.values));
#if !NCNN_VULKAN
    if(vulkan) {
        std::fprintf(stderr,"--vulkan requires an ncnn build with NCNN_VULKAN=ON\n");
        return 1;
    }
#endif
    const auto load_start=Clock::now();
#if NCNN_VULKAN
    GpuInstance instance;
    const ncnn::VulkanDevice* device=0;
    if(vulkan) {
        CHECK(ncnn::create_gpu_instance()==0);
        instance.active=true;
        CHECK(ncnn::get_gpu_count()==1);
        device=ncnn::get_gpu_device(0);
        CHECK(device && device->is_valid());
        std::printf("ncnn device: %s\n",device->info.device_name());
    }
#endif
    ncnn::Net net;
    configure_options(net.opt,vulkan,cpu_packed,fp16_packed,threads);
    std::printf("execution configuration: %s, %s, packing=%s threads=%d\n",vulkan?"Vulkan":"CPU",fp16_packed?"FP16-packed storage / FP32 arithmetic":"FP32",net.opt.use_packing_layout?"on":"off",net.opt.num_threads);
#if NCNN_VULKAN
    if(vulkan) net.set_vulkan_device(device);
#endif
    CHECK(net.load_param((dir+"/decoder.ncnn.param").c_str())==0);
    CHECK(net.opt.use_vulkan_compute==vulkan); // Reject silent device fallback.
    CHECK(net.opt.use_fp16_packed==fp16_packed && !net.opt.use_fp16_storage && !net.opt.use_fp16_arithmetic && !net.opt.use_fp16_uniform);
    CHECK(check_shape_hints(net,style,shape)==0);
    CHECK(set_shape_hints(net,style,vulkan)==0);
    size_t supported=0,cpu=0,inputs_count=0;
    for(const ncnn::Layer* layer:net.layers()) {
        if(layer->type=="Input") ++inputs_count;
        else if(vulkan && layer->support_vulkan) ++supported;
        else ++cpu;
    }
    std::printf("loaded graph: %zu GPU-capable, %zu CPU, %zu Input layers; inventory is not an execution trace\n",supported,cpu,inputs_count);
    CHECK(net.load_model((dir+"/decoder.ncnn.bin").c_str())==0);
#if NCNN_VULKAN
    if(vulkan && style) CHECK(check_style_pipelines(net)==0);
    if(vulkan) {
        CHECK(net.opt.pipeline_cache);
        std::printf("model pipeline cache after load: %zu artifacts\n",net.opt.pipeline_cache->size());
    }
#endif
    double load_seconds=std::chrono::duration<double>(Clock::now()-load_start).count();
    std::printf("cold load: %.3f s%s\n",load_seconds,vulkan?" including device setup, pipelines, and completed model upload":" including CPU pipelines");
    print_peak_rss("after load");
    if(load_only) {
        std::puts(vulkan?"PASS: model load and GPU weight upload completed; no inference run":"PASS: CPU model load completed; no inference run");
        return 0;
    }
    const char* files[]={"asr","f0","n","style","har"};
    const int widths[]={shape.time,2*shape.time,2*shape.time,256,shape.frames},heights[]={512,1,1,1,22};
    std::vector<ncnn::Mat> inputs;
    std::vector<std::string> names;
    for(int j=0;j<5;++j) {
        if(style && j!=3) continue;
        ncnn::Mat input=heights[j]==1?ncnn::Mat(widths[j]):ncnn::Mat(widths[j],heights[j]);
        CHECK(!input.empty());
        input.fill(0.f);
        CHECK(read_floats(dir+"/"+files[j]+".f32",static_cast<float*>(input.data),static_cast<size_t>(widths[j])*heights[j]));
        inputs.push_back(input);
        names.push_back("in"+std::to_string(j));
    }
    double warm_wall=0,warm_cpu=0;
    std::vector<BlobShape> shapes;
    for(int iteration=0;iteration<repeat;++iteration) {
        struct rusage cpu_before,cpu_after;
        CHECK(getrusage(RUSAGE_SELF,&cpu_before)==0);
        const auto start=Clock::now();
        ncnn::Mat output;
#if NCNN_VULKAN
        if(vulkan) CHECK(extract_gpu(net,inputs,names,target.c_str(),output)==0);
        else
#endif
        {
            ncnn::Extractor ex=net.create_extractor();
            for(size_t i=0;i<inputs.size();++i) CHECK(ex.input(names[i].c_str(),inputs[i])==0);
            if(!shapes_path.empty()) CHECK(capture_shapes(net,ex,shapes)==0);
            CHECK(ex.extract(target.c_str(),output)==0);
        }
        double seconds=std::chrono::duration<double>(Clock::now()-start).count();
        CHECK(getrusage(RUSAGE_SELF,&cpu_after)==0);
        double cpu=cpu_seconds(cpu_after)-cpu_seconds(cpu_before);
        std::printf("iteration %d/%d%s: compute plus input/output transfer wall %.6f s, process CPU %.6f s\n",
                    iteration+1,repeat,repeat>1 && iteration==0?" (first extraction/warmup)":"",seconds,cpu);
        print_peak_rss("after inference");
        CHECK(!output.empty() && output.elempack==1 && output.elemsize==4u && output.dims<=2);
        size_t count=static_cast<size_t>(output.w)*output.h;
        if(dump) {
            CHECK(write_floats(dir+(fp16_packed?"/ncnn-vulkan-fp16-packed-blob-":"/ncnn-blob-")+target+".f32",static_cast<const float*>(output.data),count));
            std::printf("blob %s: dims %d, w %d h %d, %.3f s\n",target.c_str(),output.dims,output.w,output.h,seconds);
            return 0;
        }
        const std::string prefix=dir+(vulkan?(fp16_packed?"/ncnn-vulkan-fp16-packed":"/ncnn-vulkan"):cpu_packed?"/ncnn-cpu-packed":"/ncnn-cpu");
        CHECK(write_floats(prefix+"-candidate.f32",static_cast<const float*>(output.data),count));
        std::printf("saved scalar candidate: %s-candidate.f32\n",prefix.c_str());
        CHECK(compare(output,dir,style,vulkan,cpu_packed,fp16_packed,threads,seconds,shape)==0);
        CHECK(write_floats(prefix+"-output.f32",static_cast<const float*>(output.data),count));
        std::printf("saved validated scalar output: %s-output.f32\n",prefix.c_str());
        if(!shapes_path.empty()) {
            CHECK(write_shapes(shapes_path,net,shapes));
            std::printf("saved shapes after full reference comparison: %s\n",shapes_path.c_str());
        }
        if(iteration>0) { warm_wall+=seconds; warm_cpu+=cpu; }
    }
    if(repeat>1)
        std::printf("warm mean over %d validated extractions (first excluded): wall %.6f s, process CPU %.6f s\n",repeat-1,warm_wall/(repeat-1),warm_cpu/(repeat-1));
    return 0;
}
