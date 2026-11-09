#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/core/result.hpp>

namespace zuu::ondemand {

class Document; // defined in document.hpp (one-directional: document.hpp -> value.hpp)

 enum class NodeType : core::uint8_t {
  Unknown = 0, // launcher byte cannot begin a JSON value
  Object = 1,  // '{'
  Array = 2,   // '['
  String = 3,  // '"'
  Number = 4,  // '-' or 0..9
  BoolTrue = 5,  // 't' ("true")
  BoolFalse = 6, // 'f' ("false")
  Null = 7,    // 'n' ("null")
};

//! Payload mirrors core::kOndemandNodeType exactly (index one load). Keep in sync with scripts/GenerateLookups.py.
[[nodiscard]] constexpr NodeType node_type_from_byte(core::uint8_t tag) NOEXCEPT {
  return static_cast<NodeType>(tag);
}

struct StringSpan {
  const char *start;      // AT the opening quote (source-backed, zero-copy)
  core::size_t total_len; // including BOTH quotes
  bool escaped;           // escape_count > 0: decode via unescape_string before reading
};

struct Field;

class ArrayIter;
class ObjectIter;

class Value {
public:
  Value() NOEXCEPT : document_(nullptr), token_start_(0) {}

  [[nodiscard]] NodeType type() const NOEXCEPT;

  /**
   * @brief Materialize a scalar value (P7-2/P7-3 consumer).
   *
   * Scalar promotion is lazy: the touched token is validated against the
   * FULL grammar (and UTF-8 for strings) on the immutable source bytes —
   * exactly the layers P7-2/P7-3 deliver. Re-accessing the same token
   * re-runs the same deterministic scan (values are immutable; no arena
   * caching at P7-4, the repeated-scan cost is a P4-3 optimization
   * concern alongside the decode paths).
   */
  [[nodiscard]] core::Result<StringSpan, core::JsonErrc> get_string_span() const NOEXCEPT;
  [[nodiscard]] core::Result<double, core::JsonErrc> get_double() const NOEXCEPT;
  [[nodiscard]] core::Result<bool, core::JsonErrc> get_bool() const NOEXCEPT;
  [[nodiscard]] bool is_null() const NOEXCEPT;

  /**
   * @brief Enter a container at this position (children promote lazily).
   *
   * The container's own OPEN bracket must be the launcher byte (checked
   * here); contents validate as the iterators walk them. The Structural
   * index capacity allocated at open equals the source byte count, so
   * capacity can not be the failure mode of traversal — all framing
   * violations surface as InvalidJson.
   */
  [[nodiscard]] core::Result<ArrayIter, core::JsonErrc> begin_array() const NOEXCEPT;
  [[nodiscard]] core::Result<ObjectIter, core::JsonErrc> begin_object() const NOEXCEPT;

  /**
   * @brief Element count WITHOUT materializing values (index-walk skip).
   *
   * Framing exactness is taken from the same machinery (a stray
   * structural byte among the children is a framing violation -> error);
   * the scalar CONTENTS of counted children are validated only at the
   * cheap grammar level (no decode, no number rounding).
   */
  [[nodiscard]] core::Result<core::size_t, core::JsonErrc> count_elements() const NOEXCEPT;

  // -- raw access for the on-demand object graph internals ------------------
  [[nodiscard]] const class Document *document() const NOEXCEPT { return document_; }
  [[nodiscard]] core::size_t token_start() const NOEXCEPT { return token_start_; }
  [[nodiscard]] core::size_t index_cursor() const NOEXCEPT { return index_cursor_; }

  const class Document *document_; // nullptr -> detached (NoValue) launcher
  core::size_t token_start_;  // byte position of the value's launcher token
  // Structural-index position of the first index at-or-after `token_start_`
  // (own opener for containers, trailing delimiter for scalars). Threaded
  // through the minting sites so nested begin_* stay FORWARD-ONLY (paper
  // 10.1002_spe.3313: single forward iterator, O(1) re-entry from a value).
  core::size_t index_cursor_ = 0;
};

struct Field {
  StringSpan key; // potentially unescaped (escaped == true -> decode first)
  Value value;
};

class ArrayIter {
public:
  explicit operator bool() const NOEXCEPT {
    return !done_ && failed_ == core::JsonErrc::None;
  }
  //! Current element; deref past-end / framing violation -> error result.
  [[nodiscard]] core::Result<Value, core::JsonErrc> operator*() const NOEXCEPT;
  ArrayIter &operator++() NOEXCEPT; // prefix; after the last element -> done
  [[nodiscard]] bool operator==(const ArrayIter &other) const NOEXCEPT {
    return done_ == other.done_;
  }
  [[nodiscard]] bool operator!=(const ArrayIter &other) const NOEXCEPT {
    return done_ != other.done_;
  }
  [[nodiscard]] static ArrayIter end() NOEXCEPT {
    ArrayIter it;
    it.done_ = true;
    return it;
  }

  [[nodiscard]] core::JsonErrc failure() const NOEXCEPT { return failed_; }

  bool done_ = false;
  const Document *document_ = nullptr;
  core::size_t index_cursor_ = 0; // first index at-or-after `token_start_`
  core::size_t token_start_ = 0;  // byte position of the current child
  core::JsonErrc failed_ = core::JsonErrc::None; // sticky framing error
};

class ObjectIter {
public:
  explicit operator bool() const NOEXCEPT {
    return !done_ && failed_ == core::JsonErrc::None;
  }
  /**
   * @brief The current field: key span (quoted, possibly escaped) + value.
   *
   * The KEY token is grammar-validated here (the caller needs its span);
   * the ':' separator and the value's launcher byte are framed EAGERLY so
   * a field-erasing key/value mismatch surfaces at deref time itself.
   * The VALUE token materializes by its own accessors only.
   */
  [[nodiscard]] core::Result<Field, core::JsonErrc> operator*() const NOEXCEPT;
  ObjectIter &operator++() NOEXCEPT;
  [[nodiscard]] bool operator==(const ObjectIter &other) const NOEXCEPT {
    return done_ == other.done_;
  }
  [[nodiscard]] bool operator!=(const ObjectIter &other) const NOEXCEPT {
    return done_ != other.done_;
  }
  [[nodiscard]] static ObjectIter end() NOEXCEPT {
    ObjectIter it;
    it.done_ = true;
    return it;
  }

  [[nodiscard]] core::JsonErrc failure() const NOEXCEPT { return failed_; }

  bool done_ = false;
  const Document *document_ = nullptr;
  // Mutable framing state: operator* (const) advances the object cursor to
  // the framed value position for the following operator++ (documented
  // forward-only contract; single-threaded by design).
  mutable core::size_t index_cursor_ = 0; // first index at-or-after the token
  mutable core::size_t key_start_ = 0;    // byte position of the '"key"'
  mutable core::size_t value_start_ = 0;  // byte position seen by operator*
  mutable core::JsonErrc failed_ = core::JsonErrc::None; // sticky error
};

} // namespace zuu::ondemand
