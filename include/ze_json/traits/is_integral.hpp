#pragma once

#include <ze_json/traits/value_constant.hpp>

namespace zuu::traits {

template <typename T> struct is_integral : value_constant<false> {};

template <> struct is_integral<char> : value_constant<true> {};

template <> struct is_integral<unsigned char> : value_constant<true> {};

template <> struct is_integral<unsigned short> : value_constant<true> {};

template <> struct is_integral<unsigned int> : value_constant<true> {};

template <> struct is_integral<unsigned long> : value_constant<true> {};

template <> struct is_integral<unsigned long long> : value_constant<true> {};

template <> struct is_integral<signed char> : value_constant<true> {};

template <> struct is_integral<signed short> : value_constant<true> {};

template <> struct is_integral<signed int> : value_constant<true> {};

template <> struct is_integral<signed long> : value_constant<true> {};

template <> struct is_integral<signed long long> : value_constant<true> {};

template <typename T> constexpr bool is_integral_v = is_integral<T>::value;

} // namespace zuu::traits