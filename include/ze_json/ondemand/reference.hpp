#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/core/result.hpp>

namespace zuu::ondemand {

/**
 * @brief Plain byte-by-byte structural scanner — the reference implementation.
 *
 * The scanner contract, expressed in its simplest executable form. Every
 * optimized scanner (scanner8/32/64/128) MUST reproduce this function
 * index-for-index and error-for-error on any input; the differential test
 * gates that equivalence (cpp-highperf-json rule 4, AGENTS.md section 6).
 *
 * Contract:
 * - dst receives the byte offsets of the JSON structural characters
 *   [ ] { } , : that occur OUTSIDE strings (a "structural index" stream).
 * - A double quote toggles string state; a backslash inside a string makes
 *   the next byte literal (escaped quotes do not close the string).
 * - src_size == 0                                   -> EmptyJson
 * - src == nullptr (with src_size > 0)              -> InvalidJson
 * - dst == nullptr || dst_size == 0                 -> InsufficientBuffer
 * - dst capacity exhausted while writing            -> InsufficientBuffer
 *     (dst then holds exactly dst_size indices)
 * - dangling backslash at the end of the input      -> InvalidJson
 * - string still open at the end of the input       -> InvalidJson
 * - success: returns the number of indices written.
 * The scanner classifies raw bytes only; UTF-8 validation and JSON grammar
 * checking belong to later on-demand stages.
 *
 * @cite 10.14778_3115404.3115416 Mison pre-computes structural indexes in a
 *       pre-scan so later on-demand parsing never re-scans raw bytes; this
 *       reference embodies that contract without any lookup-table shortcuts,
 *       so a codegen bug cannot corrupt the reference itself.
 * @cite 10.48550_arXiv.1902.08318 simdjson staging produces exactly this
 *       "next structural index" stream; the optimized scanners must agree
 *       with it byte-for-byte before any speed claim is admissible.
 */
[[nodiscard]] inline core::Result<core::size_t, core::JsonErrc>
reference_scan(const char *const RESTRICT src, core::size_t src_size,
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
    const char c = src[i];

    if (in_string) {
      if (c == '\\') {
        if (++i >= src_size) {
          return core::JsonErrc::InvalidJson; // dangling escape
        }
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }

    switch (c) {
    case '"':
      in_string = true;
      break;
    case '[':
    case ']':
    case '{':
    case '}':
    case ',':
    case ':':
      if (out == dst_size) {
        return core::JsonErrc::InsufficientBuffer;
      }
      dst[out++] = i;
      break;
    default:
      break;
    }
  }

  if (in_string) {
    return core::JsonErrc::InvalidJson; // string never closed
  }
  return out;
}

} // namespace zuu::ondemand
