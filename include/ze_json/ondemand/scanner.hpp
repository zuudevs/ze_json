#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/core/result.hpp>

namespace zuu::ondemand {

[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
scanner8(const char *const RESTRICT src, core::size_t src_size,
         core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

#if __LITTLE_ENDIAN__ == 1

#if __IS_64_BIT__ == 1

/**
 * @brief SWAR structural scanner (8-byte lanes).
 *
 * Measured-and-rejected variant (P4-2): loses to scanner8 on 5/6 corpora.
 * Kept tested for the thesis comparison matrix. Defined on little-endian
 * 64-bit targets only.
 */
[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
scanner64(const char *const RESTRICT src, core::size_t src_size,
          core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

#elif __IS_64_BIT__ == 0

[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
scanner32(const char *const RESTRICT src, core::size_t src_size,
          core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

#endif // __IS_64_BIT

#endif // __LITTLE_ENDIAN__ == 1

/**
 * @brief 128-bit SIMD structural scanner (16-byte lanes).
 *
 * Measured-and-rejected on native (P4-2b): loses to scanner8 on 5/6
 * corpora; kept tested for the thesis comparison matrix and available on
 * wasm64 via core::simd_* (SSE2 movemask ↔ wasm i8x16.bitmask).
 */
[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
scanner128(const char *const RESTRICT src, core::size_t src_size,
           core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

/**
 * @brief Static-dispatch entry point for the structural scanner.
 *
 * Resolves the best available variant ONCE (function-local static
 * initialization — never per call): the table-driven scanner8, which WON
 * the P4-2 measurement against the SWAR/SIMD variants (quote-dense compact
 * JSON keeps vector blocks in their serial path). The dispatched function
 * must satisfy the same contract as reference_scan(); the differential
 * test gates every variant.
 */
[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
structural_scan(const char *const RESTRICT src, core::size_t src_size,
                core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

} // namespace zuu::ondemand