#pragma once

#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>

#ifdef __wasm__
#include <wasm_simd128.h>
#else
#include <immintrin.h>
#endif

namespace zuu::core {

#ifdef __wasm__
using vec128 = v128_t;
#else
using vec128 = __m128i;
#endif // __wasm__

/**
 * @brief Multi-ISA 128-bit byte ops shared by the on-demand scanners.
 *
 * Wasm maps onto the stable wasm-simd128 intrinsics; native maps onto
 * SSE2 (baseline on every x86-64 CPU, no runtime dispatch needed here).
 * Each abstraction maps 1:1 onto its intrinsic — no wrapper hides extra
 * work — so scanners stay measurable against the raw intrinsics build.
 */

[[nodiscard]] inline vec128 simd_splat_u8(core::uint8_t value) noexcept {
#ifdef __wasm__
  return wasm_i8x16_splat(static_cast<int8_t>(value));
#else
  return _mm_set1_epi8(static_cast<char>(value));
#endif
}

/** Unaligned 16-byte load (wasm loads are unaligned by construction). */
[[nodiscard]] inline vec128 simd_loadu(const void *addr) noexcept {
#ifdef __wasm__
  return wasm_v128_load(addr);
#else
  return _mm_loadu_si128(static_cast<const __m128i *>(addr));
#endif
}

[[nodiscard]] inline vec128 simd_or(vec128 a, vec128 b) noexcept {
#ifdef __wasm__
  return wasm_v128_or(a, b);
#else
  return _mm_or_si128(a, b);
#endif
}

/** Per-lane byte equality: lane is 0xFF where equal, 0x00 otherwise. */
[[nodiscard]] inline vec128 simd_eq_u8(vec128 a, vec128 b) noexcept {
#ifdef __wasm__
  return wasm_i8x16_eq(a, b);
#else
  return _mm_cmpeq_epi8(a, b);
#endif
}

/** Bit i set when byte-lane i of the predicate result was non-zero. */
[[nodiscard]] inline core::uint32_t simd_bitmask_u8(vec128 v) noexcept {
#ifdef __wasm__
  return static_cast<core::uint32_t>(wasm_i8x16_bitmask(v));
#else
  return static_cast<core::uint32_t>(_mm_movemask_epi8(v));
#endif
}

} // namespace zuu::core
