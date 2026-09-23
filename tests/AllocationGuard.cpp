#include <cstdlib>
#include <new>

// C++ allocation guard around the actual shared-library process()/flush() calls.
thread_local bool realtime = false;
void *operator new(std::size_t n) {
    if (realtime)
        std::abort();
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void *p) noexcept {
    if (realtime)
        std::abort();
    std::free(p);
}
void operator delete[](void *p) noexcept {
    ::operator delete(p);
}
void operator delete(void *p, std::size_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    ::operator delete(p);
}
void *operator new(std::size_t n, std::align_val_t alignment) {
    if (realtime)
        std::abort();
    const auto a = static_cast<std::size_t>(alignment);
    const auto size = ((n ? n : 1) + a - 1) / a * a;
    if (void *p = std::aligned_alloc(a, size))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void operator delete(void *p, std::align_val_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void *p, std::align_val_t) noexcept {
    ::operator delete(p);
}
void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept {
    ::operator delete(p);
}
