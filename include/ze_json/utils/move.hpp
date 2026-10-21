#pragma once

#include <ze_json/core/macros.hpp>
#include <ze_json/traits/remove_reference.hpp>

namespace zuu::utils {

template <typename T>
[[nodiscard]] constexpr traits::remove_reference_t<T>&&
move(T&& t) NOEXCEPT {
    return static_cast<traits::remove_reference_t<T>&&>(t);
}

} // namespace zuu::utils