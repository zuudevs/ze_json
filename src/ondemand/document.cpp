#include <ze_json/ondemand/document.hpp>

#include <ze_json/core/lookups.hpp>
#include <ze_json/ondemand/number.hpp>
#include <ze_json/ondemand/scanner.hpp>
#include <ze_json/ondemand/string.hpp>

namespace zuu::ondemand {

namespace {

//! Whitelist class straight from the generated char-class table (1 = ws).
[[nodiscard]] inline core::uint8_t byte_class(const char *src,
                                              core::size_t pos) NOEXCEPT {
  return core::kJsonCharClass[static_cast<core::uint8_t>(src[pos])];
}

//! First non-whitespace byte position (may hit `size` at the very end).
[[nodiscard]] inline core::size_t skip_ws(const char *src, core::size_t size,
                                          core::size_t pos) NOEXCEPT {
  while (pos < size && byte_class(src, pos) == 1) {
    ++pos;
  }
  return pos;
}

//! [from, to) strictly whitespace (empty range vacuously true — including
//! the token-end == delimiter case, which is the tight `[1,2]` layout).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline bool bytes_are_ws(const Document &doc, core::size_t from,
                                       core::size_t to) NOEXCEPT {
  const char *src = doc.src();
  for (core::size_t i = from; i < to; ++i) {
    if (byte_class(src, i) != 1) {
      return false;
    }
  }
  return true;
}

//! Exact literal comparison of a token body ("true"/"false"/"null").
[[nodiscard]] inline bool lit_equal(const Document &doc, core::size_t pos,
                                    const char *lit,
                                    core::size_t n) NOEXCEPT {
  if (pos + n > doc.src_size()) {
    return false;
  }
  const char *src = doc.src();
  for (core::size_t i = 0; i < n; ++i) {
    if (src[pos + i] != lit[i]) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Matching closer of a bracket-opener (pairing table input).
 *
 * `kNoBracket` marks an unmatched position — the pairing pass marks it
 * when the opener never closes or its closer interleaves cross-type.
 */
inline constexpr core::size_t kNoBracket = static_cast<core::size_t>(-1);

//! Index whose byte position is the first one >= `byte_pos`, walking only
//! FORWARD from `cursor` (each index is visited once per document walk).
[[nodiscard]] core::size_t first_index_at_or_after(const Document &doc,
                                                   core::size_t cursor,
                                                   core::size_t byte_pos) NOEXCEPT {
  while (cursor < doc.index_count() && doc.index_at(cursor) < byte_pos) {
    ++cursor;
  }
  return cursor;
}

/**
 * @brief One JSON token's LENGTH validated from its launcher byte.
 *
 * Grammar level only: scalars promote through the P7-2/P7-3 scanners,
 * literals ("true"/"false"/"null") compare byte-exact. A launcher that
 * cannot start any value returns InvalidJson — the iterator turns this
 * into its sticky error instead of surfacing `dst` output (documented
 * untouch contract of unescape_string mirrors that discipline).
 */
[[nodiscard]] core::Result<core::size_t, core::JsonErrc>
frame_token_len(const Document &doc, core::size_t pos) NOEXCEPT {
  if (pos >= doc.src_size()) {
    return core::JsonErrc::InvalidJson;
  }
  const core::uint8_t kind = doc.classify(pos);
  const char *src = doc.src();
  const core::size_t left = doc.src_size() - pos;
  switch (kind) {
  case static_cast<core::uint8_t>(NodeType::String): {
    const auto parts = scan_string(src + pos, left);
    if (!parts) {
      return parts.error();
    }
    return parts.value().total_len;
  }
  case static_cast<core::uint8_t>(NodeType::Number): {
    const auto parts = scan_number(src + pos, left);
    if (!parts) {
      return parts.error();
    }
    return parts.value().total_len;
  }
  case static_cast<core::uint8_t>(NodeType::BoolTrue):
    if (!lit_equal(doc, pos, "true", 4)) {
      return core::JsonErrc::InvalidJson;
    }
    return core::size_t{4};
  case static_cast<core::uint8_t>(NodeType::BoolFalse):
    if (!lit_equal(doc, pos, "false", 5)) {
      return core::JsonErrc::InvalidJson;
    }
    return core::size_t{5};
  case static_cast<core::uint8_t>(NodeType::Null):
    if (!lit_equal(doc, pos, "null", 4)) {
      return core::JsonErrc::InvalidJson;
    }
    return core::size_t{4};
  default:
    return core::JsonErrc::InvalidJson;
  }
}

//! Framed position of the next child after a consumed ',' delimiter.
struct NextChild {
  core::size_t token_start;
  core::size_t index_cursor; // first index at-or-after `token_start`
};

[[nodiscard]] core::Result<NextChild, core::JsonErrc>
next_child_after_comma(const Document &doc, core::size_t comma_index) NOEXCEPT {
  const core::size_t nts =
      skip_ws(doc.src(), doc.src_size(), doc.index_at(comma_index) + 1);
  if (nts >= doc.src_size()) {
    return core::JsonErrc::InvalidJson; // [1, <eof>
  }
  const core::size_t j = first_index_at_or_after(doc, comma_index + 1, nts);
  if (j >= doc.index_count()) {
    return core::JsonErrc::InvalidJson; // no closer/delimiter ahead
  }
  const core::uint8_t kind = doc.classify(nts);
  if (kind == 0) {
    return core::JsonErrc::InvalidJson; // trailing comma / stray structural
  }
  if ((kind == static_cast<core::uint8_t>(NodeType::Array) ||
       kind == static_cast<core::uint8_t>(NodeType::Object)) &&
      doc.index_at(j) != nts) {
    return core::JsonErrc::InvalidJson; // the opener must own its index
  }
  return NextChild{nts, j};
}

//! The delimiter byte after the framed scalar tool `token_end` must be in
//! {',', closer} with a whitespace-only gap; else the framing is broken.
[[nodiscard]] core::Result<core::uint8_t, core::JsonErrc>
framing_delimiter(const Document &doc, core::size_t token_end,
                  core::size_t &index_cursor) NOEXCEPT {
  if (index_cursor >= doc.index_count()) {
    return core::JsonErrc::InvalidJson; // [1,2 <eof> / gap without a closer
  }
  const core::size_t delim_pos = doc.index_at(index_cursor);
  if (delim_pos < token_end ||
      !bytes_are_ws(doc, token_end, delim_pos)) {
    return core::JsonErrc::InvalidJson; // e.g. [1 2] / ["a" "b"]
  }
  return static_cast<core::uint8_t>(doc.src()[delim_pos]);
}

/**
 * @brief Framed position of the next object key after a consumed ','.
 *
 * An object key must be a string token (launcher '"'); the returned index
 * cursor is the first index at-or-after it — the ':' that separates the
 * key and the value. `next_child_after_comma` (array) and
 * `next_key_after_comma` (object) differ only in the launcher rejection.
 */
[[nodiscard]] core::Result<NextChild, core::JsonErrc>
next_key_after_comma(const Document &doc, core::size_t comma_index) NOEXCEPT {
  const core::size_t nts =
      skip_ws(doc.src(), doc.src_size(), doc.index_at(comma_index) + 1);
  if (nts >= doc.src_size() ||
      doc.classify(nts) != static_cast<core::uint8_t>(NodeType::String)) {
    return core::JsonErrc::InvalidJson; // {"a": 1,} / {"a": 1} # gone-key
  }
  const core::size_t j = first_index_at_or_after(doc, comma_index + 1, nts);
  if (j >= doc.index_count()) {
    return core::JsonErrc::InvalidJson; // no ':' after the key anywhere
  }
  return NextChild{nts, j};
}

} // namespace

// clang-format off
// === Document =============================================================
// clang-format on

core::Result<Document, core::JsonErrc>
Document::open(memory::Arena &arena, const char *src,
               core::size_t src_size) NOEXCEPT {
  if (src_size != 0 && src == nullptr) {
    return core::JsonErrc::InvalidJson; // scanner contract mirrored up-front
  }
  Document doc;
  doc.arena_ = &arena;
  doc.src_ = src;
  doc.src_size_ = src_size;
  doc.indexes_ = nullptr;
  doc.index_count_ = 0;
  doc.match_ = nullptr;
  if (src_size != 0) {
    // Upper bounds: every entry (index, match, stack slot) stores at most
    // one per structural byte, and the stack can hold every opener once.
    const core::size_t slot_bytes = src_size * sizeof(core::size_t);
    core::size_t *const indexes =
        static_cast<core::size_t *>(arena.allocate(slot_bytes));
    core::size_t *const match =
        static_cast<core::size_t *>(arena.allocate(slot_bytes));
    core::size_t *const stack =
        static_cast<core::size_t *>(arena.allocate(slot_bytes));
    if (indexes == nullptr || match == nullptr || stack == nullptr) {
      return core::JsonErrc::InsufficientBuffer;
    }
    const auto scan =
        structural_scan(src, src_size, indexes, src_size);
    if (!scan) {
      return scan.error(); // EmptyJson/InvalidJson/InsufficientBuffer as-is
    }
    const core::size_t n = scan.value();
    // LIFO pairing over the flat index stream (Mison-style staging): an
    // opener pushes its index; a closer pairs with the TOP only when the
    // TYPE matches — the crossed-closer case (`[ { ] }`) marks BOTH ends
    // kNoBracket and keeps walking. Marking (not erroring) keeps open
    // structural-knowledge-ONLY; every consumer sees kNoBracket as its
    // framing violation.
    core::size_t sp = 0;
    for (core::size_t j = 0; j < n; ++j) {
      const char b = src[indexes[j]];
      if (b == '[' || b == '{') {
        match[j] = kNoBracket;
        stack[sp++] = j;
        continue;
      }
      if (b != ']' && b != '}') {
        match[j] = kNoBracket; // ',' / ':' are delimiters, not brackets
        continue;
      }
      if (sp == 0) {
        match[j] = kNoBracket; // closer without opener
        continue;
      }
      const core::size_t opener = stack[--sp];
      const char open_char = src[indexes[opener]];
      if ((open_char == '[' && b != ']') || (open_char == '{' && b != '}')) {
        match[opener] = kNoBracket; // crossed brackets: both ends unmatched
        match[j] = kNoBracket;
        continue;
      }
      match[opener] = j;
      match[j] = j;
    }
    doc.indexes_ = indexes;
    doc.match_ = match;
    doc.index_count_ = n;
  }
  return doc;
}

core::uint8_t Document::classify(core::size_t byte_pos) const NOEXCEPT {
  if (src_ == nullptr || byte_pos >= src_size_) {
    return 0;
  }
  return core::kOndemandNodeType[static_cast<core::uint8_t>(src_[byte_pos])];
}

core::Result<Value, core::JsonErrc> Document::get_root() const NOEXCEPT {
  const core::size_t pos = skip_ws(src_, src_size_, 0);
  if (src_ == nullptr || pos >= src_size_) {
    return core::JsonErrc::EmptyJson;
  }
  const core::uint8_t kind = classify(pos);
  if (kind == 0) {
    return core::JsonErrc::InvalidJson;
  }
  if (kind == static_cast<core::uint8_t>(NodeType::Array) ||
      kind == static_cast<core::uint8_t>(NodeType::Object)) {
    // Root framing: the opener owns the whole index walk — leading entries
    // can not exist (whitespace-only prefix), so indexes_[0] must BE it,
    // and the matching closer must be the LAST index (anything behind is
    // trailing document garbage: `{} {}` is not one document).
    if (index_count_ == 0 || index_at(0) != pos) {
      return core::JsonErrc::InvalidJson;
    }
    const core::size_t close = index_close(0);
    if (close == kNoBracket || close != index_count_ - 1) {
      return core::JsonErrc::InvalidJson;
    }
    // Trailing bytes AFTER the closing bracket must be whitespace only
    // (non-structural garbage is invisible to the index: `[] abc`).
    if (!bytes_are_ws(*this, index_at(close) + 1, src_size_)) {
      return core::JsonErrc::InvalidJson;
    }
    Value value;
    value.document_ = this;
    value.token_start_ = pos;
    value.index_cursor_ = 0; // own opener (defensive; begin_* re-checks)
    return value;
  }
  // Scalar root: nothing after the token but whitespace, and no structural
  // index anywhere in the document (`1 ,` / "a" 5 are not valid root ends).
  const auto len = frame_token_len(*this, pos);
  if (!len) {
    return len.error();
  }
  if (!bytes_are_ws(*this, pos + len.value(), src_size_) || index_count_ != 0) {
    return core::JsonErrc::InvalidJson;
  }
  Value value;
  value.document_ = this;
  value.token_start_ = pos;
  value.index_cursor_ = 0;
  return value;
}

// === Value ================================================================

NodeType Value::type() const NOEXCEPT {
  if (document_ == nullptr) {
    return NodeType::Unknown;
  }
  return node_type_from_byte(document_->classify(token_start_));
}

core::Result<StringSpan, core::JsonErrc>
Value::get_string_span() const NOEXCEPT {
  if (document_ == nullptr ||
      document_->classify(token_start_) !=
          static_cast<core::uint8_t>(NodeType::String)) {
    return core::JsonErrc::InvalidJson;
  }
  const auto parts = scan_string(document_->src() + token_start_,
                                 document_->src_size() - token_start_);
  if (!parts) {
    return parts.error();
  }
  return StringSpan{document_->src() + token_start_, parts.value().total_len,
                    parts.value().escape_count != 0};
}

core::Result<double, core::JsonErrc> Value::get_double() const NOEXCEPT {
  if (document_ == nullptr || token_start_ >= document_->src_size()) {
    return core::JsonErrc::InvalidJson;
  }
  // Trailing bytes ignored by contract; framing is the iterator's job.
  return parse_number(document_->src() + token_start_,
                      document_->src_size() - token_start_);
}

core::Result<bool, core::JsonErrc> Value::get_bool() const NOEXCEPT {
  if (document_ == nullptr) {
    return core::JsonErrc::InvalidJson;
  }
  if (lit_equal(*document_, token_start_, "true", 4)) {
    return true;
  }
  if (lit_equal(*document_, token_start_, "false", 5)) {
    return false;
  }
  return core::JsonErrc::InvalidJson;
}

bool Value::is_null() const NOEXCEPT {
  return document_ != nullptr && lit_equal(*document_, token_start_, "null", 4);
}

core::Result<ArrayIter, core::JsonErrc> Value::begin_array() const NOEXCEPT {
  if (document_ == nullptr ||
      document_->classify(token_start_) !=
          static_cast<core::uint8_t>(NodeType::Array)) {
    return core::JsonErrc::InvalidJson;
  }
  // `[` is structural: the first index >= token_start_ must be the opener
  // itself; anything else means the value's cursor is out of contract.
  const core::size_t cursor =
      first_index_at_or_after(*document_, index_cursor_, token_start_);
  if (cursor >= document_->index_count() ||
      document_->index_at(cursor) != token_start_ ||
      document_->src()[token_start_] != '[') {
    return core::JsonErrc::InvalidJson;
  }
  ArrayIter iter;
  iter.document_ = document_;
  // FRESH-CHILD framing: the first child (if any) starts after the opener
  // and is framed at the next * / ++. An immediate closer frames an EMPTY
  // array (no children, done); EOF leaves a pending framing failure.
  const core::size_t child_ts =
      skip_ws(document_->src(), document_->src_size(), token_start_ + 1);
  const core::size_t j =
      first_index_at_or_after(*document_, cursor + 1, child_ts);
  iter.index_cursor_ = j;
  if (child_ts >= document_->src_size()) {
    iter.token_start_ = child_ts; // `[ <eof` : pending failure at ++
    return iter;
  }
  if (j < document_->index_count() && document_->index_at(j) == child_ts &&
      document_->src()[child_ts] == ']') {
    iter.done_ = true; // `[]` : the closer itself frames the empty array
    return iter;
  }
  iter.token_start_ = child_ts;
  return iter;
}

core::Result<ObjectIter, core::JsonErrc> Value::begin_object() const NOEXCEPT {
  if (document_ == nullptr ||
      document_->classify(token_start_) !=
          static_cast<core::uint8_t>(NodeType::Object)) {
    return core::JsonErrc::InvalidJson;
  }
  const core::size_t cursor =
      first_index_at_or_after(*document_, index_cursor_, token_start_);
  if (cursor >= document_->index_count() ||
      document_->index_at(cursor) != token_start_ ||
      document_->src()[token_start_] != '{') {
    return core::JsonErrc::InvalidJson;
  }
  ObjectIter iter;
  iter.document_ = document_;
  // FRESH-KEY framing: the first key (if any) starts after the opener; the
  // first index at-or-after it is the ':' that must follow. An immediate
  // closer frames an EMPTY object (no fields, done).
  const core::size_t key_ts =
      skip_ws(document_->src(), document_->src_size(), token_start_ + 1);
  const core::size_t j =
      first_index_at_or_after(*document_, cursor + 1, key_ts);
  iter.index_cursor_ = j;
  if (key_ts >= document_->src_size()) {
    iter.key_start_ = key_ts; // `{ <eof` : pending failure at *
    return iter;
  }
  if (j < document_->index_count() && document_->index_at(j) == key_ts &&
      document_->src()[key_ts] == '}') {
    iter.done_ = true; // `{}` : the closer itself frames the empty object
    return iter;
  }
  iter.key_start_ = key_ts;
  return iter;
}

core::Result<core::size_t, core::JsonErrc>
Value::count_elements() const NOEXCEPT {
  if (document_ == nullptr) {
    return core::JsonErrc::InvalidJson;
  }
  const core::uint8_t kind = document_->classify(token_start_);
  if (kind == static_cast<core::uint8_t>(NodeType::Array)) {
    const auto iter_result = begin_array();
    if (!iter_result) {
      return iter_result.error();
    }
    ArrayIter iter = iter_result.value();
    core::size_t count = 0;
    while (iter.operator bool()) {
      // '*' frames the current child (launcher-level); ++ frames its
      // delimiter. Grammar-level only — no value materialization.
      const auto &element = iter.operator*();
      if (!element) {
        return element.error();
      }
      ++iter;
      if (iter.failed_ != core::JsonErrc::None) {
        return iter.failed_;
      }
      ++count;
    }
    return count;
  }
  if (kind == static_cast<core::uint8_t>(NodeType::Object)) {
    const auto iter_result = begin_object();
    if (!iter_result) {
      return iter_result.error();
    }
    ObjectIter iter = iter_result.value();
    core::size_t count = 0;
    while (iter.operator bool()) {
      // Object advance reads field framing produced at operator* —
      // materialize the framing only (key scan), never the value body.
      const auto field = iter.operator*();
      if (!field) {
        return field.error();
      }
      ++iter;
      if (iter.failed_ != core::JsonErrc::None) {
        return iter.failed_;
      }
      ++count;
    }
    return count;
  }
  return core::JsonErrc::InvalidJson;
}

// === ArrayIter ============================================================

core::Result<Value, core::JsonErrc> ArrayIter::operator*() const NOEXCEPT {
  if (failed_ != core::JsonErrc::None) {
    return failed_;
  }
  if (done_ || document_ == nullptr) {
    return core::JsonErrc::InvalidJson; // deref past end
  }
  if (token_start_ >= document_->src_size() ||
      document_->classify(token_start_) == 0) {
    return core::JsonErrc::InvalidJson; // e.g. the byte after ',' is garbage
  }
  Value value;
  value.document_ = document_;
  value.token_start_ = token_start_;
  value.index_cursor_ = index_cursor_;
  return value;
}

ArrayIter &ArrayIter::operator++() NOEXCEPT {
  if (done_ || failed_ != core::JsonErrc::None) {
    return *this;
  }
  const Document &doc = *document_;
  const core::size_t ts = token_start_;
  const core::uint8_t kind = doc.classify(ts);
  if (kind == static_cast<core::uint8_t>(NodeType::Array) ||
      kind == static_cast<core::uint8_t>(NodeType::Object)) {
    // Container child: skip by INDEX ONLY — its contents stay unvalidated
    // (the on-demand latitude); the child's opener must own the cursor.
    if (index_cursor_ >= doc.index_count() ||
        doc.index_at(index_cursor_) != ts) {
      failed_ = core::JsonErrc::InvalidJson;
      return *this;
    }
    const core::size_t close = doc.index_close(index_cursor_);
    if (close == kNoBracket || close + 1 >= doc.index_count()) {
      failed_ = core::JsonErrc::InvalidJson; // unmatched / no delimiter after
      return *this;
    }
    const core::uint8_t b =
        static_cast<core::uint8_t>(doc.src()[doc.index_at(close + 1)]);
    if (b == ',') {
      const auto next = next_child_after_comma(doc, close + 1);
      if (!next) {
        failed_ = next.error();
        return *this;
      }
      token_start_ = next.value().token_start;
      index_cursor_ = next.value().index_cursor;
    } else if (b == ']') {
      done_ = true;
    } else {
      failed_ = core::JsonErrc::InvalidJson; // ':' / nested bracket stray
    }
    // Scalar child: validate the token, then frame the delimiter.
  } else if (kind != 0) {
    const auto len = frame_token_len(doc, ts);
    if (!len) {
      failed_ = len.error();
      return *this;
    }
    const auto delim = framing_delimiter(doc, ts + len.value(), index_cursor_);
    if (!delim) {
      failed_ = delim.error();
      return *this;
    }
    if (delim.value() == ',') {
      const auto next = next_child_after_comma(doc, index_cursor_);
      if (!next) {
        failed_ = next.error();
        return *this;
      }
      token_start_ = next.value().token_start;
      index_cursor_ = next.value().index_cursor;
    } else if (delim.value() == ']') {
      done_ = true;
    } else {
      failed_ = core::JsonErrc::InvalidJson;
    }
  } else {
    failed_ = core::JsonErrc::InvalidJson; // child is not a value at all
  }
  return *this;
}

// === ObjectIter ===========================================================

core::Result<Field, core::JsonErrc> ObjectIter::operator*() const NOEXCEPT {
  if (failed_ != core::JsonErrc::None) {
    return failed_;
  }
  if (done_ || document_ == nullptr) {
    return core::JsonErrc::InvalidJson;
  }
  const Document &doc = *document_;
  if (key_start_ >= doc.src_size() ||
      doc.classify(key_start_) !=
          static_cast<core::uint8_t>(NodeType::String)) {
    failed_ = core::JsonErrc::InvalidJson;
    return failed_;
  }
  const auto key = scan_string(doc.src() + key_start_,
                               doc.src_size() - key_start_);
  if (!key) {
    failed_ = key.error();
    return failed_;
  }
  const core::size_t key_end = key_start_ + key.value().total_len;
  const core::size_t colon_cursor =
      first_index_at_or_after(doc, index_cursor_, key_end);
  if (colon_cursor >= doc.index_count()) {
    failed_ = core::JsonErrc::InvalidJson;
    return failed_;
  }
  const core::size_t colon_pos = doc.index_at(colon_cursor);
  if (colon_pos < key_end || !bytes_are_ws(doc, key_end, colon_pos) ||
      doc.src()[colon_pos] != ':') {
    failed_ = core::JsonErrc::InvalidJson; // {"a" 1} / {"a" x: 1}
    return failed_;
  }
  const core::size_t vts = skip_ws(doc.src(), doc.src_size(), colon_pos + 1);
  if (vts >= doc.src_size() || doc.classify(vts) == 0) {
    failed_ = core::JsonErrc::InvalidJson; // missing value
    return failed_;
  }
  const core::size_t vcursor = first_index_at_or_after(
      doc, colon_cursor + 1, vts);
  // Mutable framing state (forward-only contract): operator* is the only
  // step of object iteration that learns where the current value starts;
  // the cursor moves for the following operator++.
  index_cursor_ = vcursor;
  value_start_ = vts;

  Field field;
  field.key = StringSpan{doc.src() + key_start_, key.value().total_len,
                         key.value().escape_count != 0};
  field.value.document_ = document_;
  field.value.token_start_ = vts;
  field.value.index_cursor_ = vcursor;
  return field;
}

ObjectIter &ObjectIter::operator++() NOEXCEPT {
  if (done_ || failed_ != core::JsonErrc::None) {
    return *this;
  }
  const Document &doc = *document_;
  const core::size_t vts = value_start_;
  const core::uint8_t kind = doc.classify(vts);
  if (kind == static_cast<core::uint8_t>(NodeType::Array) ||
      kind == static_cast<core::uint8_t>(NodeType::Object)) {
    // Container value: skip by index only (unvisited contents stay raw).
    if (index_cursor_ >= doc.index_count() ||
        doc.index_at(index_cursor_) != vts) {
      failed_ = core::JsonErrc::InvalidJson;
      return *this;
    }
    const core::size_t close = doc.index_close(index_cursor_);
    if (close == kNoBracket || close + 1 >= doc.index_count()) {
      failed_ = core::JsonErrc::InvalidJson;
      return *this;
    }
    const core::uint8_t b =
        static_cast<core::uint8_t>(doc.src()[doc.index_at(close + 1)]);
    if (b == ',') {
      const auto next = next_key_after_comma(doc, close + 1);
      if (!next) {
        failed_ = next.error();
        return *this;
      }
      key_start_ = next.value().token_start;
      index_cursor_ = next.value().index_cursor;
    } else if (b == '}') {
      done_ = true;
    } else {
      failed_ = core::JsonErrc::InvalidJson;
    }
    return *this;
  }
  if (kind == 0) {
    failed_ = core::JsonErrc::InvalidJson;
    return *this;
  }
  // Scalar value: validate the token, then frame the ',' / '}' boundary.
  const auto len = frame_token_len(doc, vts);
  if (!len) {
    failed_ = len.error();
    return *this;
  }
  const auto delim = framing_delimiter(doc, vts + len.value(), index_cursor_);
  if (!delim) {
    failed_ = delim.error();
    return *this;
  }
  if (delim.value() == ',') {
    const auto next = next_key_after_comma(doc, index_cursor_);
    if (!next) {
      failed_ = next.error();
      return *this;
    }
    key_start_ = next.value().token_start;
    index_cursor_ = next.value().index_cursor;
  } else if (delim.value() == '}') {
    done_ = true;
  } else {
    failed_ = core::JsonErrc::InvalidJson;
  }
  return *this;
}

} // namespace zuu::ondemand
