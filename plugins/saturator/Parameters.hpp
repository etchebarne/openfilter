#pragma once
#include <array>
#include <charconv>
#include <cstdio>
#include <openfilter/parameters/Descriptor.hpp>
#include <string_view>
namespace openfilter::saturator {
enum Global : unsigned {
    Bypass,
    Output,
    Input,
    Mix,
    CrossoverLow,
    CrossoverHigh,
    Compensation,
    globals
};
enum Field : unsigned {
    Enabled,
    Style,
    Drive,
    BandMix,
    Level,
    Dynamics,
    Bass,
    Mid,
    Treble,
    Presence,
    Solo,
    Mute,
    stride
};
constexpr unsigned band(unsigned b, unsigned f) {
    return globals + b * stride + f;
}
constexpr unsigned parameterCount = globals + 3 * stride;
using Values = std::array<double, parameterCount>;
inline Parameter parameter(unsigned i) {
    static constexpr std::array<Parameter, globals> global{
        {{0, "Bypass", 0, 1, 0, true, false},
         {1, "Output", -24, 24, 0, false, true},
         {2, "Input", -24, 24, 0, false, true},
         {3, "Mix", 0, 100, 100, false, true},
         {4, "Low crossover", 40, 4000, 250, false, true},
         {5, "High crossover", 200, 18000, 4000, false, true},
         {6, "Compensation", 0, 100, 100, false, true}}};
    static constexpr std::array<Parameter, stride> fields{
        {{0, "Enabled", 0, 1, 1, true, false},
         {0, "Style", 0, 3, 0, true, false},
         {0, "Drive", 0, 36, 6, false, true},
         {0, "Band mix", 0, 100, 100, false, true},
         {0, "Level", -24, 24, 0, false, true},
         {0, "Dynamics", -100, 100, 0, false, true},
         {0, "Bass", -12, 12, 0, false, true},
         {0, "Mid", -12, 12, 0, false, true},
         {0, "Treble", -12, 12, 0, false, true},
         {0, "Presence", -12, 12, 0, false, true},
         {0, "Solo", 0, 1, 0, true, false},
         {0, "Mute", 0, 1, 0, true, false}}};
    if (i < globals)
        return global[i];
    auto p = fields[(i - globals) % stride];
    p.id = i;
    return p;
}
inline int indexForId(uint32_t id) {
    return id < parameterCount ? int(id) : -1;
}
inline Values defaults() {
    Values v{};
    for (unsigned i = 0; i < parameterCount; ++i)
        v[i] = parameter(i).initial;
    return v;
}
inline bool logarithmic(unsigned i) {
    return i == CrossoverLow || i == CrossoverHigh;
}
inline double normalized(unsigned i, double v) {
    const auto p = parameter(i);
    return logarithmic(i) ? std::log(v / p.min) / std::log(p.max / p.min)
                          : (v - p.min) / (p.max - p.min);
}
inline double denormalized(unsigned i, double n) {
    const auto p = parameter(i);
    n = std::clamp(n, 0., 1.);
    return logarithmic(i) ? p.min * std::pow(p.max / p.min, n) : p.min + n * (p.max - p.min);
}
inline const char *unit(unsigned i) {
    if (logarithmic(i))
        return "Hz";
    if (i == Mix || i == Compensation ||
        (i >= globals && ((i - globals) % stride == BandMix || (i - globals) % stride == Dynamics)))
        return "%";
    return parameter(i).stepped ? "" : "dB";
}
inline constexpr const char *styles[]{"Soft", "Rounded", "Dense", "Asymmetric"};
inline void format(unsigned i, double v, char *out, size_t size) {
    if (parameter(i).stepped) {
        std::snprintf(out, size, "%s",
                      i >= globals && (i - globals) % stride == Style
                          ? styles[unsigned(std::clamp(v, 0., 3.))]
                      : v ? "On"
                          : "Off");
        return;
    }
    char buffer[64];
    auto r = std::to_chars(buffer, buffer + 63, v, std::chars_format::fixed,
                           logarithmic(i) || std::string_view(unit(i)) == "%" ? 0 : 1);
    if (r.ec != std::errc{}) {
        if (size)
            *out = 0;
        return;
    }
    *r.ptr = 0;
    std::snprintf(out, size, "%s %s", buffer, unit(i));
}
inline bool parse(unsigned i, std::string_view s, double &v) {
    while (!s.empty() && s.front() == ' ')
        s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ')
        s.remove_suffix(1);
    if (parameter(i).stepped) {
        if (i >= globals && (i - globals) % stride == Style) {
            for (unsigned k = 0; k < 4; ++k)
                if (s == styles[k]) {
                    v = k;
                    return true;
                }
        } else {
            if (s == "Off") {
                v = 0;
                return true;
            }
            if (s == "On") {
                v = 1;
                return true;
            }
        }
    }
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    if (s.empty())
        return false;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || !std::isfinite(v))
        return false;
    std::string_view suffix(r.ptr, s.data() + s.size() - r.ptr);
    while (!suffix.empty() && suffix.front() == ' ')
        suffix.remove_prefix(1);
    if (!suffix.empty() && suffix != unit(i))
        return false;
    v = parameter(i).constrain(v);
    return true;
}
} // namespace openfilter::saturator
