#pragma once

#include <ze_json/core/integral.hpp>
#include <ze_json/utils/repeat_byte.hpp>

namespace zuu::utils {

[[nodiscard]] constexpr core::uint64_t
swar_has_zero_byte(core::uint64_t v) noexcept {
  return (v - utils::repeat_byte_v<core::uint64_t, 0x01_u8>)&~v &
         utils::repeat_byte_v<core::uint64_t, 0x80_u8>;
}

} // namespace zuu::utils