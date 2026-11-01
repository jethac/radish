#include "runtime_helpers.h"
#include "radish_snake.h"
#include "radish_affine_snake.h"
#if NCNN_VULKAN
#include "pipelinecache.h"
#endif
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
#include <memory>
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
    std::vector<double> extractions,workspace_trim;
    double load_seconds=0;
};
struct LoopPipelineCache {
#if NCNN_VULKAN
    radish_ncnn::GpuInstance instance;
    const ncnn::VulkanDevice* device=nullptr;
    std::unique_ptr<ncnn::PipelineCache> cache;
    void initialize() {
        if(cache) return;
        require(ncnn::create_gpu_instance()==0,"cannot create shared GPU instance"); instance.active=true;
        require(ncnn::get_gpu_count()==1,"expected one private Radish GPU");
        device=ncnn::get_gpu_device(0);
        require(device && device->is_valid(),"invalid shared GPU device");
        require(std::strstr(device->info.device_name(),"Radish")!=nullptr,"expected Radish device");
        require(!device->info.bug_corrupted_online_pipeline_cache(),"shared pipeline cache lookup is disabled");
        cache.reset(new ncnn::PipelineCache(device));
    }
#endif
};
class CoreSession {
    double load_start=now();
    bool vulkan,resident;
    std::string parameters;
#if NCNN_VULKAN
    radish_ncnn::GpuInstance instance;
    const ncnn::VulkanDevice* device=nullptr;
#endif
    radish_ncnn::SnakeContext snake;
    radish_ncnn::AffineSnakeContext affine_snake;
    ncnn::Net net;
public:
    double load_seconds=0;
    CoreSession(const radish::KokoroShapePlan& plan,const std::string& weights,
                bool use_vulkan,int threads,bool request_loop,LoopPipelineCache* shared=nullptr):vulkan(use_vulkan),resident(request_loop),parameters(plan.param_text) {
#if NCNN_VULKAN
    if(vulkan) {
        if(shared) {
            shared->initialize(); device=shared->device;
        } else {
            require(ncnn::create_gpu_instance()==0,"cannot create GPU instance"); instance.active=true;
            require(ncnn::get_gpu_count()==1,"expected one private Radish GPU");
            device=ncnn::get_gpu_device(0);
            require(device && device->is_valid(),"invalid GPU device");
            require(std::strstr(device->info.device_name(),"Radish")!=nullptr,"expected Radish device");
        }
        std::printf("ncnn device: %s\n",device->info.device_name());
    }
#else
    require(!vulkan && !shared,"runtime was built without Vulkan");
#endif
    radish_ncnn::configure_options(net.opt,vulkan,!vulkan,false,threads);
    if(resident) net.opt.use_local_pool_allocator=false;
#if NCNN_VULKAN
    if(vulkan) {
        net.set_vulkan_device(device);
        if(shared) net.opt.pipeline_cache=shared->cache.get();
    }
#endif
    require(snake.register_layer(net)==0,"cannot register RadishSnake");
    require(affine_snake.register_layer(net)==0,"cannot register RadishAffineSnake");
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
    load_seconds=now()-load_start;
    std::printf("core cold load: %.6f s\n",load_seconds);
    }
    bool matches(const radish::KokoroShapePlan& plan) const { return parameters==plan.param_text; }
    CoreResult extract(const radish_frontend::Boundary& boundary,int repeat) {
    CoreResult result;
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
        double start=now(),trim_seconds=0; ncnn::Mat output;
#if NCNN_VULKAN
        if(vulkan) require(radish_ncnn::extract_gpu(net,inputs,names,"out0",output,resident,&trim_seconds)==0,"GPU extraction failed");
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
        result.values.assign(values,values+count); result.extractions.push_back(seconds); result.workspace_trim.push_back(trim_seconds);
        std::printf("core extraction %d/%d (%s): %.6f s including transfers\n",iteration+1,repeat,iteration?"loaded-core repeat":"first",seconds);
    }
    return result;
}
};
CoreResult run_core(const radish_frontend::Boundary& boundary,const radish::KokoroShapePlan& plan,
                    const std::string& weights,bool vulkan,int threads,int repeat) {
    CoreSession session(plan,weights,vulkan,threads,false);
    CoreResult result=session.extract(boundary,repeat);
    result.load_seconds=session.load_seconds;
    return result;
}
void publish_runtime(Output& output,const radish_frontend::Boundary& boundary,const CoreResult& core,
                     bool vulkan,int threads,int ort_threads,int repeat,const char* core_graph,
                     double frontend_wall,double shapes_wall,int shape_blobs,double core_wall,
                     double tail_wall,double synthesis_wall,bool resident=false,size_t request_index=0,bool cache_hit=false,bool reuse_pipeline_cache=false) {
        std::ostringstream manifest;
        manifest << std::setprecision(17) << "{\n  \"format\": \"radish-kokoro-runtime-v1\",\n  \"backend\": " << quote(vulkan?"vulkan":"cpu-packed")
                 << ",\n  \"core_graph\": " << quote(core_graph)
                 << ",\n  \"reuse_pipeline_cache\": " << (reuse_pipeline_cache?"true":"false")
                 << ",\n  \"accuracy\": \"unreferenced generation; validate the captured boundary separately\",\n  \"T\": " << boundary.T
                 << ",\n  \"frames\": " << boundary.frames << ",\n  \"samples\": " << boundary.samples << ",\n  \"sample_rate\": 24000,\n  \"threads\": " << threads
                 << ",\n  \"ort_threads\": " << ort_threads << ",\n  \"repeat\": " << repeat << ",\n  \"ort_version\": " << quote(boundary.ort_version)
                 << ",\n  \"frontend_wall_seconds\": " << frontend_wall << ",\n  \"shape_seconds\": " << shapes_wall
                 << ",\n  \"shape_blobs\": " << shape_blobs << ",\n  \"core_load_seconds\": " << core.load_seconds << ",\n  \"core_scope_seconds\": " << core_wall
                 << ",\n  \"tail_seconds\": " << tail_wall << ",\n  \"synthesis_seconds\": " << synthesis_wall << ",\n  \"extraction_seconds\": [";
        for(size_t i=0;i<core.extractions.size();++i) { if(i) manifest << ", "; manifest << core.extractions[i]; }
        manifest << "],\n  \"environment\": {";
        const char* env_names[]={"RADISH_NCNN_TILED","RADISH_NCNN_TILED32","RADISH_NCNN_PLANAR","RADISH_NCNN_NORM_SUM4","RADISH_NCNN_PAD_CHANNELS","RADISH_NCNN_SHARED32","RADISH_NCNN_MIXED_STORAGE","RADISH_NCNN_PIPELINE_CACHE","RADISH_NCNN_TRACE","RADISH_PROFILE","RADISH_BATCH_CHUNKS","RADISH_TILED_BATCH_CHUNKS","RADISH_TILED_GROUPS_X","RADISH_SHARED_GROUPS","RADISH_NCNN_RESHAPE_ALIAS","VK_DRIVER_FILES"};
        for(size_t i=0;i<sizeof(env_names)/sizeof(env_names[0]);++i) {
            if(i) manifest << ", ";
            const char* value=std::getenv(env_names[i]);
            manifest << quote(env_names[i]) << ": " << quote(value?value:"");
        }
        manifest << "}";
        if(resident) {
            manifest << ",\n  \"request_index\": " << request_index << ",\n  \"core_cache_hit\": " << (cache_hit?"true":"false")
                     << ",\n  \"workspace_trim_seconds\": [";
            for(size_t i=0;i<core.workspace_trim.size();++i) { if(i) manifest << ", "; manifest << core.workspace_trim[i]; }
            manifest << "]";
        }
        manifest << ",\n  \"timing_scope\": " << quote(resident
            ? "Complete request: fresh frontend/source, metadata, core cache miss load or retained Net extraction, and one tail; extraction includes completed uploads/downloads and allocator trim. Core scope includes any previous Net eviction. Excludes output publication and final session Net destruction."
            : "synthesis includes frontend creation/destruction, metadata, core loading/all extractions/destruction, and one tail; excludes output publication. Loaded-core repeats reuse the same boundary.") << "\n}\n";
        std::string json=manifest.str(); output.write("runtime.json",json.data(),json.size()); output.publish();

}

