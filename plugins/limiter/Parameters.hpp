#pragma once
#include <array>
#include <charconv>
#include <cstdio>
#include <openfilter/parameters/Descriptor.hpp>
#include <string_view>

namespace openfilter::limiter {
enum Index : unsigned {
    Bypass,
    Gain,
    Ceiling,
    Release,
    StereoLink,
    AutoRelease,
    UnityGain,
    Style,
    Lookahead,
    Attack,
    ReleaseLink,
    TruePeak,
    parameterCount
};
enum StyleValue : unsigned { Legacy, Clean, Punch, Dense };
inline constexpr const char *styleNames[]{"Legacy", "Clean", "Punch", "Dense"};
using Values = std::array<double, parameterCount>;
inline Parameter parameter(unsigned i) {
    static constexpr std::array<Parameter, parameterCount> p{
        {{0, "Bypass", 0, 1, 0, true, false},
         {1, "Gain", 0, 36, 0, false, true},
         {2, "Ceiling", -24, 0, -1, false, true},
         {3, "Release", 20, 2000, 200, false, true},
         {4, "Stereo Link", 0, 100, 100, false, true},
         {5, "Auto Release", 0, 1, 0, true, false},
         {6, "Unity Gain", 0, 1, 0, true, false},
         {7, "Style", 0, 3, Clean, true, false},
         {8, "Lookahead", 0, 5, 5, false, true},
         {9, "Attack", .1, 1000, 100, false, true},
         {10, "Release Link", 0, 100, 100, false, true},
         {11, "True Peak", 0, 1, 1, true, false}}};
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
inline double normalized(unsigned i, double v) {
    const auto p = parameter(i);
    return i == Release || i == Attack ? std::log(v / p.min) / std::log(p.max / p.min)
                                       : (v - p.min) / (p.max - p.min);
}
inline double denormalized(unsigned i, double n) {
    const auto p = parameter(i);
    n = std::clamp(n, 0., 1.);
    return i == Release || i == Attack ? p.min * std::pow(p.max / p.min, n)
                                       : p.min + n * (p.max - p.min);
}
inline const char *unit(unsigned i) {
    return i == Gain                                       ? "dB"
           : i == Ceiling                                  ? "dBFS"
           : i == Release || i == Lookahead || i == Attack ? "ms"
           : i == StereoLink || i == ReleaseLink           ? "%"
                                                           : "";
}
inline void format(unsigned i, double v, char *out, size_t size) {
    if (i == Style) {
        std::snprintf(out, size, "%s",
                      styleNames[static_cast<unsigned>(parameter(i).constrain(v))]);
        return;
    }
    if (parameter(i).stepped) {
        std::snprintf(out, size, "%s", v ? "On" : "Off");
        return;
    }
    char buffer[64];
    auto r = std::to_chars(buffer, buffer + 63, v, std::chars_format::fixed,
                           i == StereoLink || i == ReleaseLink ? 0 : 1);
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
    if (i == Style)
        for (unsigned n = 0; n < 4; ++n)
            if (s == styleNames[n]) {
                v = n;
                return true;
            }
    if (i != Style && parameter(i).stepped && (s == "On" || s == "Off")) {
        v = s == "On";
        return true;
    }
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    if (s.empty())
        return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
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
} // namespace openfilter::limiter
