#pragma once

#include <ze_json/meta/remove_reference.hpp>

namespace zuu::utils {

template <typename T>
[[nodiscard]] constexpr meta::remove_reference_t<T> &&move(T &&t) noexcept {
  return static_cast<meta::remove_reference_t<T> &&>(t);
}

} // namespace zuu::utils