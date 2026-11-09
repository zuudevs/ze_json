#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/core/result.hpp>

namespace zuu::ondemand {

/**
 * @brief Structural decomposition of a JSON string token (RFC 8259).
 *
 * `total_len` spans BOTH quotes; `inner_len` is the byte count between
 * them (the hard upper bound of the decoded output: every escape is
 * STRICTLY shorter than its escaped form — 2-byte simple escapes shrink
 * to 1 byte, 6-byte `\uXXXX` escapes shrink to <= 4 bytes — and raw
 * bytes copy 1:1, so decode output never exceeds `inner_len`).
 * `escape_count` counts backslash escapes (0 = plain copy).
 */
struct StringParts {
  core::size_t total_len;    // including both quotes
  core::size_t inner_len;    // bytes between the quotes (decode upper bound)
  core::size_t escape_count; // number of backslash escapes
};

/**
 * @brief Validate the JSON string grammar and collect the decomposition.
 *
 * `src` must point AT the opening quote (the on-demand parser calls this
 * positioned on a token start); a leading non-quote returns InvalidJson.
 * A following garbage byte is the CALLER's delimiter check — the consumed
 * prefix ends at the closing quote.
 *
 * Grammar (RFC 8259): any byte >= 0x20 except '"' and '\' between the
 * quotes; escapes of `" \ / b f n r t` and `\uXXXX` (4 hex digits, both
 * cases). Raw multi-byte sequences are validated with the strict RFC 3629
 * semantics anchored in P7-1 — encoded surrogates (ED A0-BF), overlongs,
 * sequences beyond U+10FFFF and truncations all return InvalidUtf8.
 *
 * Lone surrogate escapes (e.g. "\ud800") are VALID here: V8 `JSON.parse`
 * accepts them, and pairing is a DECISION of the decoder (see
 * unescape_string), not of the validator.
 *
 * @cite 10.48550_arXiv.1902.08318 simdjson — string tokens are validated
 *       in one linear pass with escapes recognized as the only two-byte
 *       nesting entity; the linear grammar+UTF-8 structure mirrors that.
 * @cite 10.14778_3115404.3115416 Mison — keeping structural indexing free
 *       of full string decoding, which is why this layer returns a
 *       decomposition (span + escape count) instead of decoded bytes.
 */
[[nodiscard]] core::Result<StringParts, core::JsonErrc>
scan_string(const char *src, core::size_t len) NOEXCEPT;

/**
 * @brief Decode a JSON string token into unescaped UTF-8 bytes.
 *
 * Semantics locked with the owner (P7-3): the reference is V8
 * `JSON.parse`, empirically probed (escapes `" \ / b f n r t u` decoded,
 * raw controls < 0x20 rejected, raw DEL legal, lone-surrogate escapes
 * ACCEPTED). Decode policy per point kind:
 *   - simple escapes -> the mapped ASCII byte (\n -> 0x0A, ...);
 *   - \uXXXX -> UTF-8 of the code point; a HIGH surrogate followed
 *     immediately by a `\uXXXX` escape whose value is a LOW surrogate
 *     (DC00-DFFF) decodes to the astral code point as 4-byte UTF-8;
 *   - lone surrogate escapes: the 6 escape bytes are copied through
 *     VERBATIM (case-preserving) — the value has no valid UTF-8
 *     representation, escaping it is the only lossless encoding;
 *   - escaped \u0000 decodes to the single 0x00 byte (RFC 8259 legal).
 *
 * `src` must span the token as reported by scan_string (both quotes
 * included). Two passes validate first, so NO grammar error ever writes
 * to `dst`. `cap` must be at least the decoded length —
 * `StringParts::inner_len` is a safe upper bound; with a smaller cap the
 * function returns InsufficientBuffer with dst fully untouched.
 *
 * @cite 10.48550_arXiv.1902.08318 simdjson — copies-and-decodes the
 *       string body backward/forward with every escape resolved to
 *       UTF-8; this is the scalar baseline the P4-3 table fast path
 *       must beat.
 */
[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
unescape_string(const char *src, core::size_t len, char *dst,
                core::size_t cap) NOEXCEPT;

} // namespace zuu::ondemand
