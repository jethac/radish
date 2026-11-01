#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/stat.h>

namespace {
constexpr size_t channels=22,bins=11,fixture_frames=25561,kernel=20,hop=5,trim=10;
constexpr double absolute_tolerance=2e-4,relative_tolerance=2e-3,minimum_snr=50.0;
struct FileCloser { void operator()(FILE* file) const { std::fclose(file); } };
using File=std::unique_ptr<FILE,FileCloser>;
using Clock=std::chrono::steady_clock;
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"requires IEEE FP32");

void require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}
void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
size_t checked_multiply(size_t a,size_t b) {
    require(b==0 || a<=std::numeric_limits<size_t>::max()/b,"runtime tensor size overflow");
    return a*b;
}
size_t file_bytes(const char* path) {
    struct stat info;
    require(stat(path,&info)==0 && S_ISREG(info.st_mode) && info.st_size>0,
            std::string("invalid or missing input: ")+path);
    require(static_cast<uintmax_t>(info.st_size)<=std::numeric_limits<size_t>::max(),"input file exceeds addressable size");
    return static_cast<size_t>(info.st_size);
}
struct RuntimeShape {
    size_t frames,samples,total;
};
RuntimeShape read_shape(const char* path,bool runtime_frames) {
    const size_t bytes=file_bytes(path);
    require(bytes%(channels*sizeof(float))==0,"core file must contain 22 complete FP32 channels");
    const size_t frames=bytes/(channels*sizeof(float));
    require(frames>1 && frames<=static_cast<size_t>(std::numeric_limits<int>::max())/channels,"core frame count exceeds checked tensor limits");
    require(runtime_frames || frames==fixture_frames,"non-fixture core length requires --runtime-frames");
    const size_t samples=checked_multiply(frames-1,hop);
    require(samples<=std::numeric_limits<size_t>::max()-kernel,"waveform size overflow");
    const size_t total=samples+kernel;
    checked_multiply(total,sizeof(double));
    checked_multiply(samples,sizeof(float));
    return {frames,samples,total};
}
File open_file(const char* path,const char* mode) {
    File file(std::fopen(path,mode));
    require(bool(file),std::string("cannot open ")+path);
    return file;
}
void read_exact(const char* path,void* data,size_t bytes) {
    File file=open_file(path,"rb");
    require(std::fread(data,1,bytes,file.get())==bytes && std::fgetc(file.get())==EOF && !std::ferror(file.get()),
            std::string("wrong size or read error: ")+path);
}
uint32_t little_u32(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
float little_float(const unsigned char* p) {
    uint32_t bits=little_u32(p);
    float value;
    std::memcpy(&value,&bits,sizeof(value));
    return value;
}
std::vector<float> read_floats(const char* path,size_t count) {
    const size_t bytes_count=checked_multiply(count,sizeof(float));
    std::vector<float> values(count);
    read_exact(path,values.data(),bytes_count);
    unsigned char* bytes=reinterpret_cast<unsigned char*>(values.data());
    for(size_t i=0;i<count;++i) {
        float value=little_float(bytes+i*4);
        if(!std::isfinite(value)) throw std::runtime_error(std::string("nonfinite input: ")+path);
        values[i]=value;
    }
    return values;
}
struct Tail {
    float scale,threshold;
    std::array<float,channels*kernel> basis;
    std::array<float,kernel> window;
};
Tail read_tail(const char* path) {
    std::array<unsigned char,1936> bytes;
    read_exact(path,bytes.data(),bytes.size());
    require(std::memcmp(bytes.data(),"KOKTAIL\0",8)==0,"invalid tail magic");
    const std::array<uint32_t,12> expected={{1,88,channels,bins,fixture_frames,kernel,kernel,hop,trim,trim,24000,0}};
    for(size_t i=0;i<expected.size();++i)
        require(little_u32(bytes.data()+8+i*4)==expected[i],"unsupported tail version, dimensions, or constants");
    const char* source_hash="e6454fdf4a4b8138d67777683e3ac1e9d90561d7de4a5029831dbce55164d430";
    const char* digits="0123456789abcdef";
    for(size_t i=0;i<32;++i) {
        unsigned value=bytes[56+i];
        require(digits[value>>4]==source_hash[i*2] && digits[value&15]==source_hash[i*2+1],"unexpected source ONNX hash");
    }
    Tail tail;
    tail.scale=little_float(bytes.data()+88);
    tail.threshold=little_float(bytes.data()+92);
    require(tail.scale==4.f && tail.threshold==std::numeric_limits<float>::min(),"unexpected scale or threshold");
    for(size_t i=0;i<tail.basis.size();++i) {
        tail.basis[i]=little_float(bytes.data()+96+i*4);
        require(std::isfinite(tail.basis[i]),"nonfinite inverse basis");
    }
    for(size_t i=0;i<tail.window.size();++i) {
        tail.window[i]=little_float(bytes.data()+96+tail.basis.size()*4+i*4);
        require(std::isfinite(tail.window[i]) && tail.window[i]>=0,"invalid window normalization");
    }
    return tail;
}
double cpu_time() {
    struct rusage usage;
    require(getrusage(RUSAGE_SELF,&usage)==0,"getrusage failed");
    return usage.ru_utime.tv_sec+usage.ru_stime.tv_sec+(usage.ru_utime.tv_usec+usage.ru_stime.tv_usec)*1e-6;
}
std::vector<double> waveform(const std::vector<float>& core,const Tail& tail,const RuntimeShape& shape) {
    const size_t frames=shape.frames;
    std::vector<double> result(shape.total,0.0);
    // Reverse frames preserves the exported sample-major overlap-add order.
    for(size_t frame=frames;frame-- > 0;) {
        std::array<double,channels> spectrum;
        for(size_t bin=0;bin<bins;++bin) {
            double magnitude=std::exp(double(core[bin*frames+frame]));
            double phase=std::sin(double(core[(bin+bins)*frames+frame]));
            spectrum[bin]=magnitude*std::cos(phase);
            spectrum[bin+bins]=magnitude*std::sin(phase);
            require(std::isfinite(spectrum[bin]) && std::isfinite(spectrum[bin+bins]),"nonfinite spectrum");
        }
        for(size_t sample=0;sample<kernel;++sample) {
            double block=0;
            for(size_t channel=0;channel<channels;++channel)
                block+=double(tail.basis[channel*kernel+sample])*spectrum[channel];
            result[frame*hop+sample]+=block;
        }
    }
    for(size_t i=0;i<tail.window.size();++i)
        if(tail.window[i]>tail.threshold) result[i]/=double(tail.window[i]);
    for(double& value:result) {
        value*=double(tail.scale);
        require(std::isfinite(value) && std::fabs(value)<=std::numeric_limits<float>::max(),"nonfinite or unrepresentable waveform");
    }
    return result;
}
void write_waveform(const char* path,const std::vector<double>& values,size_t samples) {
    const std::string temporary=std::string(path)+".tmp";
    File file=open_file(temporary.c_str(),"wbx");
    try {
        std::array<unsigned char,4096> buffer;
        for(size_t first=0;first<samples;) {
            size_t count=std::min(buffer.size()/4,samples-first);
            for(size_t j=0;j<count;++j) {
                float value=static_cast<float>(values[trim+first+j]);
                uint32_t bits;
                std::memcpy(&bits,&value,sizeof(bits));
                for(unsigned k=0;k<4;++k) buffer[j*4+k]=static_cast<unsigned char>(bits>>(8*k));
            }
            require(std::fwrite(buffer.data(),4,count,file.get())==count,"waveform write failed");
            first+=count;
        }
        require(std::fclose(file.release())==0,"waveform close failed");
        require(std::rename(temporary.c_str(),path)==0,"waveform rename failed");
    } catch(...) {
        file.reset();
        std::remove(temporary.c_str());
        throw;
    }
}
} // namespace

