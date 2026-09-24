#pragma once
#include <array>
#include <charconv>
#include <cstdio>
#include <openfilter/parameters/Descriptor.hpp>
#include <string_view>

namespace openfilter::reverb {
enum Index : unsigned {
    Bypass,
    Output,
    Space,
    DecayRate,
    Predelay,
    Character,
    Thickness,
    Distance,
    Brightness,
    Width,
    Ducking,
    Mix,
    Input,
    Style,
    Freeze,
    AutoGate,
    GateHold,
    PredelaySync,
    PredelayOffset,
    MixLock,
    globals
};
enum BandField : unsigned { Enabled, Frequency, Amount, Q, Type, fields };
inline constexpr unsigned bands = 6, parameterCount = globals + 2 * bands * fields;
inline constexpr unsigned bandIndex(bool post, unsigned band, unsigned field) {
    return globals + (unsigned(post) * bands + band) * fields + field;
}
using Values = std::array<double, parameterCount>;
inline Parameter parameter(unsigned i) {
    static constexpr std::array<Parameter, globals> p{
        {{Bypass, "Bypass", 0, 1, 0, true, false},
         {Output, "Output", -24, 24, 0, false, true},
         {Space, "Space", .2, 10, 2.5, false, true},
         {DecayRate, "Decay Rate", 25, 400, 100, false, true},
         {Predelay, "Predelay", 0, 500, 20, false, true},
         {Character, "Character", 0, 100, 25, false, true},
         {Thickness, "Thickness", 0, 100, 60, false, true},
         {Distance, "Distance", 0, 100, 35, false, true},
         {Brightness, "Brightness", 0, 100, 60, false, true},
         {Width, "Width", 0, 200, 100, false, true},
         {Ducking, "Ducking", 0, 100, 0, false, true},
         {Mix, "Mix", 0, 100, 25, false, true},
         {Input, "Input", -24, 24, 0, false, true},
         {Style, "Style", 0, 2, 0, true, false},
         {Freeze, "Freeze", 0, 1, 0, true, false},
         {AutoGate, "Auto Gate", 0, 1, 0, true, false},
         {GateHold, "Gate Hold", 10, 2000, 250, false, true},
         {PredelaySync, "Predelay Sync", 0, 4, 0, true, false},
         {PredelayOffset, "Predelay Offset", 50, 200, 100, false, true},
         {MixLock, "Lock Mix", 0, 1, 0, true, false}}};
    if (i < globals)
        return p[i];
    const auto field = (i - globals) % fields;
    const bool post = i >= globals + bands * fields;
    const unsigned b = (i - globals) / fields % bands;
    constexpr double frequencies[]{100, 300, 800, 2000, 6000, 12000};
    switch (field) {
    case Enabled:
        return {i, "Enabled", 0, 1, 0, true, false};
    case Frequency:
        return {i, "Frequency", 20, 20000, frequencies[b], false, true};
    case Amount:
        return post ? Parameter{i, "Gain", -24, 24, 0, false, true}
                    : Parameter{i, "Decay", 25, 400, 100, false, true};
    case Q:
        return {i, "Q", .2, 10, .7071067811865476, false, true};
    default:
        return {i, "Shape", 0, post ? 5. : 2., 0, true, false};
    }
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
    return i == Space || i == DecayRate || i == GateHold ||
           (i >= globals && ((i - globals) % fields == Frequency || (i - globals) % fields == Q ||
                             ((i - globals) % fields == Amount && i < globals + bands * fields)));
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
inline const char *choice(unsigned i, unsigned n) {
    constexpr const char *styles[]{"Modern", "Vintage", "Plate"};
    constexpr const char *sync[]{"Off", "1/4", "1/8", "1/16", "1/32"};
    constexpr const char *shapes[]{"Bell",    "Low shelf", "High shelf",
                                   "Low cut", "High cut",  "Notch"};
    if (i == Style)
        return styles[std::min(n, 2u)];
    if (i == PredelaySync)
        return sync[std::min(n, 4u)];
    if (i >= globals && (i - globals) % fields == Type)
        return shapes[std::min(n, 5u)];
    return n ? "On" : "Off";
}
inline const char *unit(unsigned i) {
    if (i == Space)
        return "s";
    if (i == Output || i == Input)
        return "dB";
    if (i == Predelay || i == GateHold)
        return "ms";
    if (i >= globals) {
        if ((i - globals) % fields == Frequency)
            return "Hz";
        if ((i - globals) % fields == Amount)
            return i >= globals + bands * fields ? "dB" : "%";
        return "";
    }
    return parameter(i).stepped ? "" : "%";
}
inline void format(unsigned i, double v, char *out, size_t size) {
    if (parameter(i).stepped) {
        std::snprintf(out, size, "%s", choice(i, unsigned(v)));
        return;
    }
    char buffer[64];
    auto r = std::to_chars(buffer, buffer + 63, v, std::chars_format::fixed,
                           i == Space || (i >= globals && (i - globals) % fields == Q) ? 2 : 1);
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
    if (parameter(i).stepped)
        for (unsigned n = 0; n <= parameter(i).max; ++n)
            if (s == choice(i, n)) {
                v = n;
                return true;
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
    if (suffix == "kHz" && std::string_view(unit(i)) == "Hz")
        v *= 1000;
    else if (!suffix.empty() && suffix != unit(i))
        return false;
    v = parameter(i).constrain(v);
    return true;
}
} // namespace openfilter::reverb
