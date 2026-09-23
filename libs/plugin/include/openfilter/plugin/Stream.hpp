#pragma once
#include <clap/stream.h>
#include <cstddef>
#include <cstdint>

namespace openfilter::plugin {
inline bool readAll(const clap_istream *stream, void *data, size_t size) noexcept {
    if (!stream || !stream->read)
        return false;
    auto *bytes = static_cast<uint8_t *>(data);
    while (size) {
        const auto n = stream->read(stream, bytes, size);
        if (n <= 0 || static_cast<uint64_t>(n) > size)
            return false;
        bytes += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}
inline bool writeAll(const clap_ostream *stream, const void *data, size_t size) noexcept {
    if (!stream || !stream->write)
        return false;
    const auto *bytes = static_cast<const uint8_t *>(data);
    while (size) {
        const auto n = stream->write(stream, bytes, size);
        if (n <= 0 || static_cast<uint64_t>(n) > size)
            return false;
        bytes += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}
inline void put32(uint8_t *p, uint32_t n) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = static_cast<uint8_t>(n >> (8 * i));
}
inline uint32_t get32(const uint8_t *p) noexcept {
    uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i)
        n |= uint32_t(p[i]) << (8 * i);
    return n;
}
inline void put64(uint8_t *p, uint64_t n) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        p[i] = static_cast<uint8_t>(n >> (8 * i));
}
inline uint64_t get64(const uint8_t *p) noexcept {
    uint64_t n = 0;
    for (unsigned i = 0; i < 8; ++i)
        n |= uint64_t(p[i]) << (8 * i);
    return n;
}
inline uint32_t checksum(const uint8_t *p, size_t size) noexcept {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
} // namespace openfilter::plugin
