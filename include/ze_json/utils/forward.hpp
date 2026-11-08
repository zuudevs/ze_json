#pragma once

#include <ze_json/core/macro.hpp>
#include <ze_json/meta/remove_reference.hpp>

namespace zuu::utils {

template <typename T>
constexpr T &&forward(meta::remove_reference_t<T> &arg) noexcept {
  return static_cast<T &&>(arg);
}

template <typename T>
constexpr T &&forward(meta::remove_reference_t<T> &&arg) noexcept {
  return static_cast<T &&>(arg);
}

} // namespace zuu::utils