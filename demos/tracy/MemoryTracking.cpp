// Linked only into demo_memory: one set of global allocation replacements per EXE.
#include <tracy/Tracy.hpp>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace {
void* Allocate(std::size_t size, std::size_t alignment = 0) {
    if (size == 0) size = 1;
    for (;;) {
        void* p = alignment ? _aligned_malloc(size, alignment) : std::malloc(size);
        if (p) {
            TracyAllocS(p, size, 10);
            return p;
        }
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}
void Free(void* p, bool aligned = false) noexcept {
    if (!p) return;
    TracyFree(p);
    if (aligned) _aligned_free(p);
    else std::free(p);
}
}

void* operator new(std::size_t n) { return Allocate(n); }
void* operator new[](std::size_t n) { return Allocate(n); }
void operator delete(void* p) noexcept { Free(p); }
void operator delete[](void* p) noexcept { Free(p); }
void operator delete(void* p, std::size_t) noexcept { Free(p); }
void operator delete[](void* p, std::size_t) noexcept { Free(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return Allocate(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return Allocate(n); } catch (...) { return nullptr; }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { Free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { Free(p); }

void* operator new(std::size_t n, std::align_val_t a) { return Allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return Allocate(n, static_cast<std::size_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { Free(p, true); }
void operator delete[](void* p, std::align_val_t) noexcept { Free(p, true); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { Free(p, true); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { Free(p, true); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return Allocate(n, static_cast<std::size_t>(a)); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return Allocate(n, static_cast<std::size_t>(a)); } catch (...) { return nullptr; }
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { Free(p, true); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { Free(p, true); }