struct Cancelled : std::runtime_error {
    explicit Cancelled(const std::string& phase):std::runtime_error(phase) {}
};
void check_cancel(const std::string& marker,const std::string& phase) {
    if(marker.empty()) return;
    struct stat info;
    if(lstat(marker.c_str(),&info)==0) {
        require(S_ISREG(info.st_mode),"cancellation marker must be a regular file");
        throw Cancelled(phase);
    }
    require(errno==ENOENT,"cannot inspect cancellation marker");
}
std::string request_name(size_t index) {
    std::ostringstream out; out << "request-" << std::setw(3) << std::setfill('0') << index;
    return out.str();
}
struct MemorySample {
    std::string phase;
    size_t request;
    double seconds;
    long rss=0,hwm=0,swap=0;
};
MemorySample memory_sample(const char* phase,size_t request,double start) {
    MemorySample result; result.phase=phase; result.request=request; result.seconds=now()-start;
    std::ifstream input("/proc/self/status");
    require(bool(input),"cannot sample process memory");
    std::string line;
    while(std::getline(input,line)) {
        if(line.compare(0,6,"VmRSS:")==0) { std::istringstream fields(line.substr(6)); fields >> result.rss; }
        else if(line.compare(0,6,"VmHWM:")==0) { std::istringstream fields(line.substr(6)); fields >> result.hwm; }
        else if(line.compare(0,7,"VmSwap:")==0) { std::istringstream fields(line.substr(7)); fields >> result.swap; }
    }
    require(!input.bad() && result.rss>0 && result.hwm>=result.rss && result.swap>=0,"invalid process memory sample");
    std::printf("request %zu phase %s: RSS=%ld KiB HWM=%ld KiB swap=%ld KiB\n",request,phase,result.rss,result.hwm,result.swap);
    return result;
}
void atomic_session_manifest(const std::string& directory,const std::string& data) {
    std::string pattern=directory+"/.session-XXXXXX";
    std::vector<char> temporary(pattern.begin(),pattern.end()); temporary.push_back('\0');
    int fd=mkstemp(temporary.data());
    require(fd>=0,"cannot create session manifest staging file");
    try {
        const char* cursor=data.data(); size_t remaining=data.size();
        while(remaining) {
            ssize_t count=::write(fd,cursor,remaining);
            if(count<0 && errno==EINTR) continue;
            require(count>0,"cannot write session manifest");
            cursor+=count; remaining-=static_cast<size_t>(count);
        }
        require(fsync(fd)==0,"cannot flush session manifest");
        int result=close(fd); fd=-1; require(result==0,"cannot close session manifest");
        require(rename(temporary.data(),(directory+"/session.json").c_str())==0,"cannot publish session manifest");
    } catch(...) {
        if(fd>=0) close(fd);
        unlink(temporary.data());
        throw;
    }
}
struct RequestRecord {
    size_t index;
    std::string input;
    int64_t T;
    bool cache_hit;
    double load,synthesis,publication,eviction;
    size_t pipeline_cache_entries,device_pipeline_cache_entries;
};
int run_request_loop(const std::vector<std::string>& args,const std::vector<std::string>& queued,
                     const std::string& parameters,const radish_waveform::Tail& tail,
                     const std::string& ort_library,bool vulkan,int threads,int ort_threads,int max_T,
                     int layers,int blobs,const char* core_graph,const std::string& cancel_file,double process_start,bool reuse_pipeline_cache) {
    require(!args[5].empty() && args[5].back()!='/',"session output must name a new directory");
    require(mkdir(args[5].c_str(),0700)==0,"cannot create exclusive session directory");
    std::vector<RequestRecord> completed;
    std::vector<MemorySample> memory;
    std::unique_ptr<LoopPipelineCache> shared;
    std::unique_ptr<CoreSession> session;
    std::string phase="before_request";
    size_t active=0;
    double cleanup=0,cancellation_observed=-1;
    auto manifest=[&](const char* status,const std::string& error) {
        std::ostringstream out;
        out << std::setprecision(17) << "{\n  \"format\": \"radish-kokoro-runtime-session-v1\",\n  \"status\": " << quote(status)
            << ",\n  \"backend\": " << quote(vulkan?"vulkan":"cpu-packed") << ",\n  \"core_graph\": " << quote(core_graph)
            << ",\n  \"reuse_pipeline_cache\": " << (reuse_pipeline_cache?"true":"false")
            << ",\n  \"requested_requests\": " << queued.size() << ",\n  \"completed_requests\": " << completed.size()
            << ",\n  \"active_request_index\": " << active << ",\n  \"phase\": " << quote(phase)
            << ",\n  \"error\": " << quote(error) << ",\n  \"session_seconds\": " << now()-process_start
            << ",\n  \"final_core_cleanup_seconds\": " << cleanup << ",\n  \"cancellation_observed_seconds\": ";
        if(cancellation_observed<0) out << "null"; else out << cancellation_observed;
        out << ",\n  \"cancellation_scope\": \"Poll only between completed phases; active frontend/GPU work drains before cancellation is observed.\",\n  \"requests\": [";
        for(size_t i=0;i<completed.size();++i) {
            const RequestRecord& r=completed[i]; if(i) out << ',';
            out << "\n    {\"index\": " << r.index << ", \"directory\": " << quote(request_name(r.index))
                << ", \"input_directory\": " << quote(r.input) << ", \"T\": " << r.T
                << ", \"core_cache_hit\": " << (r.cache_hit?"true":"false") << ", \"core_load_seconds\": " << r.load
                << ", \"synthesis_seconds\": " << r.synthesis << ", \"through_publication_seconds\": " << r.publication
                << ", \"previous_core_eviction_seconds\": " << r.eviction
                << ", \"pipeline_cache_entries\": " << r.pipeline_cache_entries
                << ", \"device_pipeline_cache_entries\": " << r.device_pipeline_cache_entries << '}';
        }
        out << "\n  ],\n  \"memory_samples\": [";
        for(size_t i=0;i<memory.size();++i) {
            const MemorySample& m=memory[i]; if(i) out << ',';
            out << "\n    {\"request_index\": " << m.request << ", \"phase\": " << quote(m.phase) << ", \"seconds\": " << m.seconds
                << ", \"rss_kib\": " << m.rss << ", \"process_high_water_kib\": " << m.hwm << ", \"swap_kib\": " << m.swap << '}';
        }
        out << "\n  ],\n  \"timing_scope\": \"Each request regenerates frontend/source and one tail. A same-shape core remains resident; per-request extraction includes GPU allocator trimming. Request timing excludes publication and final session teardown; session timing includes completed captures and final teardown. Memory samples are phase observations, not isolated peaks. Accuracy requires separate same-boundary references for every completed request.\"\n}\n";
        atomic_session_manifest(args[5],out.str());
    };
    auto cleanup_core=[&]() { double start=now(); session.reset(); shared.reset(); cleanup=now()-start; };
    auto checkpoint=[&](const char* next) {
        phase=next;
        memory.push_back(memory_sample(next,active,process_start));
        manifest("running","");
        check_cancel(cancel_file,phase);
    };
    try {
        manifest("running","");
        for(active=0;active<queued.size();++active) {
            checkpoint("before_request");
            double request_start=now(),front_start=now();
            radish_frontend::Boundary boundary=radish_frontend::generate_frontend(ort_library,args[0]+"/frontend-only.onnx",args[0]+"/harmonic-source.onnx",queued[active],max_T,ort_threads);
            double frontend_wall=now()-front_start;
            check_boundary(boundary);
            checkpoint("after_frontend");
            double shapes_start=now();
            radish::KokoroShapePlan plan=radish::infer_kokoro_shapes(parameters,static_cast<int>(boundary.T));
            double shapes_wall=now()-shapes_start;
            require(plan.layers==layers && plan.blobs==blobs,"inferred request graph inventory changed");
            checkpoint("after_shapes");
            double core_start=now(),load=0,eviction=0;
            bool hit=session && session->matches(plan);
            if(!hit) {
                double eviction_start=now(); session.reset(); eviction=now()-eviction_start;
                if(reuse_pipeline_cache && !shared) shared.reset(new LoopPipelineCache);
                session.reset(new CoreSession(plan,args[3],vulkan,threads,true,shared.get()));
                load=session->load_seconds;
            }
            std::printf("request %zu core cache %s T=%lld\n",active,hit?"hit":"miss",static_cast<long long>(boundary.T));
            checkpoint("after_core_load");
            CoreResult core=session->extract(boundary,1); core.load_seconds=load;
            double core_wall=now()-core_start;
            size_t pipeline_entries=0,device_pipeline_entries=0;
#if NCNN_VULKAN
            if(shared) {
                pipeline_entries=shared->cache->size();
                device_pipeline_entries=shared->device->get_pipeline_cache()->size();
            }
#endif
            checkpoint("after_core");
            double tail_start=now();
            std::vector<float> audio=radish_waveform::synthesize(core.values.data(),core.values.size(),static_cast<size_t>(boundary.frames),tail);
            double tail_wall=now()-tail_start;
            require(audio.size()==static_cast<size_t>(boundary.samples),"unexpected request waveform length");
            double synthesis_wall=now()-request_start;
            checkpoint("after_tail");
            Output output(args[5]+"/"+request_name(active));
            radish_frontend::export_boundary(boundary,output.stage+"/boundary");
            output.write("core.f32",core.values.data(),core.values.size()*sizeof(float));
            output.write("waveform.f32",audio.data(),audio.size()*sizeof(float));
            checkpoint("before_publication");
            publish_runtime(output,boundary,core,vulkan,threads,ort_threads,1,core_graph,
                            frontend_wall,shapes_wall,plan.blobs,core_wall,tail_wall,synthesis_wall,true,active,hit,reuse_pipeline_cache);
            completed.push_back({active,queued[active],boundary.T,hit,load,synthesis_wall,now()-request_start,eviction,pipeline_entries,device_pipeline_entries});
            phase="request_published";
            manifest("running","");
        }
        phase="complete"; cleanup_core();
        memory.push_back(memory_sample("session_cleanup",active,process_start));
        manifest("complete","");
        std::printf("request loop complete: %zu fresh syntheses in %.6f s including final cleanup; accuracy remains unreferenced\n",completed.size(),now()-process_start);
        return 0;
    } catch(const Cancelled& error) {
        cancellation_observed=now()-process_start;
        phase=error.what(); cleanup_core(); manifest("cancelled","");
        std::fprintf(stderr,"CANCELLED at request %zu phase %s after draining active work\n",active,phase.c_str());
        return 2;
    } catch(const std::exception& error) {
        cleanup_core(); manifest("failed",error.what());
        throw;
    }
}

}

