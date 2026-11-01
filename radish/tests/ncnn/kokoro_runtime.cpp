#include "runtime_helpers.h"
#include "radish_snake.h"
#include "../kokoro_frontend.h"
#include "../kokoro_shapes.h"
#include "../kokoro_waveform.h"
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
void require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}
double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string quote(const std::string& value) {
    std::ostringstream out; out << '"';
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') out << '\\' << c;
        else if(c<32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    }
    out << '"'; return out.str();
}
size_t file_size(const std::string& path) {
    struct stat info;
    require(stat(path.c_str(),&info)==0 && S_ISREG(info.st_mode) && info.st_size>=0,
            "missing or invalid regular file: "+path);
    require(static_cast<uintmax_t>(info.st_size)<=std::numeric_limits<size_t>::max(),"oversized file: "+path);
    return static_cast<size_t>(info.st_size);
}
std::string read_param(const std::string& path) {
    size_t bytes=file_size(path);
    require(bytes>0 && bytes<1024*1024,"unexpected parameter size");
    std::ifstream input(path.c_str(),std::ios::binary);
    std::string value(bytes,'\0');
    input.read(&value[0],bytes);
    require(bool(input) && input.peek()==EOF && value.find('\0')==std::string::npos,"cannot read complete text parameters");
    return value;
}
void remove_stage(const std::string& path) {
    DIR* dir=opendir(path.c_str());
    if(!dir) return;
    while(dirent* entry=readdir(dir)) {
        if(!std::strcmp(entry->d_name,".") || !std::strcmp(entry->d_name,"..")) continue;
        std::string name=path+"/"+entry->d_name;
        struct stat info;
        if(lstat(name.c_str(),&info)==0 && S_ISDIR(info.st_mode)) remove_stage(name);
        else unlink(name.c_str());
    }
    closedir(dir); rmdir(path.c_str());
}
struct Output {
    std::string stage,destination;
    explicit Output(const std::string& output):destination(output) {
        require(!output.empty() && output.back()!='/',"output must name a new directory");
        struct stat info;
        require(lstat(output.c_str(),&info)!=0 && errno==ENOENT,"output already exists or cannot be inspected");
        size_t slash=output.find_last_of('/');
        std::string parent=slash==std::string::npos?".":slash==0?"/":output.substr(0,slash);
        std::string pattern=parent+"/.kokoro-runtime-XXXXXX";
        std::vector<char> buffer(pattern.begin(),pattern.end()); buffer.push_back('\0');
        char* made=mkdtemp(buffer.data());
        require(made!=nullptr,"cannot create output staging directory"); stage=made;
    }
    ~Output() { if(!stage.empty()) remove_stage(stage); }
    void write(const char* name,const void* data,size_t bytes) {
        std::string path=stage+"/"+name;
        int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
        require(fd>=0,"cannot create output: "+path);
        const char* cursor=static_cast<const char*>(data);
        while(bytes) {
            ssize_t count=::write(fd,cursor,bytes);
            if(count<0 && errno==EINTR) continue;
            if(count<=0) { close(fd); throw std::runtime_error("cannot write output: "+path); }
            cursor+=count; bytes-=static_cast<size_t>(count);
        }
        require(close(fd)==0,"cannot close output: "+path);
    }
    void publish() {
        require(syscall(SYS_renameat2,AT_FDCWD,stage.c_str(),AT_FDCWD,destination.c_str(),1)==0,
                "cannot publish new output (renameat2 NOREPLACE required)");
        stage.clear();
    }
};
int integer(const char* text,int low,int high) {
    char* end=nullptr; errno=0; long result=std::strtol(text,&end,10);
    require(!errno && end!=text && !*end && result>=low && result<=high,"integer option outside bounds");
    return static_cast<int>(result);
}
void check_boundary(const radish_frontend::Boundary& b) {
    require(b.T>0 && b.T<=4096 && b.frames==120*b.T+1 && b.samples==600*b.T,"invalid frontend dimensions");
    const std::vector<float>* values[]={&b.asr,&b.f0,&b.n,&b.style,&b.har};
    const size_t counts[]={static_cast<size_t>(512*b.T),static_cast<size_t>(2*b.T),static_cast<size_t>(2*b.T),256,static_cast<size_t>(22*b.frames)};
    for(size_t i=0;i<5;++i) {
        require(values[i]->size()==counts[i],"frontend boundary length mismatch");
        for(float value:*values[i]) require(std::isfinite(value),"nonfinite frontend value");
    }
}
struct CoreResult {
    std::vector<float> values;
    std::vector<double> extractions;
    double load_seconds=0;
};
CoreResult run_core(const radish_frontend::Boundary& boundary,const radish::KokoroShapePlan& plan,
                    const std::string& weights,bool vulkan,int threads,int repeat) {
    CoreResult result;
    double load_start=now();
#if NCNN_VULKAN
    radish_ncnn::GpuInstance instance;
    const ncnn::VulkanDevice* device=nullptr;
    if(vulkan) {
        require(ncnn::create_gpu_instance()==0,"cannot create GPU instance"); instance.active=true;
        require(ncnn::get_gpu_count()==1,"expected one private Radish GPU");
        device=ncnn::get_gpu_device(0);
        require(device && device->is_valid(),"invalid GPU device");
        require(std::strstr(device->info.device_name(),"Radish")!=nullptr,"expected Radish device");
        std::printf("ncnn device: %s\n",device->info.device_name());
    }
#else
    require(!vulkan,"runtime was built without Vulkan");
#endif
    radish_ncnn::SnakeContext snake;
    ncnn::Net net;
    radish_ncnn::configure_options(net.opt,vulkan,!vulkan,false,threads);
#if NCNN_VULKAN
    if(vulkan) net.set_vulkan_device(device);
#endif
    require(snake.register_layer(net)==0,"cannot register RadishSnake");
    require(net.load_param_mem(plan.param_text.c_str())==0,"cannot load inferred graph");
    require(net.opt.use_vulkan_compute==vulkan,"silent Vulkan device fallback");
    require(!net.opt.use_fp16_packed && !net.opt.use_fp16_storage && !net.opt.use_fp16_arithmetic,"unexpected precision change");
    require(net.layers().size()==static_cast<size_t>(plan.layers) && net.blobs().size()==static_cast<size_t>(plan.blobs),"loaded graph inventory mismatch");
    for(const radish::KokoroShapeRecord& record:plan.shapes) {
        const ncnn::Blob& blob=net.blobs().at(record.blob);
        const ncnn::Mat& shape=blob.shape;
        require(blob.name==record.name && shape.dims==record.shape.dims && shape.w==record.shape.w && shape.h==record.shape.h
                && shape.d==record.shape.d && shape.c==record.shape.c && shape.elempack==1 && shape.elemsize==4u,"loaded logical shape mismatch: "+record.name);
    }
    require(radish_ncnn::set_shape_hints(net,false,vulkan)==0,"style projection shape failure");
    require(net.load_model(weights.c_str())==0,"cannot load core weights/pipelines");
    result.load_seconds=now()-load_start;
    std::printf("core cold load: %.6f s\n",result.load_seconds);
    const std::vector<float>* arrays[]={&boundary.asr,&boundary.f0,&boundary.n,&boundary.style,&boundary.har};
    const int widths[]={int(boundary.T),int(2*boundary.T),int(2*boundary.T),256,int(boundary.frames)};
    const int heights[]={512,1,1,1,22};
    std::vector<ncnn::Mat> inputs; std::vector<std::string> names;
    for(size_t i=0;i<5;++i) {
        ncnn::Mat input=heights[i]==1?ncnn::Mat(widths[i]):ncnn::Mat(widths[i],heights[i]);
        require(!input.empty(),"cannot allocate core input");
        input.fill(0.f); std::memcpy(input.data,arrays[i]->data(),arrays[i]->size()*sizeof(float));
        inputs.push_back(input); names.push_back("in"+std::to_string(i));
    }
    for(int iteration=0;iteration<repeat;++iteration) {
        double start=now(); ncnn::Mat output;
#if NCNN_VULKAN
        if(vulkan) require(radish_ncnn::extract_gpu(net,inputs,names,"out0",output)==0,"GPU extraction failed");
        else
#endif
        {
            ncnn::Extractor extractor=net.create_extractor();
            for(size_t i=0;i<inputs.size();++i) require(extractor.input(names[i].c_str(),inputs[i])==0,"CPU input failure");
            require(extractor.extract("out0",output)==0,"CPU extraction failed");
        }
        double seconds=now()-start;
        require(!output.empty() && output.dims==2 && output.w==boundary.frames && output.h==22 && output.c==1 && output.d==1
                && output.elempack==1 && output.elemsize==4u,"invalid scalar core output shape");
        const float* values=static_cast<const float*>(output.data);
        size_t count=static_cast<size_t>(boundary.frames)*22;
        require(output.total()>=count,"short core output allocation");
        for(size_t i=0;i<count;++i) require(std::isfinite(values[i]),"nonfinite core output");
        result.values.assign(values,values+count); result.extractions.push_back(seconds);
        std::printf("core extraction %d/%d (%s): %.6f s including transfers\n",iteration+1,repeat,iteration?"loaded-core repeat":"first",seconds);
    }
    return result;
}
}

