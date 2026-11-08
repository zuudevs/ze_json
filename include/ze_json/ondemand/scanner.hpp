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

} // namespace zuu::ondemand