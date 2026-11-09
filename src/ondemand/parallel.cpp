#include <ze_json/core/lookups.hpp>
#include <ze_json/core/simd.hpp>
#include <ze_json/ondemand/scanner.hpp>
#include <ze_json/utils/swar.hpp>

namespace zuu::ondemand {

#if __LITTLE_ENDIAN__ == 1

#if __IS_64_BIT__ == 1

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
    } else if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',' ||
               c == ':') {
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

    const core::uint64_t quotes = utils::swar_eq(lanes, 0x22);
    const core::uint64_t slashes = utils::swar_eq(lanes, 0x5C);
    const bool interesting = (quotes | slashes) != 0;

    if (!in_string && !interesting) {
      // FAST-A: outside a string; every flagged lane is a real structural byte.
      core::uint64_t structural =
          utils::swar_eq(lanes, 0x2C) | utils::swar_eq(lanes, 0x3A) |
          utils::swar_eq(lanes, 0x5B) | utils::swar_eq(lanes, 0x5D) |
          utils::swar_eq(lanes, 0x7B) | utils::swar_eq(lanes, 0x7D);
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
      if (!walk_byte()) {
        return core::JsonErrc::InsufficientBuffer;
      }
    }
  }

  while (i < src_size) {
    if (!walk_byte()) {
      return core::JsonErrc::InsufficientBuffer;
    }
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
  (void)src;
  (void)src_size;
  (void)dst;
  (void)dst_size;
  return core::JsonErrc::InvalidJson;
}
#endif // __IS_64_BIT__ == 0
#endif // __LITTLE_ENDIAN__ == 1

#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1 &&                            \
    (defined(__SSE2__) || defined(__wasm__))

/**
 * @brief 128-bit SIMD structural scanner (16-byte blocks).
 *
 * Same three-path structure as scanner64 (proven by the differential gate),
 * with per-lane byte classification (one compare per class, 0.75 ops/byte
 * vs ~3.6 for SWAR) and bitmask lane extraction (SSE2 movemask /
 * wasm-simd128 i8x16.bitmask). Built on core::simd_* so the identical
 * source serves both ISAs:
 *   FAST-A  outside a string, no quote/backslash in the block
 *   FAST-B  inside a string, no quote/backslash in the block
 *   SLOW    byte-serial walk of the block (exact reference semantics)
 *
 * @cite 10.48550_arXiv.1902.08318 simdjson: 128-bit block classification with
 *       per-class vector compares and a carried string-state; the escape
 *       chain stays serial (see scanner64 note) for differential-verifiable
 *       correctness.
 */
core::Result<core::size_t, core::JsonErrc>
scanner128(const char *const RESTRICT src, core::size_t src_size,
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

  const core::vec128 quote_v = core::simd_splat_u8('"');
  const core::vec128 slash_v = core::simd_splat_u8('\\');
  const core::vec128 comma_v = core::simd_splat_u8(',');
  const core::vec128 colon_v = core::simd_splat_u8(':');
  const core::vec128 lbrack_v = core::simd_splat_u8('[');
  const core::vec128 rbrack_v = core::simd_splat_u8(']');
  const core::vec128 lbrace_v = core::simd_splat_u8('{');
  const core::vec128 rbrace_v = core::simd_splat_u8('}');

  core::size_t out = 0;
  core::size_t i = 0;
  bool in_string = false;
  bool escaped_carry = false;

  auto record = [&](core::size_t index) -> bool {
    if (out == dst_cap) {
      return false;
    }
    dst[out++] = index;
    return true;
  };

  auto walk_byte = [&]() {
    const char c = src[i];
    if (in_string) {
      if (escaped_carry) {
        escaped_carry = false;
      } else if (c == '\\') {
        escaped_carry = true;
      } else if (c == '"') {
        in_string = false;
      }
    } else if (c == '"') {
      in_string = true;
    } else if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',' ||
               c == ':') {
      if (!record(i)) {
        return false;
      }
    }
    i += 1;
    return true;
  };

  const core::size_t n_blocks = src_size / 16;
  for (core::size_t block = 0; block < n_blocks; ++block) {
    const core::vec128 lanes = core::simd_loadu(src + i);
    const core::uint32_t interesting = core::simd_bitmask_u8(core::simd_or(
        core::simd_eq_u8(lanes, quote_v), core::simd_eq_u8(lanes, slash_v)));

    if (interesting == 0) {
      if (!in_string) {
        // FAST-A
        const core::vec128 structural_v = core::simd_or(
            core::simd_or(core::simd_or(core::simd_eq_u8(lanes, comma_v),
                                        core::simd_eq_u8(lanes, colon_v)),
                          core::simd_or(core::simd_eq_u8(lanes, lbrack_v),
                                        core::simd_eq_u8(lanes, rbrack_v))),
            core::simd_or(core::simd_eq_u8(lanes, lbrace_v),
                          core::simd_eq_u8(lanes, rbrace_v)));
        core::uint32_t mask = core::simd_bitmask_u8(structural_v);
        while (mask != 0) {
          const unsigned bit = static_cast<unsigned>(__builtin_ctz(mask));
          mask &= mask - 1;
          if (!record(i + bit)) {
            return core::JsonErrc::InsufficientBuffer;
          }
        }
      }
      // FAST-A (recorded) or FAST-B (skipped); escaped carry clears either way.
      i += 16;
      escaped_carry = false;
      continue;
    }

    // SLOW: quote/backslash present — walk the block byte-serially.
    for (unsigned lane = 0; lane < 16; ++lane) {
      if (!walk_byte()) {
        return core::JsonErrc::InsufficientBuffer;
      }
    }
  }

  while (i < src_size) {
    if (!walk_byte()) {
      return core::JsonErrc::InsufficientBuffer;
    }
  }

  if (in_string) {
    return core::JsonErrc::InvalidJson; // string never closed
  }
  return out;
}

#endif // little-endian 64-bit + (SSE2 || wasm-simd128)

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
