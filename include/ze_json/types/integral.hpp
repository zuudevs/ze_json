#pragma once

#include <ze_json/core/macros.hpp>

namespace zuu::types {

using uint8_t  = __UINT8_TYPE__;
using uint16_t = __UINT16_TYPE__;
using uint32_t = __UINT32_TYPE__;
using uint64_t = __UINT64_TYPE__;

using int8_t  = __INT8_TYPE__;
using int16_t = __INT16_TYPE__;
using int32_t = __INT32_TYPE__;
using int64_t = __INT64_TYPE__;

using size_t    = __SIZE_TYPE__;
using uintptr_t = __UINTPTR_TYPE__;

} // namespace zuu::types

constexpr auto
operator""_u8(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::types::uint8_t>(v);
}

constexpr auto
operator""_u16(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::types::uint16_t>(v);
}

constexpr auto
operator""_u32(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::types::uint32_t>(v);
}

constexpr auto
operator""_u64(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::types::uint64_t>(v);
}

constexpr auto
operator""_i8(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::types::int8_t>(v);
}

constexpr auto
operator""_i16(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::types::int16_t>(v);
}

constexpr auto
operator""_i32(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::types::int32_t>(v);
}

constexpr auto
operator""_i64(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::types::int64_t>(v);
}