#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/macros.hpp>
#include <ze_json/lookups/structural.hpp>
#include <ze_json/types/result.hpp>
#include <ze_json/utils/repeat_byte.hpp>

namespace zuu::scanner {

[[nodiscard]] types::Result<types::size_t, core::JsonErrc>
scanner8(
    const char* const RESTRICT src, types::size_t src_size,
    types::size_t* RESTRICT dst, types::size_t dst_size) NOEXCEPT;

#if __LITTLE_ENDIAN__ == 1
#if __IS_64_BIT__ == 1
[[nodiscard]] types::Result<types::size_t, core::JsonErrc>
scanner64(
    const char* const RESTRICT src, types::size_t src_size,
    types::size_t* RESTRICT dst, types::size_t dst_size) NOEXCEPT;
#elif __IS_64_BIT__ == 0
#else
[[nodiscard]] types::Result<types::size_t, core::JsonErrc>
scanner32(
    const char* const RESTRICT src, types::size_t src_size,
    types::size_t* RESTRICT dst, types::size_t dst_size) NOEXCEPT;
#endif // __IS_64_BIT
#endif // __LITTLE_ENDIAN__ == 1

} // namespace zuu::scanner