#include <ze_json/core/lookups.hpp>
#include <ze_json/ondemand/scanner.hpp>

namespace zuu::ondemand {

/**
 * @brief Table-driven 8-bit structural scanner (scalar fast path).
 *
 * Replaces per-byte character comparisons with one codegen table load
 * (core::kJsonCharClass bitmask: 1=whitespace, 2=structural, 4=quote,
 * 8=escape) plus two masked tests. Byte-identical to reference_scan()
 * by contract; the differential test is the gate.
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
  bool in_string = false;

  for (core::size_t i = 0; i < src_size; ++i) {
    const core::uint8_t cls =
        core::kJsonCharClass[static_cast<core::uint8_t>(src[i])];

    if (in_string) {
      if (cls & 0x08_u8) { // escape byte: the next byte is literal
        if (++i >= src_size) {
          return core::JsonErrc::InvalidJson; // dangling escape
        }
      } else if (cls & 0x04_u8) { // closing quote
        in_string = false;
      }
      continue;
    }

    if (cls & 0x04_u8) { // opening quote
      in_string = true;
    } else if (cls & 0x02_u8) { // structural character
      if (out == dst_size) {
        return core::JsonErrc::InsufficientBuffer;
      }
      dst[out++] = i;
    }
  }

  if (in_string) {
    return core::JsonErrc::InvalidJson; // string never closed
  }
  return out;
}

} // namespace zuu::ondemand
