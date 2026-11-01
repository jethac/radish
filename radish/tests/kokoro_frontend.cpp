// Standalone CPU Kokoro frontend/source capture. No listener or GPU changes.
// Build against the pinned ORT C API24 headers; load the runtime explicitly.
#include "kokoro_frontend.h"
#include <onnxruntime_c_api.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace {
using radish_frontend::Timing;

static_assert(ORT_API_VERSION == 24, "use the pinned ORT API24 headers");
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "requires IEEE FP32");

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
static double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::string path(const std::string& directory, const char* name) {
    return directory + "/" + name;
}
static std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    }
    out << '"';
    return out.str();
}

template<class T> static std::vector<T> read_raw(const std::string& filename, size_t min_count, size_t max_count) {
    std::unique_ptr<FILE, int(*)(FILE*)> file(std::fopen(filename.c_str(), "rb"), std::fclose);
    require(bool(file), "cannot open " + filename);
    struct stat info;
    require(fstat(fileno(file.get()), &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0,
            "not a regular input file: " + filename);
    const uint64_t bytes = uint64_t(info.st_size);
    require(bytes % sizeof(T) == 0 && bytes / sizeof(T) >= min_count && bytes / sizeof(T) <= max_count,
            "unexpected raw input length: " + filename);
    std::vector<T> values(size_t(bytes / sizeof(T)));
    require(std::fread(values.data(), sizeof(T), values.size(), file.get()) == values.size() &&
            std::fgetc(file.get()) == EOF && !std::ferror(file.get()), "cannot read complete input: " + filename);
    return values;
}
static void finite(const std::vector<float>& values, const char* name) {
    for (float value : values) require(std::isfinite(value), std::string("nonfinite ") + name);
}

// A completed directory appears only after every file and the manifest are written.
// renameat2(NOREPLACE) also rejects an existing empty directory or dangling symlink.
class OutputDirectory {
    std::string destination, stage;
    std::vector<std::string> files;
public:
    explicit OutputDirectory(const std::string& output) : destination(output) {
        require(!output.empty() && output.back() != '/', "output must name a new directory without a trailing slash");
        struct stat info;
        require(lstat(output.c_str(), &info) != 0 && errno == ENOENT, "output already exists or cannot be inspected");
        const size_t slash = output.find_last_of('/');
        const std::string parent = slash == std::string::npos ? "." : (slash == 0 ? "/" : output.substr(0, slash));
        std::string pattern = parent + "/.kokoro-frontend-XXXXXX";
        std::vector<char> buffer(pattern.begin(), pattern.end()); buffer.push_back('\0');
        char* result = mkdtemp(buffer.data());
        require(result != nullptr, "cannot create output staging directory");
        stage = result;
    }
    ~OutputDirectory() {
        if (!stage.empty()) {
            for (const std::string& file : files) unlink(file.c_str());
            rmdir(stage.c_str());
        }
    }
    void write(const char* name, const void* data, size_t bytes) {
        const std::string filename = path(stage, name);
        const int fd = open(filename.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
        require(fd >= 0, "cannot create " + filename);
        files.push_back(filename);
        const char* cursor = static_cast<const char*>(data);
        size_t remaining = bytes;
        while (remaining) {
            const ssize_t count = ::write(fd, cursor, remaining);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { close(fd); throw std::runtime_error("cannot write " + filename); }
            cursor += count; remaining -= size_t(count);
        }
        require(close(fd) == 0, "cannot close " + filename);
    }
    template<class T> void write(const char* name, const std::vector<T>& values) {
        write(name, values.data(), values.size() * sizeof(T));
    }
    void publish() {
        require(syscall(SYS_renameat2, AT_FDCWD, stage.c_str(), AT_FDCWD, destination.c_str(), 1 /* RENAME_NOREPLACE */) == 0,
                "cannot publish new output directory (renameat2 NOREPLACE required)");
        stage.clear();
    }
};

template<class T> using OrtHandle = std::unique_ptr<T, void(*)(T*)>;
struct Tensor {
    std::vector<int64_t> shape;
    std::vector<float> values;
};

class Runtime {
    void* library = nullptr;
    OrtEnv* environment = nullptr;
    OrtMemoryInfo* memory = nullptr;
    OrtSessionOptions* options = nullptr;
public:
    const OrtApi* api = nullptr;
    std::string version;
    Runtime(const std::string& filename, int threads) {
        try {
            library = dlopen(filename.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!library) { const char* error = dlerror(); throw std::runtime_error(error ? error : "dlopen failed"); }
            typedef const OrtApiBase* (*GetBase)();
            GetBase get_base = reinterpret_cast<GetBase>(dlsym(library, "OrtGetApiBase"));
            require(get_base != nullptr, "runtime lacks OrtGetApiBase");
            const OrtApiBase* base = get_base();
            require(base && base->GetVersionString && base->GetApi, "invalid ORT API base");
            const char* reported = base->GetVersionString();
            require(reported != nullptr, "ORT version unavailable");
            version = reported;
            int major = 0, minor = 0;
            require(std::sscanf(version.c_str(), "%d.%d", &major, &minor) == 2 && major == 1 && minor >= 24,
                    "ORT API24 requires runtime1.24 or newer in the1.x series; got " + version);
            std::printf("ONNX Runtime %s; requested C API24; threads=%d; optimization=disabled\n", version.c_str(), threads);
            std::fflush(stdout);
            api = base->GetApi(ORT_API_VERSION);
            require(api != nullptr, "runtime does not provide C API24");
            check(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "radish-kokoro-frontend", &environment));
            check(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
            check(api->CreateSessionOptions(&options));
            check(api->SetIntraOpNumThreads(options, threads));
            check(api->SetInterOpNumThreads(options, 1));
            check(api->SetSessionGraphOptimizationLevel(options, ORT_DISABLE_ALL));
            check(api->AddSessionConfigEntry(options, "session.intra_op.allow_spinning", "0"));
            check(api->AddSessionConfigEntry(options, "session.inter_op.allow_spinning", "0"));
        } catch (...) { cleanup(); throw; }
    }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    ~Runtime() { cleanup(); }
    void cleanup() {
        if (api) {
            if (options) api->ReleaseSessionOptions(options);
            if (memory) api->ReleaseMemoryInfo(memory);
            if (environment) api->ReleaseEnv(environment);
        }
        if (library) dlclose(library);
        options = nullptr; memory = nullptr; environment = nullptr; library = nullptr;
    }
    void check(OrtStatus* status) const {
        if (!status) return;
        const std::string message = api->GetErrorMessage(status);
        api->ReleaseStatus(status);
        throw std::runtime_error("ORT: " + message);
    }
    OrtHandle<OrtValue> tensor(void* data, size_t bytes, const std::vector<int64_t>& shape, ONNXTensorElementDataType type) const {
        OrtValue* result = nullptr;
        check(api->CreateTensorWithDataAsOrtValue(memory, data, bytes, shape.data(), shape.size(), type, &result));
        return OrtHandle<OrtValue>(result, api->ReleaseValue);
    }
    void names(OrtSession* session, bool input, const std::vector<const char*>& expected) const {
        size_t count = 0;
        check(input ? api->SessionGetInputCount(session, &count) : api->SessionGetOutputCount(session, &count));
        require(count == expected.size(), "unexpected graph input/output count; frontend must exclude the harmonic source");
        OrtAllocator* allocator = nullptr;
        check(api->GetAllocatorWithDefaultOptions(&allocator));
        for (size_t i = 0; i < count; ++i) {
            char* raw = nullptr;
            check(input ? api->SessionGetInputName(session, i, allocator, &raw) : api->SessionGetOutputName(session, i, allocator, &raw));
            const std::string name(raw); allocator->Free(allocator, raw);
            require(name == expected[i], "unexpected graph input/output name: " + name);
        }
    }
    std::vector<Tensor> run(const std::string& model, const std::vector<const char*>& input_names,
                           const std::vector<const OrtValue*>& inputs, const std::vector<const char*>& output_names,
                           Timing& timing, size_t max_values) const {
        const double begin = now();
        OrtSession* raw = nullptr;
        check(api->CreateSession(environment, model.c_str(), options, &raw));
        OrtHandle<OrtSession> session(raw, api->ReleaseSession);
        timing.load = now() - begin;
        names(session.get(), true, input_names);
        names(session.get(), false, output_names);
        std::vector<OrtValue*> results(output_names.size(), nullptr);
        struct Outputs {
            const OrtApi* api; std::vector<OrtValue*>& values;
            ~Outputs() { for (OrtValue* value : values) if (value) api->ReleaseValue(value); }
        } cleanup_results = {api, results};
        const double started = now();
        check(api->Run(session.get(), nullptr, input_names.data(), inputs.data(), inputs.size(),
                       output_names.data(), output_names.size(), results.data()));
        timing.run = now() - started;
        std::vector<Tensor> copied;
        for (OrtValue* value : results) {
            int is_tensor = 0;
            check(api->IsTensor(value, &is_tensor)); require(is_tensor, "graph returned a non-tensor");
            OrtTensorTypeAndShapeInfo* raw_shape = nullptr;
            check(api->GetTensorTypeAndShape(value, &raw_shape));
            OrtHandle<OrtTensorTypeAndShapeInfo> info(raw_shape, api->ReleaseTensorTypeAndShapeInfo);
            ONNXTensorElementDataType type;
            check(api->GetTensorElementType(info.get(), &type));
            require(type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, "graph returned non-FP32 data");
            size_t rank = 0, count = 0;
            check(api->GetDimensionsCount(info.get(), &rank));
            check(api->GetTensorShapeElementCount(info.get(), &count));
            require(rank >= 1 && rank <= 3 && count > 0 && count <= max_values, "output exceeds tensor bounds");
            Tensor copy; copy.shape.resize(rank);
            check(api->GetDimensions(info.get(), copy.shape.data(), rank));
            for (int64_t dimension : copy.shape) require(dimension > 0, "nonpositive output dimension");
            float* data = nullptr;
            check(api->GetTensorMutableData(value, reinterpret_cast<void**>(&data)));
            require(data != nullptr, "output has no data");
            copy.values.assign(data, data + count); finite(copy.values, "ORT output");
            copied.push_back(std::move(copy));
        }
        // Copies outlive both OrtValues and the session/its large frontend arena.
        return copied;
    }
};

} // namespace

namespace radish_frontend {
Boundary generate_frontend(const std::string& library, const std::string& frontend,
                           const std::string& source, const std::string& input,
                           int max_frames, int threads) {
    require(max_frames >= 1 && max_frames <= 4096, "invalid max-frames");
    require(threads >= 1 && threads <= 6, "invalid threads");
    const uint16_t endian = 1;
    require(*reinterpret_cast<const unsigned char*>(&endian) == 1, "raw captures require little-endian host");
    std::vector<int64_t> ids = read_raw<int64_t>(path(input, "ids.i64"), 3, 512);
    std::vector<float> style = read_raw<float>(path(input, "style.f32"), 256, 256);
    std::vector<float> speed = read_raw<float>(path(input, "speed.f32"), 1, 1);
    require(ids.front() == 0 && ids.back() == 0, "token IDs require both zero pads");
    for (size_t i = 1; i + 1 < ids.size(); ++i) require(ids[i] >= 1 && ids[i] <= 177, "invalid Kokoro token ID");
    finite(style, "style"); finite(speed, "speed"); require(speed[0] > 0, "speed must be positive");
    const double started = now();
    Boundary boundary;
    {
        Runtime runtime(library, threads);
        OrtHandle<OrtValue> token_tensor = runtime.tensor(ids.data(), ids.size()*sizeof(int64_t), {1, int64_t(ids.size())}, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
        OrtHandle<OrtValue> style_tensor = runtime.tensor(style.data(), style.size()*sizeof(float), {1, 256}, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        OrtHandle<OrtValue> speed_tensor = runtime.tensor(speed.data(), sizeof(float), {1}, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        const char* asr_name = "/encoder/MatMul_1_output_0";
        const char* f0_name = "/encoder/Reshape_output_0";
        const char* n_name = "/encoder/Reshape_1_output_0";
        const char* duration_name = "/encoder/Clip_output_0";
        const char* source_name = "/decoder/decoder/generator/m_source/l_tanh/Tanh_output_0";
        const char* har_name = "/decoder/decoder/generator/Concat_3_output_0";
        Timing front_time, source_time;
        std::vector<Tensor> front = runtime.run(frontend, {"input_ids", "style", "speed"},
            {token_tensor.get(), style_tensor.get(), speed_tensor.get()}, {asr_name, f0_name, n_name, duration_name},
            front_time, size_t(512)*size_t(max_frames));
        require(front[0].shape.size() == 3 && front[0].shape[0] == 1 && front[0].shape[1] == 512,
                "unexpected ASR shape");
        const int64_t t = front[0].shape[2], frames = 120*t+1, samples = 600*t;
        require(t >= 1 && t <= max_frames, "frontend duration exceeds requested frame bound");
        require(front[1].shape == std::vector<int64_t>({1, 2*t}) && front[2].shape == front[1].shape,
                "F0/N shape does not match ASR length");
        require(front[3].shape == std::vector<int64_t>({1, int64_t(ids.size())}), "durations shape does not match IDs");
        double sum = 0;
        for (float duration : front[3].values) {
            require(duration >= 1 && std::floor(duration) == duration, "invalid token duration"); sum += duration;
        }
        require(sum == double(t), "duration sum does not match ASR length");
        std::printf("Frontend: T=%lld load=%.6fs run=%.6fs; session released before source\n",
                    static_cast<long long>(t), front_time.load, front_time.run); std::fflush(stdout);
        OrtHandle<OrtValue> f0_tensor = runtime.tensor(front[1].values.data(), front[1].values.size()*sizeof(float),
                                                     front[1].shape, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        std::vector<Tensor> harmonic = runtime.run(source, {f0_name}, {f0_tensor.get()}, {source_name, har_name},
                                                  source_time, size_t(22*frames));
        require(harmonic[0].shape == std::vector<int64_t>({1, samples, 1}) &&
                harmonic[1].shape == std::vector<int64_t>({1, 22, frames}), "unexpected harmonic source shape");
        boundary.T = t; boundary.frames = frames; boundary.samples = samples;
        boundary.asr = std::move(front[0].values); boundary.f0 = std::move(front[1].values);
        boundary.n = std::move(front[2].values); boundary.durations = std::move(front[3].values);
        boundary.har = std::move(harmonic[1].values);
        boundary.ort_version = runtime.version;
        boundary.frontend_time = front_time; boundary.source_time = source_time;
    }
    boundary.boundary_seconds = now() - started;
    boundary.ids = std::move(ids); boundary.style = std::move(style); boundary.speed = speed[0];
    boundary.ort_library = library; boundary.frontend_graph = frontend; boundary.source_graph = source;
    boundary.input_directory = input; boundary.threads = threads;
    return boundary;
}

void export_boundary(const Boundary& boundary, const std::string& output) {
    OutputDirectory directory(output);
    const std::vector<int64_t>& ids = boundary.ids;
    const std::vector<float>& style = boundary.style;
    const std::string& library = boundary.ort_library;
    const std::string& frontend = boundary.frontend_graph;
    const std::string& source = boundary.source_graph;
    const std::string& input = boundary.input_directory;
    const Timing& front_time = boundary.frontend_time;
    const Timing& source_time = boundary.source_time;
    const int64_t t = boundary.T, frames = boundary.frames, samples = boundary.samples;
    const int threads = boundary.threads;
    const double generated = boundary.boundary_seconds;
    directory.write("ids.i64", ids); directory.write("style.f32", style); directory.write("speed.f32", &boundary.speed, sizeof(boundary.speed));
    directory.write("asr.f32", boundary.asr); directory.write("f0.f32", boundary.f0);
    directory.write("n.f32", boundary.n); directory.write("durations.f32", boundary.durations);
    directory.write("har.f32", boundary.har);
    std::ostringstream report;
    report << std::setprecision(17) << "{\n  \"format\": \"radish-kokoro-frontend-boundary-v1\",\n"
           << "  \"ort_version\": " << quote(boundary.ort_version) << ", \"ort_api\": 24,\n"
           << "  \"ort_library\": " << quote(library) << ",\n  \"frontend_graph\": " << quote(frontend)
           << ",\n  \"source_graph\": " << quote(source) << ",\n  \"input_directory\": " << quote(input) << ",\n"
           << "  \"threads\": " << threads << ", \"optimization\": \"disabled\",\n"
           << "  \"tokens_with_pads\": " << ids.size() << ", \"T\": " << t << ", \"frames\": " << frames
           << ", \"samples\": " << samples << ", \"sample_rate\": 24000,\n"
           << "  \"frontend\": {\"load_seconds\": " << front_time.load << ", \"run_seconds\": " << front_time.run << "},\n"
           << "  \"source\": {\"load_seconds\": " << source_time.load << ", \"run_seconds\": " << source_time.run << "},\n"
           << "  \"boundary_seconds\": " << generated << ",\n"
           << "  \"scope\": \"Fresh CPU frontend/source boundary only; CPU core references, shape hints, GPU and waveform validation remain separate.\"\n}\n";
    const std::string manifest = report.str(); directory.write("frontend.json", manifest.data(), manifest.size());
    directory.publish();
}
} // namespace radish_frontend

#ifndef RADISH_FRONTEND_NO_MAIN
static int number(const char* text, int upper, const char* name) {
    char* end = nullptr; errno = 0;
    const long value = std::strtol(text, &end, 10);
    require(errno == 0 && end != text && *end == '\0' && value >= 1 && value <= upper,
            std::string("invalid ") + name);
    return int(value);
}

int main(int argc, char** argv) {
    try {
        require(argc >= 5, "Usage: kokoro-frontend FRONTEND_ONLY.onnx SOURCE.onnx INPUT_DIR NEW_OUTPUT [--ort-lib FILE] [--threads1..6] [--max-frames1..4096]");
        const std::string frontend = argv[1], source = argv[2], input = argv[3], output = argv[4];
        std::string library = "/usr/local/lib/straylight-listen/libonnxruntime.so";
        int threads = 4, max_frames = 4096;
        for (int i = 5; i < argc; i += 2) {
            require(i + 1 < argc, "missing option value");
            if (!std::strcmp(argv[i], "--ort-lib")) library = argv[i+1];
            else if (!std::strcmp(argv[i], "--threads")) threads = number(argv[i+1], 6, "threads");
            else if (!std::strcmp(argv[i], "--max-frames")) max_frames = number(argv[i+1], 4096, "max-frames");
            else throw std::runtime_error(std::string("unknown option: ") + argv[i]);
        }
        struct stat output_info;
        require(!output.empty() && output.back() != '/', "output must name a new directory without a trailing slash");
        require(lstat(output.c_str(), &output_info) != 0 && errno == ENOENT, "output already exists or cannot be inspected");
        const radish_frontend::Boundary boundary = radish_frontend::generate_frontend(library, frontend, source, input, max_frames, threads);
        radish_frontend::export_boundary(boundary, output);
        const int64_t t = boundary.T, frames = boundary.frames, samples = boundary.samples;
        const Timing& source_time = boundary.source_time;
        const double generated = boundary.boundary_seconds;
        std::printf("PASS: fresh boundary T=%lld frames=%lld samples=%lld source_load=%.6fs source_run=%.6fs total=%.6fs output=%s\n",
                    static_cast<long long>(t), static_cast<long long>(frames), static_cast<long long>(samples),
                    source_time.load, source_time.run, generated, output.c_str());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

#endif
