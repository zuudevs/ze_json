#pragma once

#include <ze_json/core/integral.hpp>

namespace zuu::utils {

[[nodiscard]] inline void*
align(
    core::size_t alignment, core::size_t size, void*& ptr,
    core::size_t& space) NOEXCEPT {
	auto pn                  = reinterpret_cast<core::uintptr_t>(ptr);
    core::uintptr_t aligned = (pn + alignment - 1) & ~(alignment - 1);
    core::size_t padding    = aligned - pn;

    if (space < padding + size) {
        return nullptr;
    }

    ptr = reinterpret_cast<void*>(aligned);
    space -= padding;
    return ptr;
}

} // namespace zuu::utils