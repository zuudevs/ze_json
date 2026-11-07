#pragma once

#include <ze_json/core/macro.hpp>

namespace zuu::core {

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

} // namespace zuu::core

constexpr auto
operator""_u8(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::core::uint8_t>(v);
}

constexpr auto
operator""_u16(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::core::uint16_t>(v);
}

constexpr auto
operator""_u32(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::core::uint32_t>(v);
}

constexpr auto
operator""_u64(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::core::uint64_t>(v);
}

constexpr auto
operator""_i8(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::core::int8_t>(v);
}

constexpr auto
operator""_i16(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::core::int16_t>(v);
}

constexpr auto
operator""_i32(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::core::int32_t>(v);
}

constexpr auto
operator""_i64(unsigned long long v) NOEXCEPT {
    return static_cast<zuu::core::int64_t>(v);
}