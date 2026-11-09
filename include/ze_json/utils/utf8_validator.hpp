#pragma once

#include <ze_json/core/integral.hpp>
#include <ze_json/core/lookups.hpp>

namespace zuu::utils {

/**
 * @brief Scalar reference UTF-8 validator (table-free) — the correctness
 *        anchor every fast path is differential-tested against.
 *
 * Strict RFC 3629 semantics, matching the oracle used for the table
 * verification (P7-1): rejects leading/trailing continuations, C0/C1
 * overlong leads, E0/E0-style overlongs, ED surrogates (U+D800-DFFF),
 * F4 90+ beyond U+10FFFF, F5-FF leads, and truncated sequences.
 *
 * @cite 10.1002_spe.2920 the validation semantics this anchor mirrors
 *       (Unicode range rules, Section 2 of the paper).
 */
[[nodiscard]] inline bool utf8_validate_scalar(const char *src,
                                               core::size_t len) noexcept {
  const auto *p = reinterpret_cast<const core::uint8_t *>(src);
  core::size_t i = 0;
  while (i < len) {
    const core::uint8_t b0 = p[i];
    if (b0 < 0x80) {
      i += 1; // ASCII
      continue;
    }
    core::size_t need;
    core::uint8_t lo = 0x80;
    core::uint8_t hi = 0xBF;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
      need = 1; // C0/C1 (overlong) excluded
    } else if (b0 == 0xE0) {
      need = 2;
      lo = 0xA0; // E0 80-9F would be overlong
    } else if ((b0 >= 0xE1 && b0 <= 0xEC) || b0 == 0xEE || b0 == 0xEF) {
      need = 2;
    } else if (b0 == 0xED) {
      need = 2;
      hi = 0x9F; // ED A0-BF = surrogates U+D800-DFFF
    } else if (b0 == 0xF0) {
      need = 3;
      lo = 0x90; // F0 80-8F would be overlong
    } else if (b0 >= 0xF1 && b0 <= 0xF3) {
      need = 3;
    } else if (b0 == 0xF4) {
      need = 3;
      hi = 0x8F; // F4 90+ exceeds U+10FFFF
    } else {
      return false; // C0, C1, F5-FF, or stray continuation 80-BF
    }
    if (len - i < need + 1) {
      return false; // truncated sequence
    }
    const core::uint8_t b1 = p[i + 1];
    if (b1 < lo || b1 > hi) {
      return false;
    }
    for (core::size_t k = 2; k <= need; ++k) {
      const core::uint8_t bk = p[i + k];
      if (bk < 0x80 || bk > 0xBF) {
        return false;
      }
    }
    i += need + 1;
  }
  return true;
}

/**
 * @brief Production UTF-8 validator — Niagara two-table method, scalar
 *        formulation (three 16-entry lookups per byte, branch-free error
 *        detection; the SIMD pshufb formulation of the same tables is the
 *        P4-3 follow-up).
 *
 * Pipeline (paper Section 6.1-6.3):
 *   1. pair classification: v = T1H[prev>>4] & T1L[prev&0xF] & T2H[cur>>4];
 *      bits 0-6 set => the pair matches one of the seven two-byte error
 *      patterns (overlongs, surrogate, too-short, too-long, too-large);
 *   2. continuation-pair markers (bit 7) must appear exactly where a
 *      continuation pair is structurally expected (prev-prev byte >= 0xE0
 *      = 3-byte lead two back, or >= 0xF0 = 4-byte lead three back) — this
 *      is what catches wrong continuation COUNTS (missing/extra bytes in
 *      3-4 byte sequences);
 *   3. end-of-stream checks reject sequences truncated by the buffer end.
 *
 * Verified exhaustively against a strict RFC 3629 oracle during P7-1
 * (see GenerateLookups.py provenance note): 1-byte and 2-byte spaces in
 * full, 3-byte space for leads >= C0 in full, structured 4-byte sweep —
 * zero mismatches — and gated permanently by tests/utf8_test.cpp.
 *
 * @cite 10.1002_spe.2920 Niagara: validating UTF-8 with less than one
 *       instruction per byte — the two-table classification this
 *       implements and the marker/end checks it composes.
 */
[[nodiscard]] inline bool utf8_validate(const char *src,
                                        core::size_t len) noexcept {
  const auto *p = reinterpret_cast<const core::uint8_t *>(src);
  for (core::size_t i = 0; i < len; ++i) {
    const core::uint8_t prev = (i > 0) ? p[i - 1] : core::uint8_t{0};
    const core::uint8_t v = core::kUtf8Byte1High[prev >> 4] &
                            core::kUtf8Byte1Low[prev & 0x0F] &
                            core::kUtf8Byte2High[p[i] >> 4];
    if ((v & 0x7F) != 0) {
      return false; // two-byte error pattern (Table 8, bits 0-6)
    }
    const unsigned marker = static_cast<unsigned>(v >> 7);
    const unsigned expected =
        ((i >= 2 && p[i - 2] >= 0xE0) || (i >= 3 && p[i - 3] >= 0xF0)) ? 1u
                                                                       : 0u;
    if (marker != expected) {
      return false; // continuation-pair count/position mismatch (Section 6.2)
    }
  }
  if (len >= 1 && p[len - 1] >= 0xC0) {
    return false; // truncated 2/3/4-byte sequence (Section 6.3)
  }
  if (len >= 2 && p[len - 2] >= 0xE0) {
    return false;
  }
  if (len >= 3 && p[len - 3] >= 0xF0) {
    return false;
  }
  return true;
}

} // namespace zuu::utils
