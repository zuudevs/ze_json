#include <ze_json/utils/align.hpp>

namespace zuu::utils {

void*
align(
    types::size_t alignment, types::size_t size, void*& ptr,
    types::size_t& space) NOEXCEPT {
    auto pn                  = reinterpret_cast<types::uintptr_t>(ptr);
    types::uintptr_t aligned = (pn + alignment - 1) & ~(alignment - 1);
    types::size_t padding    = aligned - pn;

    if (space < padding + size) {
        return nullptr;
    }

    ptr = reinterpret_cast<void*>(aligned);
    space -= padding;
    return ptr;
}

} // namespace zuu::utils