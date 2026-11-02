#ifndef RADISH_KOKORO_FRONTEND_H
#define RADISH_KOKORO_FRONTEND_H

#include <cstdint>
#include <memory>
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
    std::string optimization = "disabled";
    bool sessions_reused = false;
    bool timing_collected = true;
};

// All returned data is owned; ORT sessions, values and runtime are released before return.
// Errors throw std::runtime_error. Raw input validation matches the standalone CLI.
Boundary generate_frontend(const std::string& ort_library, const std::string& frontend_graph,
                           const std::string& source_graph, const std::string& input_directory,
                           int max_T, int threads);

// Owns exactly one frontend/source session pair. Outputs remain request-owned.
// Dynamic shapes are revalidated per request; the pair is released at destruction.
class Session {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Session(const std::string& ort_library, const std::string& frontend_graph,
            const std::string& source_graph, int threads);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Boundary generate(const std::string& input_directory, int max_T);
};

// Atomically publishes a new directory using the captured inputs and provenance.
void export_boundary(const Boundary& boundary, const std::string& new_output_directory);

} // namespace radish_frontend

#endif
