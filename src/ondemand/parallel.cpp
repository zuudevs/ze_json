#include <ze_json/core/lookups.hpp>
#include <ze_json/ondemand/scanner.hpp>
#include <ze_json/utils/swar.hpp>
#include <ze_json/utils/utf8_validator.hpp>

namespace zuu::ondemand {

#if __LITTLE_ENDIAN__ == 1

#if __IS_64_BIT__ == 1

namespace {

constexpr core::uint64_t kOnes = 0x0101010101010101ULL;
constexpr core::uint64_t kHigh = 0x8080808080808080ULL;

/**
 * SWAR byte-equality: 0x80 in every lane whose byte equals `c`.
 * Delegates to utils::swar_has_zero_byte — the carry-free, per-byte-exact
 * form (the textbook (t - 0x01) & ~t & 0x80 idiom leaks borrows into the
 * lane above a zero lane and is NOT a valid per-byte mask; see swar.hpp).
 */
[[nodiscard]] constexpr core::uint64_t swar_eq(core::uint64_t x, core::uint8_t c) {
  return utils::swar_has_zero_byte(x ^ (static_cast<core::uint64_t>(c) * kOnes));
}

} // namespace

/**
 * @brief SWAR structural scanner (8-byte blocks, little-endian lanes).
 *
 * Three paths per block (carries: in-string state + escaped flag):
 *   FAST-A  outside a string, block has no quote/backslash
 *           -> classify + record all structural lanes, advance 8;
 *   FAST-B  inside a string, block has no quote/backslash
 *           -> whole block is string payload, advance 8;
 *   SLOW    otherwise -> byte-serial walk of the block (exact
 *           reference semantics; also used for the <8-byte tail).
 * Escape handling stays serial in the SLOW path: an escaped character can
 * never change scanner state, so skipping it byte-wise is sufficient and
 * trivially verifiable against reference_scan (differential gate).
 *
 * @cite 10.14778_3115404.3115416 Mison: structural indexes from a byte-class
 *       pre-scan; the block-parallel classification follows that design.
 * @cite 10.48550_arXiv.1902.08318 simdjson: block-parallel classification
 *       with a carried string-state across block boundaries.
 * @cite 10.1002_spe.2984 SWAR techniques (number parsing at GB/s) — the
 *       byte-parallel arithmetic idiom used for classification.
 */
core::Result<core::size_t, core::JsonErrc>
scanner64(const char *const RESTRICT src, core::size_t src_size,
          core::size_t *RESTRICT dst, core::size_t dst_cap) NOEXCEPT {
  if (src_size == 0) {
    return core::JsonErrc::EmptyJson;
  }
  if (src == nullptr) {
    return core::JsonErrc::InvalidJson;
  }
  if (dst == nullptr || dst_cap == 0) {
    return core::JsonErrc::InsufficientBuffer;
  }

  core::size_t out = 0;
  core::size_t i = 0;
  bool in_string = false;
  bool escaped_carry = false;

  auto record = [&](core::size_t index) -> bool {
    if (out == dst_cap) {
      return false; // InsufficientBuffer
    }
    dst[out++] = index;
    return true;
  };

  // Byte-serial walk (exact reference semantics) for SLOW blocks and the tail.
  auto walk_byte = [&]() {
    const char c = src[i];
    if (in_string) {
      if (escaped_carry) {
        escaped_carry = false; // escaped byte: literal, no state change
      } else if (c == '\\') {
        escaped_carry = true;
      } else if (c == '"') {
        in_string = false;
      }
    } else if (c == '"') {
      in_string = true;
    } else if (c == '[' || c == ']' || c == '{' || c == '}'
               || c == ',' || c == ':') {
      if (!record(i)) {
        return false;
      }
    }
    i += 1;
    return true;
  };

  const core::size_t n_blocks = src_size / 8;
  for (core::size_t block = 0; block < n_blocks; ++block) {
    core::uint64_t lanes;
    __builtin_memcpy(&lanes, src + i, 8);

    const core::uint64_t quotes = swar_eq(lanes, 0x22);
    const core::uint64_t slashes = swar_eq(lanes, 0x5C);
    const bool interesting = (quotes | slashes) != 0;

    if (!in_string && !interesting) {
      // FAST-A: outside a string; every flagged lane is a real structural byte.
      core::uint64_t structural =
          swar_eq(lanes, 0x2C) | swar_eq(lanes, 0x3A)
        | swar_eq(lanes, 0x5B) | swar_eq(lanes, 0x5D)
        | swar_eq(lanes, 0x7B) | swar_eq(lanes, 0x7D);
      while (structural != 0) {
        const unsigned bit = static_cast<unsigned>(__builtin_ctzll(structural));
        structural &= structural - 1;
        if (!record(i + (bit >> 3))) {
          return core::JsonErrc::InsufficientBuffer;
        }
      }
      i += 8;
      continue;
    }
    if (in_string && !interesting) {
      // FAST-B: whole block is string payload (the escaped carry, if any,
      // applies to this block's first byte — a literal, no state change).
      i += 8;
      escaped_carry = false;
      continue;
    }

    // SLOW: quote/backslash present — walk the block byte-serially.
    for (unsigned lane = 0; lane < 8; ++lane) {
      walk_byte();
    }
  }

  while (i < src_size) {
    walk_byte();
  }

  if (in_string) {
    return core::JsonErrc::InvalidJson; // string never closed
  }
  return out;
}

#elif __IS_64_BIT__ == 0

core::Result<core::size_t, core::JsonErrc>
scanner32(const char *const RESTRICT src, core::size_t src_size,
          core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT {
  // TODO: 32-bit SWAR variant (same three-path structure, 4 lanes).
  (void)src; (void)src_size; (void)dst; (void)dst_size;
  return core::JsonErrc::InvalidJson;
}
#endif // __IS_64_BIT__ == 0
#endif // __LITTLE_ENDIAN__ == 1

/**
 * @brief Static-dispatch implementation (resolve the variant pointer once).
 *
 * @cite 10.1145_2807591.2807644 measurement discipline: the dispatched variant
 *       is fixed for the whole run, never selected per call.
 */
core::Result<core::size_t, core::JsonErrc>
structural_scan(const char *const RESTRICT src, core::size_t src_size,
                core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT {
  using ScanFn = core::Result<core::size_t, core::JsonErrc> (*)(
      const char *const RESTRICT, core::size_t, core::size_t *RESTRICT,
      core::size_t) NOEXCEPT;
  static const ScanFn dispatch = []() -> ScanFn {
#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1
    // P4-2 MEASURED DECISION (run zuu-20261109053632-85afad7, dev_device,
    // native-release): the SWAR scanner64 variant LOSES to the table-driven
    // scanner8 on 5 of 6 corpora (compact JSON is quote-dense, so blocks fall
    // to the serial path and the 8-lane classification costs more than one
    // table load per byte). The dispatch therefore keeps scanner8 as the
    // default; scanner64 stays available as a tested variant for the thesis
    // comparison. If a future variant wins, change ONLY this line.
    //
    // Measured medians, same run, same corpus (MB/s):
    //   synthetic_1mb:  reference 760 | scanner8 934 | scanner64 760
    //   saq_payload_v1: reference 1180| scanner8 1410| scanner64 1240
    //   checker_batch:  reference 761 | scanner8 999 | scanner64 815
    return &scanner8;
#else
    return &scanner8;
#endif
  }();
  return dispatch(src, src_size, dst, dst_size);
}

} // namespace zuu::ondemand
