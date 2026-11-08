#pragma once

#include <ze_json/core/lookups.hpp>

namespace zuu::utils {

constexpr bool utf8_validator(const char *src, core::size_t len) noexcept;

} // namespace zuu::utils