#pragma once

#include <ze_json/core/integral.hpp>
#include <ze_json/utils/repeat_byte.hpp>

namespace zuu::utils {

/**
 * SWAR per-byte zero detector: 0x80 in every lane whose byte is zero.
 *
 * The textbook bit-trick `(v - 0x01..) & ~v & 0x80..` is only valid as a
 * BOOLEAN ("some byte is zero"); as a per-byte MASK it produces false
 * positives when a borrow crosses a zero lane into a lane holding 0x01
 * (verified empirically: 30 mismatches / 200k random inputs — found by the
 * scanner differential gate, P4-2). The carry-free form below is exact:
 *   b == 0  <=>  bit7 of ~((b & 0x7F) + 0x7F | b)
 * (max intermediate value 0xFE + 0x7F per byte, so no cross-byte carries).
 *
 * @cite 10.1002_spe.2984 SWAR byte-parallel arithmetic idiom (number parsing
 *       at a gigabyte per second) — the technique family this routine
 *       implements; the carry-free mask form is required for per-byte masks.
 */
[[nodiscard]] constexpr core::uint64_t
swar_has_zero_byte(core::uint64_t v) noexcept {
  const core::uint64_t low = v & utils::repeat_byte_v<core::uint64_t, 0x7F_u8>;
  return ~(v | (low + utils::repeat_byte_v<core::uint64_t, 0x7F_u8>))
         & utils::repeat_byte_v<core::uint64_t, 0x80_u8>;
}

/**
 * SWAR byte-equality mask: 0x80 in every lane whose byte equals `c`.
 * Broadcasts `c` across the lanes with the repeat_byte_v API and XORs it
 * into `x` so equality reduces to zero detection (carry-free, per-byte
 * exact — see swar_has_zero_byte for why the textbook form is not used).
 *
 * @cite 10.1002_spe.2984 SWAR byte-parallel classification idiom — the
 *       broadcast-XOR-then-zero-test composition used for per-byte masks.
 */
[[nodiscard]] constexpr core::uint64_t
swar_eq(core::uint64_t x, core::uint8_t byte) noexcept {
  const core::uint64_t broadcast =
      static_cast<core::uint64_t>(byte)
      * utils::repeat_byte_v<core::uint64_t, 0x01_u8>;
  return swar_has_zero_byte(x ^ broadcast);
}

} // namespace zuu::utils
