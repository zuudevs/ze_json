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
[[nodiscard]] constexpr types::uint64_t
swar_has_zero_byte(types::uint64_t v) NOEXCEPT {
    return (v - utils::repeat_byte_v<types::uint64_t, 0x01_u8>)&~v &
           utils::repeat_byte_v<types::uint64_t, 0x80_u8>;
}
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

EXTERN_C_BEGIN
EXPORT_NAME(scanner8)
void
scanner8(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size,
    zuu::types::size_t& result, zuu::types::int32_t& error) NOEXCEPT;

#if __LITTLE_ENDIAN__ == 1
#if __IS_64_BIT__ == 1
EXPORT_NAME(scanner64)
void
scanner64(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size,
    zuu::types::size_t& result, zuu::types::int32_t& error) NOEXCEPT;
#elif __IS_64_BIT__ == 0
#else
EXPORT_NAME(scanner32)
void
scanner32(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size,
    zuu::types::size_t& result, zuu::types::int32_t& error) NOEXCEPT;
#endif // __IS_64_BIT
#endif // __LITTLE_ENDIAN__ == 1
EXTERN_C_END