int main(int argc,char** argv) {
    const bool runtime_frames=argc==6 && !std::strcmp(argv[1],"--runtime-frames");
    if(argc!=5 && !runtime_frames) {
        std::fprintf(stderr,"Usage: %s [--runtime-frames] CORE.f32 TAIL.bin REFERENCE.f32 OUTPUT.f32\n",argv[0]);
        return 1;
    }
    const int first=runtime_frames?2:1;
    const char* core_path=argv[first];
    const char* tail_path=argv[first+1];
    const char* reference_path=argv[first+2];
    const char* output_path=argv[first+3];
    try {
        struct stat output_stat;
        if(stat(output_path,&output_stat)==0) {
            for(int i=first;i<first+3;++i) {
                struct stat input_stat;
                require(stat(argv[i],&input_stat)==0,"cannot stat input");
                require(input_stat.st_dev!=output_stat.st_dev || input_stat.st_ino!=output_stat.st_ino,"output aliases an input");
            }
        }
        const RuntimeShape shape=read_shape(core_path,runtime_frames);
        const size_t samples=shape.samples;
        require(file_bytes(reference_path)==checked_multiply(samples,sizeof(float)),"waveform reference length does not match runtime core");
        Tail tail=read_tail(tail_path);
        std::vector<float> core=read_floats(core_path,checked_multiply(channels,shape.frames));
        std::vector<float> reference=read_floats(reference_path,samples);
        std::printf("waveform dimensions: %zu core frames, %zu samples%s\n",shape.frames,samples,runtime_frames?" (runtime frame override)":"");
        const double before_cpu=cpu_time();
        const auto start=Clock::now();
        std::vector<double> result=waveform(core,tail,shape);
        const double wall=std::chrono::duration<double>(Clock::now()-start).count();
        const double cpu=cpu_time()-before_cpu;
        double signal_power=0,error_power=0,max_error=0;
        size_t failed=0;
        for(size_t i=0;i<samples;++i) {
            double ref=reference[i],error=result[trim+i]-ref;
            signal_power+=ref*ref;
            error_power+=error*error;
            max_error=std::max(max_error,std::fabs(error));
            if(std::fabs(error)>absolute_tolerance+relative_tolerance*std::fabs(ref)) ++failed;
        }
        require(signal_power>0,"reference has zero signal power");
        const double snr=10*std::log10(signal_power/std::max(error_power,1e-30));
        std::printf("CPU waveform tail: wall %.6f s, CPU %.6f s, %zu samples at 24000 Hz\n",wall,cpu,samples);
        std::printf("max_abs %.9g, SNR %.6f dB, mismatches %zu, atol=%g rtol=%g minimum_snr=%g\n",max_error,snr,failed,absolute_tolerance,relative_tolerance,minimum_snr);
        require(failed==0 && snr>=minimum_snr,"waveform accuracy gate failed");
        write_waveform(output_path,result,samples);
        std::printf("PASS: saved-source inputs and CPU waveform tail; wrote %s\n",output_path);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        return 1;
    }
}
