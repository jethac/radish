#include "kokoro_shapes.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace radish {
namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

int integer(const std::string& text) {
    require(!text.empty(), "empty integer");
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    require(errno == 0 && end != text.c_str() && *end == '\0' && value >= INT_MIN && value <= INT_MAX,
            "invalid or overflowing integer: " + text);
    return static_cast<int>(value);
}

float scalar(const std::string& text) {
    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(text.c_str(), &end);
    require(errno == 0 && end != text.c_str() && *end == '\0' && std::isfinite(value), "invalid scalar: " + text);
    return value;
}

int add(int a, int b) {
    require(a >= 0 && b >= 0 && a <= INT_MAX - b, "shape addition overflow");
    return a + b;
}

int multiply(int a, int b) {
    require(a >= 0 && b >= 0 && (b == 0 || a <= INT_MAX / b), "shape multiplication overflow");
    return a * b;
}

int elements(const KokoroShape& shape) {
    return multiply(multiply(shape.w, shape.h), shape.c);
}

KokoroShape shape(int dims, int w, int h = 1, int c = 1) {
    require(dims >= 1 && dims <= 3 && w > 0 && h > 0 && c > 0 &&
            (dims >= 2 || h == 1) && (dims >= 3 || c == 1), "invalid logical shape");
    KokoroShape result = {dims, w, h, 1, c};
    elements(result);
    return result;
}

bool equal(const KokoroShape& a, const KokoroShape& b) {
    return a.dims == b.dims && a.w == b.w && a.h == b.h && a.d == b.d && a.c == b.c;
}

std::vector<std::string> words(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> values;
    for (std::string value; input >> value;) values.push_back(value);
    return values;
}

std::vector<int> array(const std::string& text) {
    std::vector<int> result;
    size_t first = 0;
    for (;;) {
        const size_t comma = text.find(',', first);
        result.push_back(integer(text.substr(first, comma == std::string::npos ? comma : comma - first)));
        require(result.size() <= 9000, "oversized parameter array");
        if (comma == std::string::npos) break;
        first = comma + 1;
    }
    require(!result.empty() && result.front() >= 0 && static_cast<size_t>(result.front()) == result.size() - 1,
            "array length prefix mismatch");
    result.erase(result.begin());
    return result;
}

struct Row {
    std::string type, name;
    std::vector<std::string> fields, bottoms, tops;
    std::map<int, std::string> params;
    size_t parameter_start;

    int get(int key, int fallback) const {
        const auto value = params.find(key);
        return value == params.end() ? fallback : integer(value->second);
    }
    float real(int key, float fallback) const {
        const auto value = params.find(key);
        return value == params.end() ? fallback : scalar(value->second);
    }
    std::vector<int> list(int key) const {
        const auto value = params.find(key);
        require(value != params.end(), "required array missing");
        return array(value->second);
    }
    void allowed(std::initializer_list<int> keys) const {
        const std::set<int> permitted(keys);
        for (const auto& parameter : params)
            require(parameter.first == -23330 || parameter.first == 31 || permitted.count(parameter.first),
                    "unsupported parameter " + std::to_string(parameter.first));
    }
    void arity(size_t inputs, size_t outputs = 1) const {
        require(bottoms.size() == inputs && tops.size() == outputs, "unsupported input/output arity");
    }
};

