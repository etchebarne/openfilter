#pragma once
#include <cmath>
#include <stdexcept>
#include <string>
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) +      \
                                     " " #condition);                                              \
    } while (false)
inline void near(double a, double b, double tolerance = 1e-10) {
    CHECK(std::abs(a - b) <= tolerance);
}
