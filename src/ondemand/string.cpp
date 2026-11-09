// JSON string token parsing (TASKS P7-3): RFC 8259 grammar scan +
// unescape -> UTF-8, V8-parity semantics.
//
// Semantics locked with the owner (probed empirically against V8
// JSON.parse, see tests/string_test.cpp V8ParityEdges):
//   - escapes " \ / b f n r t u are legal (everything else invalid);
//   - raw controls 0x00-0x1F invalid, raw DEL legal;
//   - raw non-ASCII bytes must form strict RFC 3629 UTF-8 (the P7-1
//     anchor rejects encoded surrogates / overlongs / truncations);
//   - \uXXXX escapes of ANY value are valid tokens, incl. lone
//     surrogates (V8 accepts them);
//   - SURROGATE OUTPUT POLICY: a high-low pair decodes to the astral
//     code point as 4-byte UTF-8; a LONE surrogate escape is copied
//     through VERBATIM (case-preserving 6 bytes) — pass-through locked
//     decision: the value has no valid UTF-8 representation and the
//     escape is the only lossless encoding.
//
// The walker is one deterministic function used twice per unescape
// (length pass, then write pass), so InsufficientBuffer leaves dst fully
// untouched and a grammar error inside the token leaves dst untouched as
// well. Single-pass fusion is future work (P4-3 / P7-7 measurement).
//
// @cite 10.48550_arXiv.1902.08318 simdjson — one linear pass recognizes
//       the string span with escapes as the only nesting entity, then
//       the body is copied with escapes resolved to UTF-8.
// @cite 10.14778_3115404.3115416 Mison — span + escape-count
//       decomposition instead of eager decoding keeps structural
//       indexing decoupled from string materialization.
#include <ze_json/ondemand/string.hpp>

#include <ze_json/core/integral.hpp>
#include <ze_json/utils/utf8_validator.hpp>

namespace zuu::ondemand {

namespace {

/** Escape body classification (the byte after the backslash). */
inline bool is_simple_escape(core::uint8_t e) noexcept {
  return e == core::uint8_t{'"'} || e == core::uint8_t{'\\'} ||
         e == core::uint8_t{'/'} || e == core::uint8_t{'b'} ||
         e == core::uint8_t{'f'} || e == core::uint8_t{'n'} ||
         e == core::uint8_t{'r'} || e == core::uint8_t{'t'};
}

/** Target byte of a simple escape (callers validated the class first). */
[[nodiscard]] inline core::uint8_t
simple_escape_byte(core::uint8_t e) noexcept {
  constexpr core::uint8_t kSimple[]{0x22, 0x5C, 0x2F, 0x08,
                                    0x0C, 0x0A, 0x0D, 0x09};
  // '"' '\\' '/' 'b' 'f' 'n' 'r' 't' — dense-enough switch keeps this
  // branch-free enough for the scalar baseline; table is P4-3 territory.
  if (e == core::uint8_t{'"'}) {
    return kSimple[0];
  }
  if (e == core::uint8_t{'\\'}) {
    return kSimple[1];
  }
  if (e == core::uint8_t{'/'}) {
    return kSimple[2];
  }
  if (e == core::uint8_t{'b'}) {
    return kSimple[3];
  }
  if (e == core::uint8_t{'f'}) {
    return kSimple[4];
  }
  if (e == core::uint8_t{'n'}) {
    return kSimple[5];
  }
  if (e == core::uint8_t{'r'}) {
    return kSimple[6];
  }
  return kSimple[7];
}

/** 4 hex digits -> value; false on any non-hex digit (both cases). */
[[nodiscard]] inline bool hex4(const char *p, core::uint32_t &out) noexcept {
  core::uint32_t v = 0;
  for (int k = 0; k < 4; ++k) {
    const auto b = static_cast<core::uint8_t>(p[k]);
    core::uint32_t d;
    if (b >= 0x30 && b <= 0x39) { // '0'..'9'
      d = static_cast<core::uint32_t>(b - 0x30);
    } else if (b >= 0x61 && b <= 0x66) { // 'a'..'f'
      d = static_cast<core::uint32_t>(b - 0x61) + 10;
    } else if (b >= 0x41 && b <= 0x46) { // 'A'..'F'
      d = static_cast<core::uint32_t>(b - 0x41) + 10;
    } else {
      return false;
    }
    v = (v * 16U) + d; // v < 0x10000 before the update, no overflow
  }
  out = v;
  return true;
}

/**
 * Advance of the strict-UTF-8 sequence starting at `s` (0 = invalid:
 * illegal lead byte, truncated by `avail`, wrong windows/continuations).
 * Reuses the P7-1 scalar anchor so both layers share one truth source.
 */
[[nodiscard]] inline core::size_t
utf8_seq_advance(const char *s, core::size_t avail) noexcept {
  const auto b0 = static_cast<core::uint8_t>(s[0]);
  core::size_t need;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    need = 2;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    need = 3;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    need = 4;
  } else {
    return 0; // stray continuation, C0/C1, F5-FF
  }
  if (avail < need) {
    return 0; // truncated by the end of the token interior
  }
  return utils::utf8_validate_scalar(s, need) ? need : 0;
}

/** Encode one code point into strict UTF-8 (`out` needs <= 4 bytes). */
inline core::size_t encode_utf8(core::uint32_t cp, char *out) noexcept {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0U | (cp >> 6U));
    out[1] = static_cast<char>(0x80U | (cp & 0x3FU));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0U | (cp >> 12U));
    out[1] = static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
    out[2] = static_cast<char>(0x80U | (cp & 0x3FU));
    return 3;
  }
  out[0] = static_cast<char>(0xF0U | (cp >> 18U));
  out[1] = static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
  out[2] = static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
  out[3] = static_cast<char>(0x80U | (cp & 0x3FU));
  return 4;
}

