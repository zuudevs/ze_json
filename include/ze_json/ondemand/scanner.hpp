#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/core/result.hpp>
#include <ze_json/utils/repeat_byte.hpp>

namespace zuu::ondemand {

[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
scanner8(const char *const RESTRICT src, core::size_t src_size,
         core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

#if __LITTLE_ENDIAN__ == 1

#if __IS_64_BIT__ == 1

[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
scanner64(const char *const RESTRICT src, core::size_t src_size,
          core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

#elif __IS_64_BIT__ == 0

[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
scanner32(const char *const RESTRICT src, core::size_t src_size,
          core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

#endif // __IS_64_BIT

#endif // __LITTLE_ENDIAN__ == 1

[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
scanner128(const char *const RESTRICT src, core::size_t src_size,
           core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

/**
 * @brief Static-dispatch entry point for the structural scanner.
 *
 * Resolves the best available variant ONCE (function-local static
 * initialization — never per call): scanner64 (SWAR) on little-endian
 * 64-bit targets, falling back to the plain scalar scanner8 otherwise.
 * The dispatched function must satisfy the same contract as
 * reference_scan(); the differential test gates every variant.
 */
[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
structural_scan(const char *const RESTRICT src, core::size_t src_size,
                core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT;

} // namespace zuu::ondemand