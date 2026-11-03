// Generic ncnn core benchmark: load PARAM/BIN, read inputs in0.f32.. from DIR
// with shapes from DIR/shapes.txt (one line per input: dims w [h [c]] in ncnn
// order), extract out0 REPEAT times on Vulkan (or CPU with --cpu) and compare
// the last output with DIR/out0.f32 when present.
// usage: radish-core-bench [--cpu] [--threads N] PARAM BIN DIR REPEAT
#include "runtime_helpers.h"
#include "radish_ada_snake.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static bool read_f32(const std::string& path, std::vector<float>& v) {
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff n = f.tellg();
    v.resize(static_cast<size_t>(n) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(v.data()), n);
    return bool(f);
}

int main(int argc, char** argv) {
    bool vulkan = true;
    int threads = 4;
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--cpu") vulkan = false;
        else if (a == "--threads" && i + 1 < argc) threads = atoi(argv[++i]);
        else args.push_back(a);
    }
    if (args.size() != 4) {
        fprintf(stderr, "usage: radish-core-bench [--cpu] [--threads N] PARAM BIN DIR REPEAT\n");
        return 2;
    }
    const std::string dir = args[2];
    const int repeat = atoi(args[3].c_str());
    std::vector<ncnn::Mat> inputs;
    std::vector<std::string> names;
    std::ifstream shapes((dir + "/shapes.txt").c_str());
    std::string line;
    while (std::getline(shapes, line)) {
        std::istringstream ss(line);
        int dims = 0, w = 1, h = 1, c = 1;
        ss >> dims >> w;
        if (dims > 1) ss >> h;
        if (dims > 2) ss >> c;
        ncnn::Mat m = dims == 1 ? ncnn::Mat(w) : dims == 2 ? ncnn::Mat(w, h) : ncnn::Mat(w, h, c);
        std::vector<float> v;
        const std::string name = "in" + std::to_string(inputs.size());
        if (!read_f32(dir + "/" + name + ".f32", v) || v.size() != size_t(w) * h * c) {
            fprintf(stderr, "bad input %s\n", name.c_str());
            return 1;
        }
        for (int q = 0; q < c; q++)
            memcpy(m.channel(q), v.data() + size_t(q) * w * h, size_t(w) * h * 4);
        inputs.push_back(m);
        names.push_back(name);
    }
#if NCNN_VULKAN
    radish_ncnn::GpuInstance instance;
    if (vulkan) {
        if (ncnn::create_gpu_instance() != 0) return 1;
        instance.active = true;
    }
#endif
    ncnn::Net net;
    radish_ncnn::configure_options(net.opt, vulkan, !vulkan, false, threads);
#if NCNN_VULKAN
    if (vulkan) net.set_vulkan_device(ncnn::get_gpu_device(0));
#endif
    radish_ncnn::register_ada_snake(net);
    double t0 = now();
    if (net.load_param(args[0].c_str()) || net.load_model(args[1].c_str())) {
        fprintf(stderr, "load failed\n");
        return 1;
    }
    printf("load %.3f s, %zu layers\n", now() - t0, net.layers().size());
    ncnn::Mat out;
    for (int r = 0; r < repeat; r++) {
        double s = now();
#if NCNN_VULKAN
        if (vulkan) {
            if (radish_ncnn::extract_gpu(net, inputs, names, "out0", out) != 0) return 1;
        } else
#endif
        {
            ncnn::Extractor ex = net.create_extractor();
            for (size_t i = 0; i < inputs.size(); i++) ex.input(names[i].c_str(), inputs[i]);
            if (ex.extract("out0", out) != 0) return 1;
        }
        printf("extract %d: %.3f s\n", r, now() - s);
    }
    std::vector<float> ref;
    const size_t n = out.total();
    std::vector<float> got(n);
    for (int q = 0; q < out.c; q++)
        memcpy(got.data() + size_t(q) * out.w * out.h, out.channel(q), size_t(out.w) * out.h * 4);
    FILE* f = fopen((dir + "/got.f32").c_str(), "wb");
    if (f) { fwrite(got.data(), 4, size_t(out.w) * out.h * out.c, f); fclose(f); }
    if (read_f32(dir + "/out0.f32", ref) && ref.size() == size_t(out.w) * out.h * out.c) {
        double se = 0, sr = 0, mx = 0;
        for (size_t i = 0; i < ref.size(); i++) {
            const double d = got[i] - ref[i];
            se += d * d; sr += double(ref[i]) * ref[i]; mx = std::max(mx, std::fabs(d));
        }
        printf("vs reference: max_abs %.3e rel_rms %.3e (n=%zu)\n", mx, std::sqrt(se / sr), ref.size());
    } else {
        printf("output %d x %d x %d (no matching reference)\n", out.w, out.h, out.c);
    }
    return 0;
}
