#pragma once
#include <array>
#include <charconv>
#include <cstdio>
#include <openfilter/parameters/Descriptor.hpp>
#include <string_view>

namespace openfilter::deesser {
enum Index : unsigned {
    Bypass,
    Output,
    Threshold,
    Range,
    Low,
    High,
    Lookahead,
    StereoLink,
    Detection,
    Processing,
    Audition,
    Sidechain,
    Attack,
    Release,
    Input,
    parameterCount
};
using Values = std::array<double, parameterCount>;
inline Parameter parameter(unsigned i) {
    static constexpr std::array<Parameter, parameterCount> p{
        {{0, "Bypass", 0, 1, 0, true, false},
         {1, "Output", -24, 24, 0, false, true},
         {2, "Threshold", -60, 0, -30, false, true},
         {3, "Range", 0, 24, 6, false, true},
         {4, "Detection Low", 1000, 20000, 6000, false, true},
         {5, "Detection High", 1010, 22000, 14000, false, true},
         {6, "Lookahead", 0, 15, 5, false, true},
         {7, "Stereo Link", 0, 100, 100, false, true},
         {8, "Detection", 0, 1, 0, true, false},
         {9, "Processing", 0, 1, 1, true, false},
         {10, "Audition", 0, 1, 0, true, false},
         {11, "Sidechain", 0, 1, 0, true, false},
         {12, "Attack", .1, 10, .5, false, true},
         {13, "Release", 10, 500, 80, false, true},
         {14, "Input", -24, 24, 0, false, true}}};
    return p[i < parameterCount ? i : 0];
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
    return i == Low || i == High || i == Attack || i == Release;
}
inline double normalized(unsigned i, double v) {
    const auto p = parameter(i);
    v = p.constrain(v);
    return logarithmic(i) ? std::log(v / p.min) / std::log(p.max / p.min)
                          : (v - p.min) / (p.max - p.min);
}
inline double denormalized(unsigned i, double n) {
    const auto p = parameter(i);
    n = std::clamp(n, 0., 1.);
    return logarithmic(i) ? p.min * std::pow(p.max / p.min, n) : p.min + n * (p.max - p.min);
}
inline const char *unit(unsigned i) {
    switch (i) {
    case Input:
    case Output:
    case Threshold:
    case Range:
        return "dB";
    case Low:
    case High:
        return "Hz";
    case Lookahead:
    case Attack:
    case Release:
        return "ms";
    case StereoLink:
        return "%";
    default:
        return "";
    }
}
inline const char *choice(unsigned i, bool on) {
    if (i == Detection)
        return on ? "Allround" : "Vocal";
    if (i == Processing)
        return on ? "Split Band" : "Wide Band";
    if (i == Sidechain)
        return on ? "External" : "Internal";
    return on ? "On" : "Off";
}
inline void format(unsigned i, double v, char *out, size_t size) {
    if (parameter(i).stepped) {
        std::snprintf(out, size, "%s", choice(i, v >= .5));
        return;
    }
    char buffer[64];
    auto r = std::to_chars(buffer, buffer + 63, v, std::chars_format::fixed,
                           i == Low || i == High || i == StereoLink ? 0 : 1);
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
        for (unsigned k = 0; k < 2; ++k)
            if (s == choice(i, k)) {
                v = k;
                return true;
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
    if ((i == Low || i == High) && suffix == "kHz")
        v *= 1000;
    else if (!suffix.empty() && suffix != unit(i))
        return false;
    v = parameter(i).constrain(v);
    return true;
}
} // namespace openfilter::deesser
