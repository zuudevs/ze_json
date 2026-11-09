#pragma once

#include <ze_json/core/integral.hpp>

// The freestanding wasm64 build (-nostdlib) has no <new> and defines the
// placement-new pair itself; hosted toolchains (all native tests link the
// STL) get the library's declarations+definitions from <new>.
#if defined(__has_include) && __has_include(<new>)
#include <new>
#else

[[nodiscard]] inline void *operator new(zuu::core::size_t, void *p) noexcept {
  return p;
}

inline void operator delete(void *, void *) noexcept {}

#endif