Row parse(const std::vector<std::string>& fields) {
    require(fields.size() >= 4, "incomplete layer row");
    Row row;
    row.fields = fields; row.type = fields[0]; row.name = fields[1];
    require(!row.name.empty() && row.name.size() < 256, "invalid layer name");
    const int inputs = integer(fields[2]), outputs = integer(fields[3]);
    require(inputs >= 0 && inputs <= 1739 && outputs > 0 && outputs <= 1739, "invalid layer arity");
    row.parameter_start = 4 + static_cast<size_t>(inputs) + static_cast<size_t>(outputs);
    require(fields.size() >= row.parameter_start, "missing layer inputs/outputs");
    row.bottoms.assign(fields.begin() + 4, fields.begin() + 4 + inputs);
    row.tops.assign(fields.begin() + 4 + inputs, fields.begin() + row.parameter_start);
    for (size_t i = 4; i < row.parameter_start; ++i)
        require(!fields[i].empty() && fields[i].size() < 256 && fields[i].find('=') == std::string::npos,
                "invalid blob name");
    for (size_t i = row.parameter_start; i < fields.size(); ++i) {
        const size_t equals = fields[i].find('=');
        require(equals != std::string::npos && equals > 0 && equals + 1 < fields[i].size() &&
                fields[i].find('=', equals + 1) == std::string::npos, "invalid parameter token");
        const int key = integer(fields[i].substr(0, equals));
        require(row.params.emplace(key, fields[i].substr(equals + 1)).second, "duplicate parameter");
    }
    require(row.get(31, 0) == 0 || row.get(31, 0) == 2, "unsupported feature mask");
    const auto hint = row.params.find(-23330);
    if (hint != row.params.end()) {
        const std::vector<int> old = array(hint->second);
        require(old.size() == 5 * row.tops.size(), "old shape hint count mismatch");
        for (size_t i = 0; i < old.size(); i += 5) {
            require(old[i+3] == 1, "unsupported old shape depth");
            shape(old[i], old[i+1], old[i+2], old[i+4]);
        }
    }
    return row;
}

