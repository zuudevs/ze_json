#pragma once

namespace zuu::traits {

template <auto V> struct value_constant {
    static constexpr auto value = V;
};

} // namespace zuu::traits