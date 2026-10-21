#pragma once

#include <ze_json/ondemand/json_type.hpp>
#include <ze_json/types/array.hpp>

namespace zuu::lookups {

constexpr types::uint8_t TYPE_MASK   = 0b0000'0111; // 7
constexpr types::uint8_t SCAN_NORMAL = 0b0000'1000; // 8
constexpr types::uint8_t SCAN_QUOTE  = 0b0001'0000; // 16
constexpr types::uint8_t PAIR_OPEN   = 0b0010'0000; // 32
constexpr types::uint8_t PAIR_CLOSE  = 0b0100'0000; // 64

namespace __impl {

// 0 = skip (whitespace, angka, dll)
// 1 = array
// 2 = object
// 4 = string
// 5 = bool
// 6 = null
// 7 = other
[[nodiscard]] static consteval types::Array<types::uint8_t, 256>
build_structural_map() NOEXCEPT {
    types::Array<types::uint8_t, 256> map{};
    auto cast = [](zuu::ondemand::JsonType t) {
        return static_cast<types::uint8_t>(t);
    };
    map['['] = cast(zuu::ondemand::JsonType::Array) | SCAN_NORMAL | PAIR_OPEN;
    map[']'] = cast(zuu::ondemand::JsonType::Array) | SCAN_NORMAL | PAIR_CLOSE;
    map['{'] = cast(zuu::ondemand::JsonType::Object) | SCAN_NORMAL | PAIR_OPEN;
    map['}'] = cast(zuu::ondemand::JsonType::Object) | SCAN_NORMAL | PAIR_CLOSE;
    map['\"'] = cast(zuu::ondemand::JsonType::String) | SCAN_QUOTE;
    map['t'] = map['f'] = cast(zuu::ondemand::JsonType::Boolean) | SCAN_NORMAL;
    map['n']            = cast(zuu::ondemand::JsonType::Null) | SCAN_NORMAL;
    map[','] = map[':'] = cast(zuu::ondemand::JsonType::Other) | SCAN_NORMAL;

    return map;
}

} // namespace __impl

constexpr auto kStructural = __impl::build_structural_map();

} // namespace zuu::lookups