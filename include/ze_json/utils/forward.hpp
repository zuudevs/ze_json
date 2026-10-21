#pragma once

#include <ze_json/core/macros.hpp>
#include <ze_json/traits/remove_reference.hpp>

namespace zuu::utils {

template <typename T>
constexpr T&&
forward(traits::remove_reference_t<T>& arg) NOEXCEPT {
    return static_cast<T&&>(arg);
}

template <typename T>
constexpr T&&
forward(traits::remove_reference_t<T>&& arg) NOEXCEPT {
    return static_cast<T&&>(arg);
}

} // namespace zuu::utils