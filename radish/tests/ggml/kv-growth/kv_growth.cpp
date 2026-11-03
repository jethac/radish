#include "causal_lm/causal_lm.h"
#include "transcribe-backend.h"
#include "ggml-vulkan.h"
#include "ggml-cpu.h"
#include "ggml.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

static constexpr int heads = 8, dims = 128, layers = 28, prompt_rows = 12;
static void require(bool value, const char * message) {
    if (!value) throw std::runtime_error(message);
}

static int initial_khr_kv_capacity(int64_t needed, int limit) {
    int64_t capacity = 16;
    while (capacity < needed && capacity < limit) capacity *= 2;
    return static_cast<int>(std::min<int64_t>(capacity, limit));
}

static int next_capacity(int current, int capacity, int limit) {
    return static_cast<int>(std::min<int64_t>(limit,
        std::max<int64_t>(static_cast<int64_t>(current) + 1,
                          static_cast<int64_t>(capacity) * 2)));
}

static void policy_check() {
    require(initial_khr_kv_capacity(9, 320) == 16, "prefix policy");
    require(initial_khr_kv_capacity(49 + 1, 320) == 64, "prompt policy");
    require(initial_khr_kv_capacity(305, 320) == 320, "nonpow2 clamp");
    int capacity = 64;
    for (int expected : {128, 256, 320}) {
        capacity = next_capacity(capacity, capacity, 320);
        require(capacity == expected, "growth policy");
    }
    int current = 49, generated = 1, writes = 0;
    capacity = initial_khr_kv_capacity(current + 1, 320);
    while (generated < 256 && current + 1 <= 320) {
        if (current >= capacity) capacity = next_capacity(current, capacity, 320);
        require(current < capacity && capacity <= 320, "write/mask bound");
        ++current; ++generated; ++writes;
    }
    require(generated == 256 && writes == 255 && current == 304 && capacity == 320,
            "original full generation budget");
    require(319 + 1 <= 320 && !(320 + 1 <= 320), "last context row");
    std::puts("KV_POLICY prefix16 prompt64 growth128/256/320 generated256 writes255 passed timing_collected=false");
}

static uint32_t scalar_bits(ggml_type type, int kind, int layer, int position, int head, int dim) {
    uint32_t mix = uint32_t(layer * 193 + position * 97 + head * 31 + dim * 7 + kind * 701);
    mix ^= position < prompt_rows ? 0x155u : 0x2aau;
    const int value = int(mix % 2047u) - 1023;
    if (type == GGML_TYPE_F16) {
        uint32_t magnitude = uint32_t(value < 0 ? -value : value);
        return (value < 0 ? 0x8000u : 0u) | 0x2400u | (magnitude & 1023u);
    }
    float value_f32 = float(value) / 256.0f;
    uint32_t bits;
    std::memcpy(&bits, &value_f32, sizeof(bits));
    return bits;
}

static std::vector<uint8_t> expected(ggml_type type, int capacity, int written, int kind, bool permuted) {
    const size_t element = ggml_type_size(type);
    std::vector<uint8_t> bytes(size_t(layers) * capacity * heads * dims * element, 0);
    for (int layer = 0; layer < layers; ++layer)
        for (int pos = 0; pos < written; ++pos)
            for (int head = 0; head < heads; ++head)
                for (int dim = 0; dim < dims; ++dim) {
                    const size_t index = permuted
                        ? ((size_t(layer) * heads + head) * capacity + pos) * dims + dim
                        : ((size_t(layer) * capacity + pos) * heads + head) * dims + dim;
                    const uint32_t bits = scalar_bits(type, kind, layer, pos, head, dim);
                    std::memcpy(bytes.data() + index * element, &bits, element);
                }
    return bytes;
}

static void append_rows(transcribe::causal_lm::KvCache & cache, int first, int end) {
    const size_t row = size_t(heads) * dims * ggml_type_size(cache.self_k->type);
    for (int kind = 0; kind < 2; ++kind) {
        const auto all = expected(cache.self_k->type, cache.n_ctx, end, kind, false);
        auto * tensor = kind ? cache.self_v : cache.self_k;
        for (int layer = 0; layer < layers; ++layer) {
            const size_t offset = (size_t(layer) * cache.n_ctx + first) * row;
            ggml_backend_tensor_set(tensor, all.data() + offset, offset, size_t(end - first) * row);
        }
    }
    cache.n = cache.head = end <= prompt_rows ? end : end + 1;
}

