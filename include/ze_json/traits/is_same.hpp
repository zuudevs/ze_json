#pragma once

#include <ze_json/traits/value_constant.hpp>

namespace zuu::traits {

template <typename T1, typename T2> struct is_same : value_constant<false> {};

template <typename T> struct is_same<T, T> : value_constant<true> {};

template <typename T1, typename T2>
constexpr bool is_same_v = is_same<T1, T2>::value;

} // namespace zuu::traits