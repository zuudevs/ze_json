#pragma once

#include <ze_json/core/macro.hpp>

#ifdef __wasm__
#include <wasm_simd128.h>
#else
#include <immintrin.h>
#endif

namespace zuu::core {

#ifdef __wasm__
using vec128 = v128_t;
#define SIMD_SET1_EPI8(val) wasm_i8x16_splat(val)
#else
using vec128 = __m128i;
#define SIMD_SET1_EPI8(val) _mm_set1_epi8(val)
#endif // __wasm__

} // namespace zuu::core