int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try {
        double start=now();
        bool vulkan=true,backend_selected=false,affine_snake=false,deconv2d=false,request_loop=false,reuse_pipeline_cache=false;
        int threads=4,ort_threads=4,max_T=512,repeat=1;
        std::string ort_library="/usr/local/lib/straylight-listen/libonnxruntime.so";
        std::vector<std::string> args,next_inputs;
        std::string cancel_file;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--request-loop") {
                require(!request_loop,"duplicate --request-loop"); request_loop=true;
            } else if(arg=="--reuse-pipeline-cache") {
                require(!reuse_pipeline_cache,"duplicate --reuse-pipeline-cache"); reuse_pipeline_cache=true;
            } else if(arg=="--next-input") {
                require(++i<argc && next_inputs.size()<3,"--next-input requires a directory; maximum three appended requests");
                next_inputs.push_back(argv[i]);
            } else if(arg=="--cancel-file") {
                require(++i<argc && cancel_file.empty(),"--cancel-file requires one unique path"); cancel_file=argv[i];
                require(!cancel_file.empty() && cancel_file.front()=='/',"cancel-file must be an absolute path");
            } else if(arg=="--affine-snake") {
                require(!affine_snake,"duplicate --affine-snake"); affine_snake=true;
            } else if(arg=="--deconv2d") {
                require(!deconv2d,"duplicate --deconv2d"); deconv2d=true;
            } else if(arg=="--vulkan" || arg=="--cpu-packed") {
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
        require(args.size()==6,"usage: radish-kokoro-runtime [--affine-snake] [--deconv2d] [--vulkan|--cpu-packed] [--threads N] [--ort-threads N] [--max-frames N] [--repeat N] [--ort-lib PATH] [--request-loop --next-input DIR ... [--cancel-file PATH] [--reuse-pipeline-cache]] MODELS INPUT_DIR PARAM WEIGHTS TAIL NEW_OUTPUT; use the staging wrapper to verify artifact hashes");
        require(request_loop ? !next_inputs.empty() && repeat==1 : next_inputs.empty() && cancel_file.empty(),
                "request-loop requires one to three --next-input directories and --repeat1; next-input/cancel-file are loop-only");
        if(reuse_pipeline_cache) {
            require(request_loop && vulkan,"--reuse-pipeline-cache requires a Vulkan request loop");
            const char* enabled=std::getenv("RADISH_NCNN_PIPELINE_CACHE");
            require(enabled && std::strcmp(enabled,"1")==0,"--reuse-pipeline-cache requires RADISH_NCNN_PIPELINE_CACHE=1");
        }
        const std::string parameters=read_param(args[2]);
        std::istringstream header(parameters);
        int magic=0,layers=0,blobs=0;
        require(bool(header>>magic>>layers>>blobs) && magic==7767517,"invalid parameter header");
        require(deconv2d ? (affine_snake ? layers==720 && blobs==881 : layers==912 && blobs==1073)
                        : (affine_snake ? layers==714 && blobs==875
                                        : (layers==1098 && blobs==1307) || (layers==906 && blobs==1067)),
                "selected core graph does not match its flags and supported inventory");
        require(file_size(args[3])==213042160,"unexpected pinned core weight length");
        radish_waveform::Tail tail=radish_waveform::load_tail(args[4]);
        if(request_loop) {
            std::vector<std::string> queued(1,args[1]); queued.insert(queued.end(),next_inputs.begin(),next_inputs.end());
            const char* graph=deconv2d ? (affine_snake?"affine-snake-deconv2d":"snake-deconv2d")
                                      : affine_snake?"affine-snake":layers==906?"snake":"instancenorm";
            return run_request_loop(args,queued,parameters,tail,ort_library,vulkan,threads,ort_threads,max_T,
                                    layers,blobs,graph,cancel_file,start,reuse_pipeline_cache);
        }
        Output output(args[5]);
        double front_start=now();
        radish_frontend::Boundary boundary=radish_frontend::generate_frontend(ort_library,args[0]+"/frontend-only.onnx",args[0]+"/harmonic-source.onnx",args[1],max_T,ort_threads);
        double frontend_wall=now()-front_start;
        check_boundary(boundary);
        double shapes_start=now();
        radish::KokoroShapePlan plan=radish::infer_kokoro_shapes(parameters,static_cast<int>(boundary.T));
        double shapes_wall=now()-shapes_start;
        require(plan.layers==layers && plan.blobs==blobs,"inferred core graph inventory changed");
        const char* core_graph=deconv2d ? (affine_snake?"affine-snake-deconv2d":"snake-deconv2d")
                                     : affine_snake?"affine-snake":plan.layers==906?"snake":"instancenorm";
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
        publish_runtime(output,boundary,core,vulkan,threads,ort_threads,repeat,core_graph,
                        frontend_wall,shapes_wall,plan.blobs,core_wall,tail_wall,synthesis_wall);
        std::printf("generated %zu samples at 24000 Hz; synthesis %.6f s; full runtime through publication %.6f s; no reference accuracy claim\n",audio.size(),synthesis_wall,now()-start);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}
