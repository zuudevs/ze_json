#pragma once

namespace zuu::meta {

template <auto V> struct value_constant {
    static constexpr auto value = V;
};

} // namespace zuu::meta