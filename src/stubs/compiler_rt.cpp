#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>

using namespace zuu::core;

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)

//! 64x64 -> 128 product via 32-bit limbs ONLY: i128 arithmetic here must
//! not contain a multiply, or clang lowers it right back to __multi3 and
//! the stub recurses (observed: "Maximum call stack size exceeded" during
//! the number slow path). i128 add/shift lower inline on wasm64.
[[nodiscard]] static unsigned __int128
mul64x64(uint64_t x, uint64_t y) NOEXCEPT {
  const uint32_t x0 = static_cast<uint32_t>(x);
  const uint32_t x1 = static_cast<uint32_t>(x >> 32);
  const uint32_t y0 = static_cast<uint32_t>(y);
  const uint32_t y1 = static_cast<uint32_t>(y >> 32);
  const uint64_t p00 = static_cast<uint64_t>(x0) * y0;
  const uint64_t p01 = static_cast<uint64_t>(x0) * y1;
  const uint64_t p10 = static_cast<uint64_t>(x1) * y0;
  const uint64_t p11 = static_cast<uint64_t>(x1) * y1;
  unsigned __int128 product = static_cast<unsigned __int128>(p00);
  product += static_cast<unsigned __int128>(p01) << 32;
  product += static_cast<unsigned __int128>(p10) << 32;
  product += static_cast<unsigned __int128>(p11) << 64;
  return product;
}

//! a*b = a_lo*b_lo + (a_lo*b_hi + a_hi*b_lo) << 64; the a_hi*b_hi term is
//! congruent 0 (mod 2^128) and drops out of the returned type.
extern "C" unsigned __int128 __multi3(unsigned __int128 a,
                                      unsigned __int128 b) NOEXCEPT {
  const uint64_t a_lo = static_cast<uint64_t>(a);
  const uint64_t a_hi = static_cast<uint64_t>(a >> 64);
  const uint64_t b_lo = static_cast<uint64_t>(b);
  const uint64_t b_hi = static_cast<uint64_t>(b >> 64);

  const unsigned __int128 cross = mul64x64(a_lo, b_hi) + mul64x64(a_hi, b_lo);
  return mul64x64(a_lo, b_lo) + (cross << 64);
}

//! compiler-rt shift helpers required by the on-demand number path (P7-2
//! operates on the 128-bit digit accumulator): clang lowers variable-count
//! i128 shifts to these calls. Only CONSTANT counts stay inline, so the
//! implementations below shift by constants over 64-bit halves (never
//! re-entering these helpers) and pin the out-of-range behavior used by
//! wasm LLVM lowering (count >= width yields 0).
extern "C" unsigned __int128 __ashlti3(unsigned __int128 a,
                                       unsigned int count) NOEXCEPT {
  if (count >= 128) {
    return 0;
  }
  if (count >= 64) {
    const uint64_t lo = static_cast<uint64_t>(a);
    const uint64_t result_hi = lo << (count - 64);
    return static_cast<unsigned __int128>(result_hi) << 64; // low half = 0
  }
  if (count == 0) {
    return a;
  }
  const uint64_t lo = static_cast<uint64_t>(a);
  const uint64_t hi = static_cast<uint64_t>(a >> 64);
  const uint64_t result_lo = lo << count;
  const uint64_t result_hi = (hi << count) | (lo >> (64 - count));
  return static_cast<unsigned __int128>(result_hi) << 64 | result_lo;
}

extern "C" unsigned __int128 __lshrti3(unsigned __int128 a,
                                       unsigned int count) NOEXCEPT {
  if (count >= 128) {
    return 0;
  }
  if (count >= 64) {
    const uint64_t hi = static_cast<uint64_t>(a >> 64);
    return hi >> (count - 64); // result_hi = 0: count - 64 <= 63
  }
  if (count == 0) {
    return a;
  }
  const uint64_t lo = static_cast<uint64_t>(a);
  const uint64_t hi = static_cast<uint64_t>(a >> 64);
  const uint64_t result_lo = (lo >> count) | (hi << (64 - count));
  const uint64_t result_hi = hi >> count;
  return static_cast<unsigned __int128>(result_hi) << 64 | result_lo;
}

#endif // __wasm__
