#pragma once
#include <array>
#include <charconv>
#include <cstdio>
#include <openfilter/parameters/Descriptor.hpp>
#include <string_view>

namespace openfilter::gate {
enum Index : unsigned {
    Bypass,
    Output,
    Threshold,
    Ratio,
    Attack,
    Release,
    Knee,
    Range,
    WetGain,
    Mix,
    Input,
    Lookahead,
    Hold,
    SidechainHP,
    StereoLink,
    Detector,
    Hysteresis,
    Sidechain,
    SidechainLP,
    Audition,
    WetPan,
    DryPan,
    DryGain,
    parameterCount
};
using Values = std::array<double, parameterCount>;
inline Parameter parameter(unsigned i) {
    // IDs and physical units are permanent. Log mappings are UI-only.
    static constexpr std::array<Parameter, parameterCount> p{
        {{0, "Bypass", 0, 1, 0, true, false},
         {1, "Output", -24, 24, 0, false, true},
         {2, "Threshold", -80, 0, -36, false, true},
         {3, "Ratio", 1, 100, 4, false, true},
         {4, "Attack", 0, 1000, 1, false, true},
         {5, "Release", 1, 5000, 150, false, true},
         {6, "Knee", 0, 30, 6, false, true},
         {7, "Range", 0, 100, 60, false, true},
         {8, "Wet gain", -24, 24, 0, false, true},
         {9, "Mix", 0, 100, 100, false, true},
         {10, "Input", -24, 24, 0, false, true},
         {11, "Lookahead", 0, 10, 0, false, true},
         {12, "Hold", 0, 1000, 25, false, true},
         {13, "Sidechain HP", 0, 5000, 0, false, true},
         {14, "Stereo Link", 0, 100, 100, false, true},
         {15, "Detector", 0, 1, 0, true, false},
         {16, "Hysteresis", 0, 24, 3, false, true},
         {17, "Sidechain", 0, 1, 0, true, false},
         {18, "Sidechain LP", 20, 20000, 20000, false, true},
         {19, "Audition", 0, 1, 0, true, false},
         {20, "Wet pan", -100, 100, 0, false, true},
         {21, "Dry pan", -100, 100, 0, false, true},
         {22, "Dry gain", -60, 24, 0, false, true}}};
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
    return i == Ratio || i == Release || i == SidechainLP;
}
inline double normalized(unsigned i, double v) {
    const auto p = parameter(i);
    if (i == Attack)
        return std::log1p(v * 10) / std::log1p(p.max * 10);
    return logarithmic(i) ? std::log(v / p.min) / std::log(p.max / p.min)
                          : (v - p.min) / (p.max - p.min);
}
inline double denormalized(unsigned i, double n) {
    const auto p = parameter(i);
    n = std::clamp(n, 0., 1.);
    if (i == Attack)
        return std::expm1(n * std::log1p(p.max * 10)) / 10;
    return logarithmic(i) ? p.min * std::pow(p.max / p.min, n) : p.min + n * (p.max - p.min);
}
inline const char *unit(unsigned i) {
    switch (i) {
    case Output:
    case Threshold:
    case Knee:
    case Range:
    case Hysteresis:
    case DryGain:
    case WetGain:
    case Input:
        return "dB";
    case Attack:
    case Release:
    case Lookahead:
    case Hold:
        return "ms";
    case SidechainLP:
    case SidechainHP:
        return "Hz";
    case WetPan:
    case DryPan:
    case Mix:
    case StereoLink:
        return "%";
    case Ratio:
        return ":1";
    default:
        return "";
    }
}
inline void format(unsigned i, double v, char *out, size_t size) {
    if (parameter(i).stepped) {
        const char *s = i == Detector    ? (v ? "RMS" : "Peak")
                        : i == Sidechain ? (v ? "External" : "Internal")
                                         : (v ? "On" : "Off");
        std::snprintf(out, size, "%s", s);
        return;
    }
    char buffer[64];
    auto r =
        std::to_chars(buffer, buffer + 63, v, std::chars_format::fixed,
                      i == Mix || i == StereoLink || i == SidechainHP || i == SidechainLP ? 0 : 1);
    if (r.ec != std::errc{}) {
        if (size)
            *out = 0;
        return;
    }
    *r.ptr = 0;
    std::snprintf(out, size, "%s%s%s", buffer, i == Ratio ? "" : " ", unit(i));
}
inline bool parse(unsigned i, std::string_view s, double &v) {
    while (!s.empty() && s.front() == ' ')
        s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ')
        s.remove_suffix(1);
    if ((i == SidechainHP || i == SidechainLP) && s == "Off") {
        v = i == SidechainHP ? 0 : 20000;
        return true;
    }
    if (parameter(i).stepped) {
        if ((i == Detector && s == "Peak") || (i == Sidechain && s == "Internal") ||
            (i != Detector && i != Sidechain && s == "Off")) {
            v = 0;
            return true;
        }
        if ((i == Detector && s == "RMS") || (i == Sidechain && s == "External") ||
            (i != Detector && i != Sidechain && s == "On")) {
            v = 1;
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
    if (!suffix.empty() && suffix != unit(i))
        return false;
    v = parameter(i).constrain(v);
    return true;
}
} // namespace openfilter::gate
