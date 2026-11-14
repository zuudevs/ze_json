#include <ze_json/core/lookups.hpp>
#include <ze_json/ondemand/scanner.hpp>

namespace zuu::ondemand {

/**
 * @brief Table-driven 8-bit structural scanner (scalar fast path).
 *
 * Outside strings, per-byte character comparisons are replaced with one
 * codegen table load (core::kJsonCharClass bitmask: 1=whitespace,
 * 2=structural, 4=quote, 8=escape) plus two masked tests. Inside a
 * string, the interior loop is TABLE-FREE: two predictable compares
 * ('\\' then '"') per byte — the interior is the byte majority of every
 * payload, so dropping the per-byte table load is the O2-1 win (ND-13:
 * the structural scan is 82-92% of s2-open). `kJsonCharClass` sets the
 * escape bit for 0x5C ONLY and the quote bit for 0x22 ONLY, so the
 * direct compares are exactly the old masked tests (differential gate).
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
    if (cls & 0x04_u8) { // opening quote: scan the interior table-free
      ++i;
      for (;;) {
        if (i >= src_size) {
          return core::JsonErrc::InvalidJson; // string never closed
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
