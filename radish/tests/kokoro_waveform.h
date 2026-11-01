#ifndef RADISH_KOKORO_WAVEFORM_H
#define RADISH_KOKORO_WAVEFORM_H

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace radish_waveform {

struct Tail {
    float scale = 0, threshold = 0;
    std::array<float, 440> basis{};
    std::array<float, 20> window{};
};

// Checks the pinned v1 header and embedded source hash; payload SHA is the caller's responsibility.
Tail load_tail(const std::string& path);

// Requires count == 22*frames and finite FP32 core values. Returns 5*(frames-1) samples.
// Preserves double tail arithmetic and trims before the final FP32 conversion.
// Errors throw std::runtime_error.
std::vector<float> synthesize(const float* core, std::size_t count, std::size_t frames, const Tail& tail);

} // namespace radish_waveform

#endif
