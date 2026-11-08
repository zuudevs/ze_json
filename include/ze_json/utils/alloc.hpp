#pragma once

#include <ze_json/core/integral.hpp>

[[nodiscard]] inline void*
operator new(zuu::core::size_t, void* p) noexcept {
	return p;
}

inline void
operator delete(void*, void*) noexcept {}