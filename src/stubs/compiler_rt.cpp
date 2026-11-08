#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>

using namespace zuu::core;

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)

extern "C" unsigned __int128 __multi3(unsigned __int128 a,
                                      unsigned __int128 b) NOEXCEPT {
  const uint64_t a_lo = static_cast<uint64_t>(a);
  const uint64_t a_hi = static_cast<uint64_t>(a >> 64);
  const uint64_t b_lo = static_cast<uint64_t>(b);
  const uint64_t b_hi = static_cast<uint64_t>(b >> 64);

  unsigned __int128 lo = static_cast<unsigned __int128>(a_lo) * b_lo;
  unsigned __int128 cross = (static_cast<unsigned __int128>(a_hi) * b_lo) +
                            (static_cast<unsigned __int128>(a_lo) * b_hi);

  return lo + (cross << 64);
}

#endif // __wasm__
