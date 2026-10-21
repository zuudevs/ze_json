#pragma once

#include <ze_json/types/integral.hpp>
#include <ze_json/utils/repeat_byte.hpp>

namespace zuu::utils {

[[nodiscard]] constexpr types::uint64_t
swar_has_zero_byte(types::uint64_t v) NOEXCEPT {
    return (v - utils::repeat_byte_v<types::uint64_t, 0x01_u8>)&~v &
           utils::repeat_byte_v<types::uint64_t, 0x80_u8>;
}

} // namespace zuu::utils