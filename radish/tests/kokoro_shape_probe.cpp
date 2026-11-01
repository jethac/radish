#include "kokoro_shapes.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

namespace {
void require(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); }
void write_new(const char* filename, const std::string& text) {
    FILE* file = std::fopen(filename, "wx");
    require(file != nullptr, "cannot create exclusive output");
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    require(written && closed, "cannot write output");
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 5, "Usage: kokoro-shape-probe PARAM T NEW_HINTED_PARAM NEW_SHAPES_TSV");
        char* end = nullptr; errno = 0;
        const long t = std::strtol(argv[2], &end, 10);
        require(errno == 0 && end != argv[2] && *end == '\0' && t >= 1 && t <= 4096, "T must be 1..4096");
        struct stat info;
        require(lstat(argv[3], &info) != 0 && errno == ENOENT && lstat(argv[4], &info) != 0 && errno == ENOENT,
                "outputs must be new");
        std::ifstream input(argv[1], std::ios::binary);
        require(bool(input), "cannot read parameter file");
        std::ostringstream text; text << input.rdbuf(); require(!input.bad(), "parameter read failed");
        const radish::KokoroShapePlan plan = radish::infer_kokoro_shapes(text.str(), static_cast<int>(t));
        std::ostringstream shapes;
        shapes << "ncnn-logical-shapes-v1\n";
        for (const radish::KokoroShapeRecord& item : plan.shapes)
            shapes << item.layer << '\t' << item.blob << '\t' << item.name << '\t' << item.shape.dims << '\t'
                   << item.shape.w << '\t' << item.shape.h << '\t' << item.shape.d << '\t' << item.shape.c << '\n';
        write_new(argv[3], plan.param_text); write_new(argv[4], shapes.str());
        std::printf("PASS: metadata-only T=%ld: %d layers, %d logical shapes; no weights or tensors executed\n", t, plan.layers, plan.blobs);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
