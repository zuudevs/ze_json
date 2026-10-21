#include <ze_json/utils/alloc.hpp>

void*
operator new(zuu::types::size_t, void* p) NOEXCEPT {
    return p;
}

void
operator delete(void*, void*) NOEXCEPT {}