/**
 * Deterministic walker over the interior of a JSON string:
 * `s` = byte after the opening quote, exactly `inner_len` bytes walkable
 * (the closing quote is NOT part of the span). Validates the same rules
 * as the scanner (grammar + strict UTF-8) and, when `out` is non-null,
 * writes the decoded bytes applying the surrogate output policy. Returns
 * the decoded byte count or the first error.
 */
[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
decode_interior(const char *s, core::size_t inner_len, char *out) noexcept {
  core::size_t w = 0; // decoded bytes written (or counted when out = null)
  core::size_t i = 0; // walk position inside the interior
  while (i < inner_len) {
    const auto b = static_cast<core::uint8_t>(s[i]);
    if (b != 0x5C) { // not a backslash: raw byte
      if (b < 0x20) {
        return core::JsonErrc::InvalidJson; // raw control char
      }
      if (b < 0x80) {
        if (out != nullptr) {
          out[w] = static_cast<char>(b);
        }
        w += 1;
        i += 1;
        continue;
      }
      const core::size_t adv = utf8_seq_advance(s + i, inner_len - i);
      if (adv == 0) {
        return core::JsonErrc::InvalidUtf8;
      }
      if (out != nullptr) {
        __builtin_memcpy(out + w, s + i, adv);
      }
      w += adv;
      i += adv;
      continue;
    }
    // Backslash escape.
    if (inner_len < i + 2) {
      return core::JsonErrc::InvalidJson; // trailing backslash
    }
    const auto e = static_cast<core::uint8_t>(s[i + 1]);
    if (is_simple_escape(e)) {
      if (out != nullptr) {
        out[w] = static_cast<char>(simple_escape_byte(e));
      }
      w += 1;
      i += 2;
      continue;
    }
    if (e != 0x75) { // 'u'
      return core::JsonErrc::InvalidJson;
    }
    if (inner_len < i + 6) {
      return core::JsonErrc::InvalidJson; // truncated \uXXXX
    }
    core::uint32_t cp = 0;
    if (!hex4(s + i + 2, cp)) {
      return core::JsonErrc::InvalidJson;
    }
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      // High surrogate: pair with an IMMEDIATELY following \uXXXX escape
      // whose value is a low surrogate; anything else is a lone escape
      // that passes through verbatim (locked output policy).
      core::uint32_t lo = 0;
      if (inner_len >= i + 12 && s[i + 6] == 0x5C && s[i + 7] == 0x75 &&
          hex4(s + i + 8, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000U + ((cp - 0xD800) << 10U) + (lo - 0xDC00);
        // cp >= 0x10000 here, so the encoding is always 4 bytes.
        const core::size_t n = (out != nullptr) ? encode_utf8(cp, out + w) : 4;
        w += n;
        i += 12;
        continue;
      }
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
      // Lone low surrogate: also pass-through (cannot pair backward).
    } else {
      // Non-surrogate code point: encode per UTF-8.
      if (out != nullptr) {
        w += encode_utf8(cp, out + w);
      } else {
        w += cp < 0x80 ? 1 : (cp < 0x800 ? 2 : (cp < 0x10000 ? 3 : 4));
      }
      i += 6;
      continue;
    }
    // Lone surrogate escape: copy the 6 source bytes VERBATIM (this keeps
    // the original hex case so the token survives a byte-identical
    // JSON.stringify round-trip).
    if (out != nullptr) {
      __builtin_memcpy(out + w, s + i, 6);
    }
    w += 6;
    i += 6;
  }
  return w;
}

} // namespace