static void verify_allocation(transcribe::causal_lm::KvCache & cache, int written) {
    size_t total = 0;
    for (int kind = 0; kind < 2; ++kind) {
        auto * tensor = kind ? cache.self_v : cache.self_k;
        const auto wanted = expected(tensor->type, cache.n_ctx, written, kind, false);
        std::vector<uint8_t> got(wanted.size());
        ggml_backend_tensor_get(tensor, got.data(), 0, got.size());
        require(got == wanted, "complete KV payload/unused row mismatch");
        require(ggml_nbytes(tensor) == got.size(), "KV tensor bound");
        total += got.size();
    }
    require(ggml_backend_buffer_get_size(cache.buffer) == total, "entire KV allocation size");
    require(cache.n_batch == 1, "batch state changed");
}

static void verify_fresh_graph(ggml_backend_t backend, ggml_backend_t cpu, transcribe::causal_lm::KvCache & cache, int written) {
    std::unique_ptr<ggml_context, decltype(&ggml_free)> ctx(
        ggml_init({size_t(1) << 20, nullptr, true}), &ggml_free);
    require(bool(ctx), "graph context");
    const size_t element = ggml_type_size(cache.self_k->type);
    const size_t row = size_t(heads) * dims * element;
    const size_t payload = size_t(layers) * written * row;
    const size_t total = 2 * (payload + 128);
    std::unique_ptr<ggml_backend_buffer, decltype(&transcribe::safe_buffer_free)> buffer(
        ggml_backend_alloc_buffer(backend, total), &transcribe::safe_buffer_free);
    require(bool(buffer), "guarded output buffer");
    ggml_backend_buffer_clear(buffer.get(), 0xa5);
    auto * whole = ggml_new_tensor_1d(ctx.get(), GGML_TYPE_I8, int64_t(total));
    const uintptr_t base = reinterpret_cast<uintptr_t>(ggml_backend_buffer_get_base(buffer.get()));
    require(ggml_backend_tensor_alloc(buffer.get(), whole, reinterpret_cast<void *>(base)) == GGML_STATUS_SUCCESS,
            "whole output readback binding");
    auto * graph = ggml_new_graph_custom(ctx.get(), 128, false);
    require(graph, "graph allocation");
    for (int kind = 0; kind < 2; ++kind) {
        auto * source = kind ? cache.self_v : cache.self_k;
        auto * view = ggml_view_4d(ctx.get(), source, dims, heads, written, layers,
            dims * element, row, size_t(cache.n_ctx) * row, 0);
        auto * output = ggml_cont(ctx.get(), ggml_permute(ctx.get(), view, 0, 2, 1, 3));
        ggml_set_output(output);
        require(ggml_nbytes(output) == payload, "fresh view extent");
        const uintptr_t offset = size_t(kind) * (payload + 128) + 64;
        require(ggml_backend_tensor_alloc(buffer.get(), output, reinterpret_cast<void *>(base + offset)) == GGML_STATUS_SUCCESS,
                "guarded output tensor binding");
        ggml_build_forward_expand(graph, output);
    }
    ggml_backend_t backends[] = {backend, cpu};
    std::unique_ptr<ggml_backend_sched, decltype(&transcribe::safe_sched_free)> sched(
        ggml_backend_sched_new(backends, nullptr, 2, 128, false, true), &transcribe::safe_sched_free);
    require(bool(sched), "graph scheduler");
    require(ggml_backend_sched_alloc_graph(sched.get(), graph), "graph allocation failed");
    int compute = 0;
    for (int i = 0; i < ggml_graph_n_nodes(graph); ++i) {
        auto * node = ggml_graph_node(graph, i);
        if (node->op == GGML_OP_CONT) {
            require(ggml_backend_sched_get_tensor_backend(sched.get(), node) == backend, "GPU placement");
            ++compute;
        }
    }
    require(compute == 2, "both KV graphs must compute");
    require(ggml_backend_sched_graph_compute(sched.get(), graph) == GGML_STATUS_SUCCESS, "fresh GPU graph");
    std::vector<uint8_t> got(total), wanted(total, 0xa5);
    ggml_backend_tensor_get(whole, got.data(), 0, total);
    for (int kind = 0; kind < 2; ++kind) {
        const auto bytes = expected(cache.self_k->type, written, written, kind, true);
        std::copy(bytes.begin(), bytes.end(), wanted.begin() + size_t(kind) * (payload + 128) + 64);
    }
    require(got == wanted, "fresh-view output/full guard mismatch");
    verify_allocation(cache, written);
    std::printf("KV_GRAPH type=%d capacity=%d written=%d gpu_compute=2 bytes=%zu guards=passed\n",
                int(cache.self_k->type), cache.n_ctx, written, total);
}