std::vector<KokoroShape> infer(const Row& row, const std::vector<KokoroShape>& in, int t) {
    const std::string& kind = row.type;
    if (kind == "Input") {
        row.allowed({}); row.arity(0);
        require(row.tops[0] == row.name, "input name/top mismatch");
        if (row.name == "in0") return {shape(2, t, 512)};
        if (row.name == "in1" || row.name == "in2") return {shape(1, multiply(2, t))};
        if (row.name == "in3") return {shape(1, 256)};
        require(row.name == "in4", "unknown model input");
        return {shape(2, add(multiply(120, t), 1), 22)};
    }
    if (kind == "MemoryData") {
        row.allowed({0, 1}); row.arity(0);
        require(row.get(0, 0) == 1 && (row.get(1, 0) == 128 || row.get(1, 0) == 256),
                "unsupported constant tensor shape");
        return {shape(2, 1, row.get(1, 0))};
    }
    if (kind == "Split") {
        row.allowed({}); require(in.size() == 1, "Split requires one input");
        return std::vector<KokoroShape>(row.tops.size(), in[0]);
    }
    if (kind == "Crop") {
        row.allowed({-23309, -23310, -23311}); row.arity(1);
        require(equal(in[0], shape(1, 256)) && row.list(-23309) == std::vector<int>({0}) &&
                row.list(-23310) == std::vector<int>({128}) && row.list(-23311) == std::vector<int>({0}),
                "unsupported style crop");
        return {shape(1, 128)};
    }
    if (kind == "InnerProduct") {
        row.allowed({0, 1, 2}); row.arity(1);
        const int outputs = row.get(0, 0);
        require(equal(in[0], shape(1, 128)) && outputs > 0 && row.get(1, 0) == 1 &&
                row.get(2, 0) == multiply(128, outputs), "unsupported style projection");
        return {shape(1, outputs)};
    }
    if (kind == "Reshape") {
        row.allowed({0, 1, 2}); row.arity(1);
        require(row.params.count(0) && row.params.count(1), "unsupported reshape rank");
        const int dims = row.params.count(2) ? 3 : 2;
        std::vector<int> extents = {row.get(0, 0), row.get(1, 0)};
        if (dims == 3) extents.push_back(row.get(2, 0));
        int known = 1, inferred = -1;
        for (size_t i = 0; i < extents.size(); ++i) {
            if (extents[i] == -1) {
                require(inferred == -1, "multiple inferred reshape dimensions");
                inferred = static_cast<int>(i);
            } else {
                require(extents[i] > 0, "unsupported reshape zero/sentinel dimension");
                known = multiply(known, extents[i]);
            }
        }
        const int total = elements(in[0]);
        if (inferred >= 0) {
            require(total % known == 0, "nonintegral inferred reshape dimension");
            extents[static_cast<size_t>(inferred)] = total / known;
        } else require(total == known, "reshape changes element count");
        const KokoroShape result = shape(dims, extents[0], extents[1], dims == 3 ? extents[2] : 1);
        require(elements(result) == total, "reshape element count changed");
        return {result};
    }
    if (kind == "Slice") {
        row.allowed({-23300, 1}); require(in.size() == 1, "Slice requires one input");
        require(in[0].dims == 2 && row.get(1, 0) == 0, "unsupported slice axis/rank");
        const std::vector<int> sizes = row.list(-23300);
        require(sizes.size() == row.tops.size(), "slice count mismatch");
        int used = 0;
        std::vector<KokoroShape> results;
        for (size_t i = 0; i < sizes.size(); ++i) {
            int count = sizes[i];
            if (count == -233) {
                const int remaining_outputs = static_cast<int>(sizes.size() - i);
                require((in[0].h - used) % remaining_outputs == 0, "ambiguous uneven automatic slice");
                count = (in[0].h - used) / remaining_outputs;
            }
            require(count > 0 && count <= in[0].h - used, "slice exceeds input");
            results.push_back(shape(2, in[0].w, count));
            used = add(used, count);
        }
        require(used == in[0].h, "slice does not cover input");
        return results;
    }
    if (kind == "Convolution1D" || kind == "Deconvolution1D" || kind == "DeconvolutionDepthWise1D") {
        const bool transpose = kind != "Convolution1D", grouped = kind == "DeconvolutionDepthWise1D";
        if (grouped) row.allowed({0, 1, 2, 3, 4, 5, 6, 7, 15, 18});
        else if (transpose) row.allowed({0, 1, 2, 3, 4, 5, 6, 15, 18});
        else row.allowed({0, 1, 2, 3, 4, 5, 6, 15});
        row.arity(1); require(in[0].dims == 2, "1D convolution requires rank two");
        const int outputs = row.get(0, 0), kernel = row.get(1, 0), dilation = row.get(2, 1), stride = row.get(3, 1);
        const int left = row.get(4, 0), right = row.get(15, left), bias = row.get(5, 0);
        const int group = grouped ? row.get(7, 1) : 1;
        require(outputs > 0 && kernel > 0 && dilation > 0 && stride > 0 && left >= 0 && right >= 0 &&
                (bias == 0 || bias == 1) && group > 0 && in[0].h % group == 0 && outputs % group == 0,
                "invalid convolution dimensions/groups or unsupported automatic padding");
        require(row.get(6, 0) == multiply(multiply(outputs, in[0].h / group), kernel),
                "convolution weight count disagrees with channels/kernel");
        const int effective = add(multiply(dilation, kernel - 1), 1);
        int width = 0;
        if (transpose) {
            const int extra = row.get(18, 0);
            require(extra >= 0 && extra < stride, "unsupported deconvolution output padding");
            const int bordered = add(add(multiply(in[0].w - 1, stride), effective), extra);
            const int crop = add(left, right);
            require(bordered > crop, "deconvolution cropping removes all output");
            width = bordered - crop;
        } else {
            const int padded = add(add(in[0].w, left), right);
            require(padded >= effective, "convolution kernel exceeds padded width");
            width = add((padded - effective) / stride, 1);
        }
        return {shape(2, width, outputs)};
    }
    if (kind == "Concat") {
        row.allowed({0}); require(!in.empty() && row.tops.size() == 1 && row.get(0, 0) == 0, "unsupported concat axis/arity");
        int height = 0;
        for (const KokoroShape& bottom : in) {
            require(bottom.dims == 2 && bottom.w == in[0].w, "incompatible concat shapes");
            height = add(height, bottom.h);
        }
        return {shape(2, in[0].w, height)};
    }
    if (kind == "Reduction") {
        row.allowed({0, 1, -23303, 4, 5}); row.arity(1);
        require(in[0].dims == 2 && row.get(0, 0) == 3 && row.get(1, 1) == 0 &&
                row.list(-23303) == std::vector<int>({1}) && row.get(4, 0) == 1 && row.get(5, 0) == 1,
                "unsupported reduction parameters");
        return {shape(2, 1, in[0].h)};
    }
    if (kind == "InstanceNorm") {
        row.allowed({0, 1, 2}); row.arity(1);
        require(in[0].dims == 3 && in[0].h == 1 && row.get(0, 0) == in[0].c &&
                row.real(1, 0.001f) == 1e-5f && row.get(2, 1) == 0 && row.get(31, 0) == 2,
                "unsupported InstanceNorm shape, affine, epsilon, or storage mask");
        return {in[0]};
    }
    if (kind == "RadishSnake") {
        row.allowed({}); row.arity(3);
        require(row.get(31, 0) == 0 && in[0].dims == 2 &&
                (in[0].h == 128 || in[0].h == 256) &&
                equal(in[1], shape(2, 1, in[0].h)) && equal(in[2], in[1]),
                "unsupported RadishSnake shape or storage mask");
        return {in[0]};
    }
    if (kind == "BinaryOp") {
        row.allowed({0, 1, 2});
        const int operation = row.get(0, 0), with_scalar = row.get(1, 0);
        require(operation >= 0 && operation <= 3 && (with_scalar == 0 || with_scalar == 1), "unsupported binary operation");
        row.arity(with_scalar ? 1 : 2);
        require(in[0].dims == 2, "unsupported binary rank");
        if (with_scalar) { row.real(2, 0.f); return {in[0]}; }
        require(!row.params.count(2) && in[1].dims == 2 && in[0].h == in[1].h &&
                (in[0].w == in[1].w || in[0].w == 1 || in[1].w == 1), "unsupported binary broadcast");
        return {shape(2, std::max(in[0].w, in[1].w), in[0].h)};
    }
    if (kind == "UnaryOp" || kind == "ReLU") {
        row.allowed({0}); row.arity(1); require(in[0].dims == 2, "unsupported unary rank");
        if (kind == "UnaryOp") {
            const int operation = row.get(0, 0);
            require(operation == 4 || operation == 5 || operation == 9, "unsupported unary operation");
        } else require(row.real(0, 0.f) >= 0, "unsupported ReLU slope");
        return {in[0]};
    }
    if (kind == "Interp") {
        row.allowed({0, 1, 2, 6}); row.arity(1);
        require(in[0].dims == 2 && row.get(0, 0) == 1 && row.real(1, 1.f) == 1.f &&
                row.real(2, 1.f) == 2.f && row.get(6, 0) == 0, "unsupported interpolation metadata");
        return {shape(2, multiply(2, in[0].w), in[0].h)};
    }
    if (kind == "Padding") {
        row.allowed({0, 1, 2, 3, 4, 5, 6}); row.arity(1);
        require(in[0].dims == 2 && row.get(0, 0) == 0 && row.get(1, 0) == 0 && row.get(2, 0) == 1 &&
                row.get(3, 0) == 0 && row.get(4, 0) == 2 && row.real(5, 0.f) == 0.f && row.get(6, 0) == 0,
                "unsupported padding metadata");
        require(in[0].w > 1, "reflection padding requires width greater than one");
        return {shape(2, add(in[0].w, 1), in[0].h)};
    }
    throw std::runtime_error("unsupported operator " + kind);
}

