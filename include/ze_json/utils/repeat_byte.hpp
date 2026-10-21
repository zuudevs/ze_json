#pragma once

#include <ze_json/traits/is_integral.hpp>

namespace zuu::utils {
namespace __impl {

template <typename S, auto R, auto V> struct repeat_byte {
    static constexpr S value =
        (static_cast<S>(V) << (R * 8)) | repeat_byte<S, R - 1, V>::value;
};

template <typename S, auto V> struct repeat_byte<S, 0, V> {
    static constexpr S value = static_cast<S>(V);
};

} // namespace __impl

template <typename T, auto V>
    requires(traits::is_integral_v<T> && sizeof(V) == 1)
struct repeat_byte {
    static constexpr T value = __impl::repeat_byte<T, sizeof(T) - 1, V>::value;
};

template <typename T, auto V>
constexpr T repeat_byte_v = repeat_byte<T, V>::value;

} // namespace zuu::utils