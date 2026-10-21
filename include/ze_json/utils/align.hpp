#pragma once

#include <ze_json/types/integral.hpp>

namespace zuu::utils {

[[nodiscard]] void*
align(
    types::size_t alignment, types::size_t size, void*& ptr,
    types::size_t& space) NOEXCEPT;

} // namespace zuu::utils