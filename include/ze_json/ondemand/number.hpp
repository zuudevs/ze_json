#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/core/result.hpp>

namespace zuu::ondemand {

/**
 * @brief Structural decomposition of a JSON number token (RFC 8259 grammar:
 *        `-? (0 | [1-9][0-9]*) (\.[0-9]+)? ([eE][+-]?[0-9]+)?`).
 *
 * Digit bytes are `int_len + frac_len` in count; the '.' (when present)
 * sits at `digits_start + int_len` and is NOT part of the digit sequence —
 * the logical digit string concatenates the integer-part digits and the
 * fractional-part digits. `zero` is set when every digit is '0' (value is
 * ±0 — the sign is still meaningful: V8 parses "-0" as negative zero).
 * `exp_value` is clamped at 1,000,000: any exponent beyond ±400 decides
 * Infinity/0 outright, so the clamp never changes a finite result.
 */
struct NumberParts {
  bool negative;
  bool zero;
  core::size_t digits_start;
  core::size_t digits_len; // int_len + frac_len (dot excluded)
  core::size_t int_len;    // integer-part digit count (>= 1)
  core::size_t frac_len;   // fractional-part digit count (0 when absent)
  bool has_exp;
  bool exp_negative;
  core::uint64_t exp_value; // magnitude, clamped
  core::size_t total_len;   // full token length (sign through exp digits)
};

/**
 * @brief Validate the JSON number grammar and collect the decomposition.
 *
 * Consumes a PREFIX of the buffer (the on-demand parser calls this pointing
 * into a document); trailing bytes are ignored. Grammar violations return
 * InvalidJson — the number layer owns the full RFC 8259 number grammar
 * (leading zeros, bare '-', "1.", ".5", "1e", "+1" are all invalid).
 *
 * @cite 10.1002_spe.2984 number parsing at a gigabyte per second — the
 *       decomposition (mantissa digits + decimal exponent) this produces is
 *       the input format of the paper's fast/slow parsing paths.
 */
[[nodiscard]] core::Result<NumberParts, core::JsonErrc>
scan_number(const char *src, core::size_t len) NOEXCEPT;

/**
 * @brief Parse a JSON number token into a double with V8-parity semantics.
 *
 * Semantics (locked with the owner, P7 planning): the comparison baseline
 * is V8 `JSON.parse` — correctly-rounded IEEE 754 for in-range values,
 * ±Infinity on overflow (1e400), ±0 on underflow (1e-400), "-0" preserved.
 *
 * Paths (one rounding everywhere — no double rounding):
 *   - ±0 fast path for all-zero digits;
 *   - pure integers <= 19 digits: single uint64 -> double conversion
 *     (correctly rounded by IEEE 754 conversion rules);
 *   - mantissa < 2^53 (<= 15 digits) with |adjusted exponent| <= 22:
 *     exact 128-bit arithmetic (product or quotient+remainder), rounded
 *     once through the 55-bit + sticky machine;
 *   - everything else: fixed-capacity big-integer slow path (exact decimal
 *     -> binary long division, 55 significant bits + sticky) — same
 *     formulation as the paper's slow path, self-contained so the wasm64
 *     build (no libc) stays correct.
 */
[[nodiscard]] core::Result<double, core::JsonErrc>
parse_number(const char *src, core::size_t len) NOEXCEPT;

namespace detail {

/**
 * @brief Test/verification bridge for the P4-3 C2 negative-exponent paths
 *        (NOT part of the public API surface).
 *
 * Exposes the two q < 0 implementations of the exact-region number parser so
 * a differential test can pin them bit-for-bit against each other:
 *   - region_exact_neg: the reciprocal fast path (kPowerOfFiveReciprocal,
 *     paper Section 9.1 single-multiply scheme);
 *   - region_div_ref:   the proven P7-2 bit-serial long division (correctness
 *     oracle, reference-next-to-every-fast-path rule).
 * Requires 1 <= m < 2^53 and 1 <= k <= 22 — exactly region_exact's negative
 * branch contract (outside it the big-integer slow path takes over and
 * neither internal path is consulted).
 *
 * @cite 10.1002_spe.2984 fast/slow-path pairing discipline.
 */
[[nodiscard]] double region_exact_neg(core::uint64_t m, int k,
                                      bool negative) noexcept;
[[nodiscard]] double region_div_ref(core::uint64_t m, int k,
                                    bool negative) noexcept;

} // namespace detail

} // namespace zuu::ondemand