std::map<std::string, int> inventory(bool fused, bool snake) {
    std::map<std::string, int> result = {{"Input", 5}, {"Split", fused ? 110 : 164}, {"Crop", 1}, {"InnerProduct", 58},
            {"Reshape", fused ? 168 : 60}, {"Slice", 58}, {"Convolution1D", 69}, {"Concat", 5},
            {fused ? "InstanceNorm" : "Reduction", fused ? 54 : 108}, {"BinaryOp", fused ? 360 : 576},
            {"UnaryOp", fused ? 96 : 150}, {"ReLU", 13}, {"MemoryData", 96}, {"Interp", 1},
            {"DeconvolutionDepthWise1D", 1}, {"Deconvolution1D", 2}, {"Padding", 1}};
    if (snake) {
        result["BinaryOp"] = 216;
        result.erase("UnaryOp");
        result["RadishSnake"] = 48;
    }
    return result;
}

} // namespace

KokoroShapePlan infer_kokoro_shapes(const std::string& text, int t) {
    require(t >= 1 && t <= 4096, "T must be between 1 and 4096");
    require(!text.empty() && text.size() <= 2 * 1024 * 1024 && text.find('\0') == std::string::npos,
            "invalid parameter text size or embedded NUL");
    std::istringstream input(text);
    std::string line;
    require(bool(std::getline(input, line)) && words(line) == std::vector<std::string>({"7767517"}), "wrong ncnn magic");
    require(bool(std::getline(input, line)), "missing graph header");
    const std::vector<std::string> header = words(line);
    require(header.size() == 2, "malformed graph header");
    KokoroShapePlan plan;
    plan.layers = integer(header[0]); plan.blobs = integer(header[1]);
    const bool snake = plan.layers == 906 && plan.blobs == 1067;
    const bool fused = snake || (plan.layers == 1098 && plan.blobs == 1307);
    require(fused || (plan.layers == 1368 && plan.blobs == 1739), "unsupported graph inventory");
    std::map<std::string, KokoroShape> known;
    std::map<std::string, int> counts;
    std::set<std::string> layer_names, inputs;
    std::ostringstream output;
    output << "7767517\n" << plan.layers << ' ' << plan.blobs << '\n';
    int layer_index = 0;
    while (std::getline(input, line)) {
        const std::vector<std::string> fields = words(line);
        if (fields.empty()) continue;
        require(layer_index < plan.layers, "too many graph layers");
        try {
            const Row row = parse(fields);
            require(layer_names.insert(row.name).second, "duplicate layer name");
            ++counts[row.type];
            if (row.type == "Input") require(inputs.insert(row.name).second, "duplicate input");
            std::vector<KokoroShape> bottoms;
            for (const std::string& name : row.bottoms) {
                const auto value = known.find(name);
                require(value != known.end(), "missing/non-topological input " + name);
                bottoms.push_back(value->second);
            }
            const std::vector<KokoroShape> tops = infer(row, bottoms, t);
            require(tops.size() == row.tops.size(), "propagated output arity differs");
            for (size_t i = 0; i < tops.size(); ++i) {
                require(known.size() < static_cast<size_t>(plan.blobs) && known.emplace(row.tops[i], tops[i]).second,
                        "duplicate output or excess blob count");
                plan.shapes.push_back({layer_index, static_cast<int>(plan.shapes.size()), row.tops[i], tops[i]});
            }
            for (size_t i = 0; i < row.fields.size(); ++i) {
                if (i >= row.parameter_start && integer(row.fields[i].substr(0, row.fields[i].find('='))) == -23330) continue;
                if (i) output << ' ';
                output << row.fields[i];
            }
            output << " -23330=" << 5 * tops.size();
            for (const KokoroShape& top : tops)
                output << ',' << top.dims << ',' << top.w << ',' << top.h << ',' << top.d << ',' << top.c;
            output << '\n';
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("layer " + std::to_string(layer_index) + " " +
                                     (fields.size() > 1 ? fields[1] : "<malformed>") + ": " + error.what());
        }
        ++layer_index;
    }
    require(layer_index == plan.layers && known.size() == static_cast<size_t>(plan.blobs), "incomplete graph inventory");
    require(counts == inventory(fused, snake) && inputs == std::set<std::string>({"in0", "in1", "in2", "in3", "in4"}),
            "operator/input inventory differs from audited graph");
    const auto result = known.find("out0");
    require(result != known.end() && equal(result->second, shape(2, add(multiply(120, t), 1), 22)),
            "final core shape differs from 22*(120*T+1)");
    plan.param_text = output.str();
    return plan;
}

} // namespace radish