core::Result<StringParts, core::JsonErrc>
scan_string(const char *src, core::size_t len) NOEXCEPT {
  if (len == 0 || static_cast<core::uint8_t>(src[0]) != 0x22) {
    return core::JsonErrc::InvalidJson; // must start at the opening quote
  }
  core::size_t i = 1;
  core::size_t escapes = 0;
  while (i < len) {
    const auto b = static_cast<core::uint8_t>(src[i]);
    if (b == 0x22) { // closing quote
      return StringParts{i + 1, i - 1, escapes};
    }
    if (b != 0x5C) {
      if (b < 0x20) {
        return core::JsonErrc::InvalidJson; // raw control char
      }
      if (b < 0x80) {
        i += 1;
        continue;
      }
      const core::size_t adv = utf8_seq_advance(src + i, len - i);
      if (adv == 0) {
        return core::JsonErrc::InvalidUtf8;
      }
      i += adv;
      continue;
    }
    if (i + 1 >= len) {
      return core::JsonErrc::InvalidJson; // trailing backslash
    }
    const auto e = static_cast<core::uint8_t>(src[i + 1]);
    if (is_simple_escape(e)) {
      i += 2;
      escapes += 1;
      continue;
    }
    if (e != 0x75) { // 'u'
      return core::JsonErrc::InvalidJson;
    }
    if (len < i + 6) {
      return core::JsonErrc::InvalidJson; // truncated \uXXXX
    }
    core::uint32_t sink = 0;
    if (!hex4(src + i + 2, sink)) {
      return core::JsonErrc::InvalidJson;
    }
    i += 6;
    escapes += 1;
  }
  return core::JsonErrc::InvalidJson; // string hit the buffer end unclosed
}

core::Result<core::size_t, core::JsonErrc>
unescape_string(const char *src, core::size_t len, char *dst,
                core::size_t cap) NOEXCEPT {
  // Pass 1: full validation (grammar + UTF-8) — nothing writes on error.
  const auto parts = scan_string(src, len);
  if (parts.has_error()) {
    return parts.error();
  }
  const char *body = src + 1;
  const core::size_t inner_len = parts.value().inner_len;
  if (parts.value().escape_count == 0 && inner_len > 0) {
    // Plain copy fast path (raw bytes are already validated 1:1).
    if (cap < inner_len) {
      return core::JsonErrc::InsufficientBuffer;
    }
    __builtin_memcpy(dst, body, inner_len);
    return inner_len;
  }
  // Pass 2: exact decoded length (same deterministic walker, out = null).
  const auto need = decode_interior(body, inner_len, nullptr);
  if (need.has_error()) {
    return need.error(); // belt-and-braces: scan already validated these
  }
  if (cap < need.value()) {
    return core::JsonErrc::InsufficientBuffer; // dst untouched
  }
  // Pass 3: write (guaranteed to fit by the identical pass 2).
  return decode_interior(body, inner_len, dst);
}

} // namespace zuu::ondemand
