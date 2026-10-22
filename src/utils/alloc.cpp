#include <ze_json/utils/alloc.hpp>

void*
operator new(zuu::types::size_t, void* p) noexcept {
    return p;
}

void
operator delete(void*, void*) noexcept {}