static void run_type(ggml_backend_t backend, ggml_backend_t cpu, ggml_type type) {
    struct Cache {
        transcribe::causal_lm::KvCache value;
        ~Cache() { value.free(); }
    } owned;
    auto & cache = owned.value;
    require(transcribe::causal_lm::kv_init(cache, backend, 16, heads, dims, layers, type), "actual kv_init");
    append_rows(cache, 0, prompt_rows);
    append_rows(cache, prompt_rows, 16);
    int written = 16;
    verify_allocation(cache, written);
    verify_fresh_graph(backend, cpu, cache, written);
    for (int capacity : {32, 64}) {
        const int old_capacity = cache.n_ctx, old_n = cache.n, old_head = cache.head;
        require(transcribe::causal_lm::kv_grow(cache, backend, capacity, heads, dims, layers), "actual kv_grow");
        require(cache.n_ctx == capacity && cache.n == old_n && cache.head == old_head, "growth counters");
        verify_allocation(cache, written);
        verify_fresh_graph(backend, cpu, cache, written);
        const int end = capacity == 32 ? 32 : 48;
        append_rows(cache, written, end);
        written = end;
        verify_allocation(cache, written);
        verify_fresh_graph(backend, cpu, cache, written);
        std::printf("KV_GROW type=%d old=%d capacity=%d n=%d head=%d generated_rows=%d passed\n",
                    int(type), old_capacity, capacity, cache.n, cache.head, written - prompt_rows);
    }
}

int main() {
    try {
        require(std::getenv("TRANSCRIBE_VULKAN_KHR_PROFILE") &&
                std::strcmp(std::getenv("TRANSCRIBE_VULKAN_KHR_PROFILE"), "1") == 0, "KHR profile required");
        for (const char * name : {"RADISH_PROFILE", "RADISH_GPU_ELAPSED", "RADISH_GPU_ELAPSED_HOTSPOT", "GGML_VK_PERF_LOGGER"}) {
            const char * value = std::getenv(name);
            require(!value || !value[0] || std::strcmp(value, "0") == 0, "performance instrumentation forbidden");
        }
        policy_check();
        std::unique_ptr<ggml_backend, decltype(&transcribe::safe_backend_free)> backend(
            ggml_backend_vk_init(0), &transcribe::safe_backend_free);
        require(bool(backend) && ggml_backend_is_vk(backend.get()), "Vulkan backend required");
        std::unique_ptr<ggml_backend, decltype(&transcribe::safe_backend_free)> cpu(
            ggml_backend_cpu_init(), &transcribe::safe_backend_free);
        require(bool(cpu), "scheduler CPU backend required");
        run_type(backend.get(), cpu.get(), GGML_TYPE_F16);
        run_type(backend.get(), cpu.get(), GGML_TYPE_F32);
        std::puts("KV_GROWTH_PASS types=2 growths=4 fresh_graphs=10 prompt_rows=12 timing_collected=false");
        return 0;
    } catch (const std::exception & error) {
        std::fprintf(stderr, "FAIL KV_GROWTH %s\n", error.what());
        return 1;
    }
}
