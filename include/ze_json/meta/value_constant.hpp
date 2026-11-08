#pragma once

namespace zuu::meta {

template <auto V> struct value_constant {
  static constexpr decltype(V) value = V;
};

} // namespace zuu::meta