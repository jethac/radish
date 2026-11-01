#ifndef RADISH_KOKORO_FRONTEND_H
#define RADISH_KOKORO_FRONTEND_H

#include <cstdint>
#include <string>
#include <vector>

namespace radish_frontend {

struct Timing { double load = 0, run = 0; };

struct Boundary {
    int64_t T = 0, frames = 0, samples = 0;
    std::vector<float> asr, f0, n, style, har, durations;
    std::vector<int64_t> ids;
    float speed = 0;
    Timing frontend_time, source_time;
    double boundary_seconds = 0;
    std::string ort_version, ort_library, frontend_graph, source_graph, input_directory;
    int threads = 0;
};

// All returned data is owned; ORT sessions, values and runtime are released before return.
// Errors throw std::runtime_error. Raw input validation matches the standalone CLI.
Boundary generate_frontend(const std::string& ort_library, const std::string& frontend_graph,
                           const std::string& source_graph, const std::string& input_directory,
                           int max_T, int threads);

// Atomically publishes a new directory using the captured inputs and provenance.
void export_boundary(const Boundary& boundary, const std::string& new_output_directory);

} // namespace radish_frontend

#endif
