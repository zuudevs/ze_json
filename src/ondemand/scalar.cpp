#include <ze_json/core/lookups.hpp>
#include <ze_json/ondemand/scanner.hpp>
#include <ze_json/utils/swar.hpp>

namespace zuu::ondemand {

/**
 * @brief Table-driven 8-bit structural scanner (scalar fast path).
 *
 * Outside strings, per-byte character comparisons are replaced with one
 * codegen table load (core::kJsonCharClass bitmask: 1=whitespace,
 * 2=structural, 4=quote, 8=escape) plus two masked tests. Inside a
 * string, the interior runs in 8-byte SWAR blocks: one unaligned load,
 * two broadcast-XOR byte-equality masks (`"` and `\`, exact per-byte —
 * the carry-free swar_eq), and an 8-byte skip when a block is clean.
 * The first matching lane resumes the scalar residue so an escape pair
 * straddling a block boundary is consumed exactly (any quote preceded by
 * a backslash has that backslash EARLIER in the block, so the first
 * match is always the backslash — a paired quote can never be mistaken
 * for a closer). The interior is the byte majority of every payload
 * (ND-14 histogram: saq bw_p50 = 24 B, 91% of interior bytes in runs
 * >= 8 B), so the block step replaces 8 compare-loops per clean block;
 * synthetic corpora (bw_p50 = 6 B) stay correct but are NOT expected to
 * win (P4-2 lesson: measure per corpus, accept only where it wins).
 *
 * Byte-identical to reference_scan() by contract; the differential test
 * is the gate.
 *
 * @cite 10.14778_3115404.3115416 Mison: structural indexes are produced in
 *       a cheap pre-scan pass (classifying bytes via a lookup table) so the
 *       on-demand parser never re-scans raw bytes.
 * @cite 10.48550_arXiv.1902.08318 simdjson: the staging stage emits exactly
 *       this next-structural-index stream; correctness of the fast path is
 *       defined as byte-identity with the scalar reference (reference.hpp).
 * @cite 10.1002_spe.2984 SWAR byte-parallel classification idiom (the
 *       broadcast-XOR + carry-free zero-mask composition of utils/swar.hpp).
 *
 * Contract: identical to reference_scan() (see reference.hpp) — error
 * values, error precedence, dst contents on InsufficientBuffer, and the
 * returned structural-index count.
 */
core::Result<core::size_t, core::JsonErrc>
scanner8(const char *const RESTRICT src, core::size_t src_size,
         core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT {
  if (src_size == 0) {
    return core::JsonErrc::EmptyJson;
  }
  if (src == nullptr) {
    return core::JsonErrc::InvalidJson;
  }
  if (dst == nullptr || dst_size == 0) {
    return core::JsonErrc::InsufficientBuffer;
  }

  core::size_t out = 0;
  core::size_t i = 0;
  while (i < src_size) {
    const core::uint8_t cls =
        core::kJsonCharClass[static_cast<core::uint8_t>(src[i])];
    if (cls & 0x04_u8) { // opening quote: scan the interior
      ++i;
      for (;;) {
        if (i >= src_size) {
          return core::JsonErrc::InvalidJson; // string never closed
        }
        if (i + 8 <= src_size) {
          core::uint64_t w;
          __builtin_memcpy(&w, src + i, sizeof(w)); // unaligned block load
          const core::uint64_t m =
              utils::swar_eq(w, 0x22_u8)    // '"'
              | utils::swar_eq(w, 0x5C_u8); // '\\'
          if (m == 0) {
            i += 8; // clean block: skip 8 interior bytes at once
            continue;
          }
          // resume at the FIRST matching lane; everything before it is
          // interior the block just skipped.
          i += static_cast<core::size_t>(__builtin_ctzll(m)) / 8;
        }
        const char b = src[i];
        if (b == '\\') { // escape byte: the next byte is literal
          if (++i >= src_size) {
            return core::JsonErrc::InvalidJson; // dangling escape
          }
          ++i;
          continue;
        }
        if (b == '"') { // closing quote
          ++i;
          break;
        }
        ++i;
      }
      continue;
    }
    if (cls & 0x02_u8) { // structural character
      if (out == dst_size) {
        return core::JsonErrc::InsufficientBuffer;
      }
      dst[out++] = i;
    }
    ++i;
  }
  return out;
}

} // namespace zuu::ondemand
