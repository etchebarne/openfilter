#pragma once
#include <array>
#include <cmath>
#include <openfilter/parameters/Descriptor.hpp>

namespace openfilter::eq {
inline constexpr unsigned bands = 24, fields = 7, parameterCount = 2 + bands * fields;
enum Field : unsigned { Enabled, Type, Frequency, Gain, Q, Slope, Routing };
enum class Route { Stereo, Left, Right, Mid, Side };
using Values = std::array<double, parameterCount>;
inline constexpr unsigned index(unsigned band, Field field) {
    return 2 + band * fields + field;
}
inline constexpr uint32_t bandId(unsigned band, Field field) {
    return 0x100 + band * 16 + field;
}
inline constexpr int indexForId(uint32_t id) {
    if (id < 2)
        return static_cast<int>(id);
    if (id < 0x100 || (id - 0x100) / 16 >= bands || (id - 0x100) % 16 >= fields)
        return -1;
    return static_cast<int>(2 + (id - 0x100) / 16 * fields + (id - 0x100) % 16);
}
inline Parameter parameter(unsigned i) {
    if (i == 0)
        return {0, "Bypass", 0, 1, 0, true, false};
    if (i == 1)
        return {1, "Output", -24, 24, 0, false, true};
    const unsigned b = (i - 2) / fields;
    const auto f = static_cast<Field>((i - 2) % fields);
    const auto id = bandId(b, f);
    switch (f) {
    case Enabled:
        return {id, "Enabled", 0, 1, 0, true, false};
    case Type:
        return {id, "Type", 0, 5, 0, true, false};
    case Frequency:
        return {id,    "Frequency", std::log2(10.0), std::log2(30000.0), std::log2(1000.0),
                false, true};
    case Gain:
        return {id, "Gain", -24, 24, 0, false, true};
    case Q:
        return {id, "Q", std::log2(0.1), std::log2(40.0), std::log2(std::sqrt(0.5)), false, true};
    case Slope:
        return {id, "Slope", 0, 3, 0, true, false};
    case Routing:
        return {id, "Routing", 0, 4, 0, true, false};
    }
    return {id, "Invalid", 0, 1, 0};
}
inline bool isBrickwall(int type, int slope) {
    return (type == 3 || type == 4) && slope == 3;
}
inline unsigned stageCount(int type, int slope) {
    return type == 3 || type == 4 ? (slope == 3 ? 10u : 1u << slope) : 1u;
}
inline Values defaults() {
    Values result{};
    for (unsigned i = 0; i < parameterCount; ++i)
        result[i] = parameter(i).initial;
    return result;
}
inline constexpr std::array<const char *, 6> shapeNames{"Bell",    "Low Shelf", "High Shelf",
                                                        "Low Cut", "High Cut",  "Notch"};
inline constexpr std::array<const char *, 5> routeNames{"Stereo", "Left", "Right", "Mid", "Side"};
inline constexpr std::array<const char *, 4> slopeNames{"12 dB/oct", "24 dB/oct", "48 dB/oct",
                                                        "Brickwall"};
} // namespace openfilter::eq