int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try {
        double start=now();
        bool vulkan=true,backend_selected=false;
        int threads=4,ort_threads=4,max_T=512,repeat=1;
        std::string ort_library="/usr/local/lib/straylight-listen/libonnxruntime.so";
        std::vector<std::string> args;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--vulkan" || arg=="--cpu-packed") {
                require(!backend_selected,"select one backend"); backend_selected=true; vulkan=arg=="--vulkan";
            } else if(arg=="--threads" || arg=="--ort-threads" || arg=="--max-frames" || arg=="--repeat" || arg=="--ort-lib") {
                require(++i<argc,"missing option value");
                if(arg=="--ort-lib") ort_library=argv[i];
                else if(arg=="--threads") threads=integer(argv[i],1,6);
                else if(arg=="--ort-threads") ort_threads=integer(argv[i],1,6);
                else if(arg=="--max-frames") max_T=integer(argv[i],1,4096);
                else repeat=integer(argv[i],1,10);
            } else { require(arg.compare(0,2,"--")!=0,"unknown option: "+arg); args.push_back(arg); }
        }
        require(args.size()==6,"usage: radish-kokoro-runtime [--vulkan|--cpu-packed] [--threads N] [--ort-threads N] [--max-frames N] [--repeat N] [--ort-lib PATH] MODELS INPUT_DIR PARAM WEIGHTS TAIL NEW_OUTPUT; use the staging wrapper to verify artifact hashes");
        const std::string parameters=read_param(args[2]);
        require(file_size(args[3])==213042160,"unexpected pinned core weight length");
        radish_waveform::Tail tail=radish_waveform::load_tail(args[4]);
        Output output(args[5]);
        double front_start=now();
        radish_frontend::Boundary boundary=radish_frontend::generate_frontend(ort_library,args[0]+"/frontend-only.onnx",args[0]+"/harmonic-source.onnx",args[1],max_T,ort_threads);
        double frontend_wall=now()-front_start;
        check_boundary(boundary);
        double shapes_start=now();
        radish::KokoroShapePlan plan=radish::infer_kokoro_shapes(parameters,static_cast<int>(boundary.T));
        double shapes_wall=now()-shapes_start;
        require((plan.layers==1098 && plan.blobs==1307) || (plan.layers==906 && plan.blobs==1067),
                "integrated runtime requires the InstanceNorm graph or explicit RadishSnake candidate");
        std::printf("frontend complete including ORT release: %.6f s; T=%lld; metadata-only shapes: %d blobs in %.6f s\n",frontend_wall,(long long)boundary.T,plan.blobs,shapes_wall);
        double core_start=now();
        CoreResult core=run_core(boundary,plan,args[3],vulkan,threads,repeat);
        double core_wall=now()-core_start;
        double tail_start=now();
        std::vector<float> audio=radish_waveform::synthesize(core.values.data(),core.values.size(),static_cast<size_t>(boundary.frames),tail);
        double tail_wall=now()-tail_start;
        require(audio.size()==static_cast<size_t>(boundary.samples),"unexpected waveform length");
        double synthesis_wall=now()-start;
        radish_frontend::export_boundary(boundary,output.stage+"/boundary");
        output.write("core.f32",core.values.data(),core.values.size()*sizeof(float));
        output.write("waveform.f32",audio.data(),audio.size()*sizeof(float));
        std::ostringstream manifest;
        manifest << std::setprecision(17) << "{\n  \"format\": \"radish-kokoro-runtime-v1\",\n  \"backend\": " << quote(vulkan?"vulkan":"cpu-packed")
                 << ",\n  \"accuracy\": \"unreferenced generation; validate the captured boundary separately\",\n  \"T\": " << boundary.T
                 << ",\n  \"frames\": " << boundary.frames << ",\n  \"samples\": " << boundary.samples << ",\n  \"sample_rate\": 24000,\n  \"threads\": " << threads
                 << ",\n  \"ort_threads\": " << ort_threads << ",\n  \"repeat\": " << repeat << ",\n  \"ort_version\": " << quote(boundary.ort_version)
                 << ",\n  \"frontend_wall_seconds\": " << frontend_wall << ",\n  \"shape_seconds\": " << shapes_wall
                 << ",\n  \"shape_blobs\": " << plan.blobs << ",\n  \"core_load_seconds\": " << core.load_seconds << ",\n  \"core_scope_seconds\": " << core_wall
                 << ",\n  \"tail_seconds\": " << tail_wall << ",\n  \"synthesis_seconds\": " << synthesis_wall << ",\n  \"extraction_seconds\": [";
        for(size_t i=0;i<core.extractions.size();++i) { if(i) manifest << ", "; manifest << core.extractions[i]; }
        manifest << "],\n  \"environment\": {";
        const char* env_names[]={"RADISH_NCNN_TILED","RADISH_NCNN_TILED32","RADISH_NCNN_PLANAR","RADISH_NCNN_SHARED32","RADISH_NCNN_MIXED_STORAGE","RADISH_NCNN_PIPELINE_CACHE","RADISH_NCNN_TRACE","RADISH_PROFILE","RADISH_BATCH_CHUNKS","RADISH_TILED_BATCH_CHUNKS","RADISH_TILED_GROUPS_X","RADISH_SHARED_GROUPS","RADISH_NCNN_RESHAPE_ALIAS","VK_DRIVER_FILES"};
        for(size_t i=0;i<sizeof(env_names)/sizeof(env_names[0]);++i) {
            if(i) manifest << ", ";
            const char* value=std::getenv(env_names[i]);
            manifest << quote(env_names[i]) << ": " << quote(value?value:"");
        }
        manifest << "},\n  \"timing_scope\": \"synthesis includes frontend creation/destruction, metadata, core loading/all extractions/destruction, and one tail; excludes output publication. Loaded-core repeats reuse the same boundary.\"\n}\n";
        std::string json=manifest.str(); output.write("runtime.json",json.data(),json.size()); output.publish();
        std::printf("generated %zu samples at 24000 Hz; synthesis %.6f s; full runtime through publication %.6f s; no reference accuracy claim\n",audio.size(),synthesis_wall,now()-start);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}
