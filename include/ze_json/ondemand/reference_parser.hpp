#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/core/result.hpp>
#include <ze_json/ondemand/number.hpp>
#include <ze_json/ondemand/string.hpp>
#include <ze_json/ondemand/value.hpp>

namespace zuu::ondemand {

/**
 * @brief Scalar full-walk reference for the on-demand parser verdicts
 *        (TASKS P7-6).
 *
 * The walk logic expressed in its most direct executable form: a
 * recursive reader over the raw bytes (no structural index, no
 * generated table) whose ACCEPT/REJECT verdict for a whole document
 * must EXACTLY equal the verdict the on-demand API produces when a
 * caller walks the FULL tree (every skip latitude of the lazy layer is
 * undone by visiting every child). The differential test gates that
 * equivalence document-for-document, verdict-for-verdict — this is the
 * parser-side counterpart of `reference_scan` for the structural stage
 * (cpp-highperf-json rule 4; AGENTS.md section 6).
 *
 * Token-level scanning deliberately REUSES the already-anchored P7-2/P7-3
 * scanners (`scan_number`/`scan_string`): their verdicts and `total_len`
 * are the single grammar source for strings and numbers, so a codegen
 * bug cannot live inside the reference itself; this header contributes
 * only the frame walk (whitespace, containers, delimiters, tails).
 *
 * Framing semantics mirrored from the on-demand layer (document.cpp):
 * ws-only/empty input -> EmptyJson; unknown launcher -> InvalidJson;
 * scalar root followed by whitespace only to EOF; every object child is
 * `"key"` then `':'` then a value (so `{"a"}` frames as a violation just
 * like the eager field framing in ObjectIter::operator*); every array
 * child is delimited by ',' or the matching closer with a whitespace-only
 * gap; cross-type closers reject.
 *
 * json_checker contrast (the legacy RFC-8259 exceptions in the corpus
 * per tests/CMakeLists.txt): a top-level string IS accepted (fail1 —
 * V8 `JSON.parse('"a"')` accepts), and so is the 20-level nesting
 * (fail18); the differential test pins the shared verdicts, not the
 * checker's parser-independent depth rule.
 *
 * Recursion depth equals the input's nesting depth — bounded by the
 * test corpus by design (the lazy layer itself stays iterative, and its
 * deep-nesting skip gate lives in tests/value_test.cpp).
 *
 * @cite 10.1002_spe.3313 on-demand JSON — the full-walk reader is the
 *       always-visiting counterpart of the lazy single forward iterator;
 *       verdict equivalence at full traversal is the correctness gate.
 * @cite 10.14778_3115404.3115416 Mison — the reference proves correct
 *       parsing needs NO structural index, only speed does.
 */
//! Launch-byte classification WITHOUT the generated lookup table: pure
//! first-byte branching (8 cases). Any bug in the generated table
//! therefore cannot sneak past a differential test built on this kind.
[[nodiscard]] inline NodeType
ref_value_kind(const char *src, core::size_t size, core::size_t pos) NOEXCEPT {
  if (pos >= size) {
    return NodeType::Unknown;
  }
  switch (static_cast<core::uint8_t>(src[pos])) {
  case '{':
    return NodeType::Object;
  case '[':
    return NodeType::Array;
  case '"':
    return NodeType::String;
  case 't':
    return NodeType::BoolTrue;
  case 'f':
    return NodeType::BoolFalse;
  case 'n':
    return NodeType::Null;
  default:
    break; // '-' and digits classify below
  }
  const core::uint8_t b = static_cast<core::uint8_t>(src[pos]);
  return (b == '-' || (b >= '0' && b <= '9')) ? NodeType::Number
                                              : NodeType::Unknown;
}

namespace detail {

//! RFC 8259 whitespace: exactly SP / TAB / LF / CR, byte-wise (the plain
//! counterpart of kJsonCharClass's ws rule used by skip_ws upstream).
[[nodiscard]] inline bool ws_byte(core::uint8_t b) NOEXCEPT {
  return b == ' ' || b == '\t' || b == '\n' || b == '\r';
}

//! Whitespace skip over raw bytes.
[[nodiscard]] inline core::size_t skip_ws_bytes(const char *src,
                                                core::size_t size,
                                                core::size_t pos) NOEXCEPT {
  while (pos < size && ws_byte(static_cast<core::uint8_t>(src[pos]))) {
    ++pos;
  }
  return pos;
}

//! Byte-exact literal check ("true"/"false"/"null") — no table, no
//! unrolling: the reference stays executable by inspection.
[[nodiscard]] inline bool lit_bytes_equal(const char *src, core::size_t size,
                                          core::size_t pos, const char *lit,
                                          core::size_t n) NOEXCEPT {
  if (pos + n > size) {
    return false;
  }
  for (core::size_t i = 0; i < n; ++i) {
    if (static_cast<core::uint8_t>(src[pos + i]) !=
        static_cast<core::uint8_t>(lit[i])) {
      return false;
    }
  }
  return true;
}

// Out-of-line definitions live below their callers (mutual recursion);
// forward declarations keep the container walkers shaped like the on
// // -demand source they mirror.
[[nodiscard]] inline core::Result<core::size_t, core::JsonErrc>
parse_value_bytes(const char *src, core::size_t size, core::size_t pos)
    NOEXCEPT;
[[nodiscard]] inline core::Result<core::size_t, core::JsonErrc>
parse_array_bytes(const char *src, core::size_t size, core::size_t pos)
    NOEXCEPT;
[[nodiscard]] inline core::Result<core::size_t, core::JsonErrc>
parse_object_bytes(const char *src, core::size_t size, core::size_t pos)
    NOEXCEPT;

/**
 * @brief Array parse: opener consumed, elements walked to the closer.
 *
 * Loop = the framing rules of ArrayIter: a child value, then ',' (next
 * element) or the matching closer ']' with a whitespace-only gap. EOF,
 * a stray structural byte, or a cross-type closer rejects.
 */
[[nodiscard]] inline core::Result<core::size_t, core::JsonErrc>
parse_array_bytes(const char *src, core::size_t size, core::size_t pos)
    NOEXCEPT {
  core::size_t cursor = skip_ws_bytes(src, size, pos + 1);
  if (cursor < size && static_cast<core::uint8_t>(src[cursor]) == ']') {
    return cursor + 1; // `[]` : the closer frames the empty array
  }
  while (true) {
    if (cursor >= size) {
      return core::JsonErrc::InvalidJson; // unterminated array
    }
    if (ref_value_kind(src, size, cursor) == NodeType::Unknown) {
      return core::JsonErrc::InvalidJson; // `[1,]` / stray byte
    }
    const auto child = parse_value_bytes(src, size, cursor);
    if (!child) {
      return child.error();
    }
    cursor = skip_ws_bytes(src, size, child.value());
    if (cursor >= size) {
      return core::JsonErrc::InvalidJson; // no delimiter before EOF
    }
    const core::uint8_t b = static_cast<core::uint8_t>(src[cursor]);
    if (b == ',') {
      cursor = skip_ws_bytes(src, size, cursor + 1);
      continue;
    }
    if (b == ']') {
      return cursor + 1;
    }
    return core::JsonErrc::InvalidJson; // `[1 2]` / cross-type closer
  }
}

/**
 * @brief Object parse: opener consumed, `"key": value` pairs walked.
 *
 * The key grammar is a string ONLY (`{1:2}` / `{,}` reject at this
 * gate); then ':' (whitespace around both, `{"a"}`/`{"a" 1}` reject);
 * then a value handed back to parse_value_bytes. After that the same
 * ',' / closer loop as the array walker — the full-walk mirror of the
 * eager field framing in ObjectIter.
 */
[[nodiscard]] inline core::Result<core::size_t, core::JsonErrc>
parse_object_bytes(const char *src, core::size_t size, core::size_t pos)
    NOEXCEPT {
  core::size_t cursor = skip_ws_bytes(src, size, pos + 1);
  if (cursor < size && static_cast<core::uint8_t>(src[cursor]) == '}') {
    return cursor + 1; // `{}` : the closer frames the empty object
  }
  while (true) {
    if (cursor >= size) {
      return core::JsonErrc::InvalidJson; // unterminated object
    }
    if (static_cast<core::uint8_t>(src[cursor]) != '"') {
      return core::JsonErrc::InvalidJson; // key must start a string token
    }
    const auto key = scan_string(src + cursor, size - cursor);
    if (!key) {
      return key.error(); // grammar + UTF-8 straight from P7-3
    }
    cursor = skip_ws_bytes(src, size, cursor + key.value().total_len);
    if (cursor >= size || static_cast<core::uint8_t>(src[cursor]) != ':') {
      return core::JsonErrc::InvalidJson; // `{"a"}` / `{"a" 1}` / EOF
    }
    cursor = skip_ws_bytes(src, size, cursor + 1);
    const auto child = parse_value_bytes(src, size, cursor);
    if (!child) {
      return child.error(); // missing value (`{"a":}` frames here)
    }
    cursor = skip_ws_bytes(src, size, child.value());
    if (cursor >= size) {
      return core::JsonErrc::InvalidJson; // no delimiter before EOF
    }
    const core::uint8_t b = static_cast<core::uint8_t>(src[cursor]);
    if (b == ',') {
      cursor = skip_ws_bytes(src, size, cursor + 1);
      continue;
    }
    if (b == '}') {
      return cursor + 1;
    }
    return core::JsonErrc::InvalidJson; // cross-type closer / stray byte
  }
}

//! One JSON value's full-walk parse: returns the byte offset one PAST
//! the value's last byte. `pos` must not point at whitespace (callers
//! skip it). Scalars promote through the P7-2/P7-3 scanners; containers
//! recurse (see the depth note on the class doc).
[[nodiscard]] inline core::Result<core::size_t, core::JsonErrc>
parse_value_bytes(const char *src, core::size_t size, core::size_t pos)
    NOEXCEPT {
  if (pos >= size) {
    return core::JsonErrc::InvalidJson;
  }
  switch (ref_value_kind(src, size, pos)) {
  case NodeType::Object:
    return parse_object_bytes(src, size, pos);
  case NodeType::Array:
    return parse_array_bytes(src, size, pos);
  case NodeType::String: {
    const auto parts = scan_string(src + pos, size - pos);
    if (!parts) {
      return parts.error(); // grammar + UTF-8 rules straight from P7-3
    }
    return pos + parts.value().total_len;
  }
  case NodeType::Number: {
    const auto parts = scan_number(src + pos, size - pos);
    if (!parts) {
      return parts.error(); // grammar straight from P7-2
    }
    return pos + parts.value().total_len;
  }
  case NodeType::BoolTrue:
    return lit_bytes_equal(src, size, pos, "true", 4)
               ? core::Result<core::size_t, core::JsonErrc>(pos + 4)
               : core::Result<core::size_t, core::JsonErrc>(
                     core::JsonErrc::InvalidJson);
  case NodeType::BoolFalse:
    return lit_bytes_equal(src, size, pos, "false", 5)
               ? core::Result<core::size_t, core::JsonErrc>(pos + 5)
               : core::Result<core::size_t, core::JsonErrc>(
                     core::JsonErrc::InvalidJson);
  case NodeType::Null:
    return lit_bytes_equal(src, size, pos, "null", 4)
               ? core::Result<core::size_t, core::JsonErrc>(pos + 4)
               : core::Result<core::size_t, core::JsonErrc>(
                     core::JsonErrc::InvalidJson);
  default:
    return core::JsonErrc::InvalidJson; // Unknown launcher byte
  }
}

} // namespace detail

/**
 * @brief Verdict + consumed length of one full-document scalar walk.
 *
 * Empty/whitespace-only input is EmptyJson (the root contract), any
 * non-whitespace byte after the single root value rejects (`{} {}`,
 * `1 ,`, `[] abc`), and the value's own tail must reach EOF. Returns
 * the byte offset one past the value — the "consumed" count of the
 * walk; the verdict is the interesting half.
 */
[[nodiscard]] inline core::Result<core::size_t, core::JsonErrc>
ref_parse_document(const char *src, core::size_t size) NOEXCEPT {
  const core::size_t root = detail::skip_ws_bytes(src, size, 0);
  if (root >= size) {
    return core::JsonErrc::EmptyJson; // empty or whitespace-only
  }
  const auto consumed = detail::parse_value_bytes(src, size, root);
  if (!consumed) {
    return consumed.error();
  }
  const core::size_t after = detail::skip_ws_bytes(src, size, consumed.value());
  if (after != size) {
    return core::JsonErrc::InvalidJson; // non-ws tail after the root value
  }
  return consumed.value();
}

} // namespace zuu::ondemand
