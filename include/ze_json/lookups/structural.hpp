#pragma once

#include <ze_json/types/array.hpp>

namespace zuu::lookups {
namespace __impl {

// 0 = skip (whitespace, angka, dll)
// 1 = struktur (harus dicatat ke tape)
// 2 = string quote awal (masuk fase SWAR)
[[nodiscard]] static consteval types::Array<types::uint8_t, 256>
build_structural_map() NOEXCEPT {
    types::Array<types::uint8_t, 256> map{};
    map['\"'] = 2;
    map[','] = map[':'] = map['{'] = map['}'] = map['['] = map[']'] = map['t'] =
        map['n'] = map['f'] = 1;
    return map;
}

} // namespace __impl

constexpr auto kStructural = __impl::build_structural_map();

} // namespace zuu::lookups