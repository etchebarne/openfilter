#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openfilter {
struct Parameter {
    uint32_t id;
    const char *name;
    double min, max, initial;
    bool stepped = false;
    bool modulatable = false;
    double constrain(double value) const noexcept {
        if (!std::isfinite(value))
            return initial;
        value = std::clamp(value, min, max);
        return stepped ? std::trunc(value) : value;
    }
};
} // namespace openfilter
