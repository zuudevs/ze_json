// C-ABI surface for the wasm64 binding (TASKS P3-1; layout: TASKS R1).
//
// On wasm targets every function is exported via EXPORT_NAME; on native
// targets the whole translation unit compiles out so the static library
// stays freestanding-clean (no reference to linker-generated symbols).
//
// ABI contract: `include/ze_json/abi/cabi.hpp` (single source of truth,
// mirrored by bindings/js/ze_json.mjs).
//
// v3 (TASKS P7-5) adds the on-demand parser surface on a static parser
// slot pool: one Arena + one Document placed per slot, arena working bytes
// carved from the shared bump heap so ze_reset_heap stays the single
// rewind point between corpora (it closes every slot first). Traversal
// exports rebuild the exact C++ iterator from the (cursor, token_start)
// frame pair the caller feeds back and drive the REAL on-demand operators
// — no framing logic is duplicated on the ABI side.

#include <ze_json/abi/cabi.hpp>
#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/ondemand/document.hpp>
#include <ze_json/ondemand/reference.hpp>
#include <ze_json/ondemand/scanner.hpp>
#include <ze_json/ondemand/string.hpp> // unescape_string (ze_get_string)
#include <ze_json/utils/alloc.hpp> // placement-new (freestanding-safe guard)
#include <ze_json/utils/move.hpp> // utils::move (no std:: in freestanding core)

#if defined(__wasm__)

namespace zuu {

namespace {

/**
 * @brief One pooled parser: Arena + Document placed into static storage.
 *
 * Freestanding-safe (no malloc): both C++ objects live in bss slots, and
 * the arena's working bytes are carved from the shared bump heap (see
 * bump_aligned), so `ze_reset_heap` is the single lifetime rewind. `alive`
 * guards every subsequent export against double-close / use-after-close.
 */
struct ParserSlot {
  alignas(8) unsigned char arena_stor[sizeof(ondemand::Arena)];
  alignas(8) unsigned char doc_stor[sizeof(ondemand::Document)];
  bool alive = false;
};

static ParserSlot parser_slots[abi::kParserSlotMax];

//! The live slot for `slot_id`, or nullptr for an id out of range / closed.
[[nodiscard]] ParserSlot *slot_at(core::uint64_t slot_id) {
  if (slot_id >= abi::kParserSlotMax) {
    return nullptr;
  }
  ParserSlot &slot = parser_slots[static_cast<core::size_t>(slot_id)];
  return slot.alive ? &slot : nullptr;
}

[[nodiscard]] const ondemand::Document *
slot_document(core::uint64_t slot_id) {
  const ParserSlot *slot = slot_at(slot_id);
  return slot ? reinterpret_cast<const ondemand::Document *>(slot->doc_stor)
              : nullptr;
}

//! Reconstruct a Value frame on `doc`. `cursor == 0` re-derives the
//! structural-index position by forward search (always correct); step
//! results hand back the exact one, which begin_*/operator++ re-uses in
//! O(1). The raw-field publication of Value (P7-4) is what makes these
//! stateless foreign re-entries faithful replays of the C++ walk.
[[nodiscard]] ondemand::Value frame_value(const ondemand::Document *doc,
                                          core::uint64_t token_start,
                                          core::uint64_t cursor) {
  ondemand::Value value;
  value.document_ = doc;
  value.token_start_ = static_cast<core::size_t>(token_start);
  value.index_cursor_ = static_cast<core::size_t>(cursor);
  return value;
}

} // namespace

extern "C" {

// wasm-ld synthetic global: first byte after the (static) data section.
extern core::uint8_t __heap_base[];

static core::uint8_t *heap_cursor = nullptr;

//! 16-byte aligned bump allocation from linear memory's static tail. There
//! is no free: allocations live until `ze_reset_heap` (which also closes
//! every parser slot — see the v3 pool below). The wasm64 measurement
//! memory classes (documents <= 1 MiB) never exhaust the 64 MiB initial
//! memory; growth is NOT synthesized here (freestanding, no grow call).
[[nodiscard]] static void *bump_aligned(core::uint64_t size) {
  if (heap_cursor == nullptr) {
    heap_cursor = __heap_base;
  }
  const core::uint64_t aligned =
      (size + 15u) & ~static_cast<core::uint64_t>(15u);
  void *ptr = heap_cursor;
  heap_cursor += aligned;
  return ptr;
}

EXPORT_NAME(ze_abi_version)
core::uint64_t ze_abi_version() { return abi::kAbiVersion; }

//! v3 parser export; defined in the parser section below. ze_reset_heap
//! closes slots in the same step it rewinds (documents share the bump heap).
core::uint64_t ze_parser_close(core::uint64_t slot);

EXPORT_NAME(ze_alloc)
core::uint64_t ze_alloc(core::uint64_t size) {
  return reinterpret_cast<core::uint64_t>(bump_aligned(size));
}

EXPORT_NAME(ze_reset_heap)
void ze_reset_heap() {
  // Close parser slots FIRST: documents reference stale bump-heap cores
  // after the rewind, so `alive` must be cleared in the same step.
  for (core::uint32_t i = 0; i < abi::kParserSlotMax; ++i) {
    ze_parser_close(i);
  }
  heap_cursor = __heap_base;
}

EXPORT_NAME(ze_scanner8)
core::uint64_t ze_scanner8(core::uint64_t src, core::uint64_t src_size,
                           core::uint64_t dst, core::uint64_t dst_cap) {
  const auto result = ondemand::scanner8(
      reinterpret_cast<const char *>(static_cast<core::uintptr_t>(src)),
      static_cast<core::size_t>(src_size),
      reinterpret_cast<core::size_t *>(static_cast<core::uintptr_t>(dst)),
      static_cast<core::size_t>(dst_cap));
  return result.has_value() ? abi::pack_ok(result.value())
                            : abi::pack_error(result.error());
}

EXPORT_NAME(ze_reference_scan)
core::uint64_t ze_reference_scan(core::uint64_t src, core::uint64_t src_size,
                                 core::uint64_t dst, core::uint64_t dst_cap) {
  const auto result = ondemand::reference_scan(
      reinterpret_cast<const char *>(static_cast<core::uintptr_t>(src)),
      static_cast<core::size_t>(src_size),
      reinterpret_cast<core::size_t *>(static_cast<core::uintptr_t>(dst)),
      static_cast<core::size_t>(dst_cap));
  return result.has_value() ? abi::pack_ok(result.value())
                            : abi::pack_error(result.error());
}

#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1

EXPORT_NAME(ze_scanner64)
core::uint64_t ze_scanner64(core::uint64_t src, core::uint64_t src_size,
                            core::uint64_t dst, core::uint64_t dst_cap) {
  const auto result = ondemand::scanner64(
      reinterpret_cast<const char *>(static_cast<core::uintptr_t>(src)),
      static_cast<core::size_t>(src_size),
      reinterpret_cast<core::size_t *>(static_cast<core::uintptr_t>(dst)),
      static_cast<core::size_t>(dst_cap));
  return result.has_value() ? abi::pack_ok(result.value())
                            : abi::pack_error(result.error());
}

#endif // little-endian 64-bit (scanner64 availability)

#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1 &&                            \
    (defined(__SSE2__) || defined(__wasm__))

EXPORT_NAME(ze_scanner128)
core::uint64_t ze_scanner128(core::uint64_t src, core::uint64_t src_size,
                             core::uint64_t dst, core::uint64_t dst_cap) {
  const auto result = ondemand::scanner128(
      reinterpret_cast<const char *>(static_cast<core::uintptr_t>(src)),
      static_cast<core::size_t>(src_size),
      reinterpret_cast<core::size_t *>(static_cast<core::uintptr_t>(dst)),
      static_cast<core::size_t>(dst_cap));
  return result.has_value() ? abi::pack_ok(result.value())
                            : abi::pack_error(result.error());
}

#endif // little-endian 64-bit + (SSE2 || wasm-simd128)

EXPORT_NAME(ze_structural_scan)
core::uint64_t ze_structural_scan(core::uint64_t src, core::uint64_t src_size,
                                  core::uint64_t dst, core::uint64_t dst_cap) {
  const auto result = ondemand::structural_scan(
      reinterpret_cast<const char *>(static_cast<core::uintptr_t>(src)),
      static_cast<core::size_t>(src_size),
      reinterpret_cast<core::size_t *>(static_cast<core::uintptr_t>(dst)),
      static_cast<core::size_t>(dst_cap));
  return result.has_value() ? abi::pack_ok(result.value())
                            : abi::pack_error(result.error());
}

// === v3 on-demand parser surface (P7-5) ====================================
// Contract: `include/ze_json/abi/cabi.hpp` (packing families, done
// sentinel, slot pool). Traversal exports REPLAY the real on-demand
// operators on a reconstructed iterator — the (cursor, token_start) frame
// pair a result hands back is fed forward by the foreign consumer, keeping
// the walk forward-only and stateless per call.

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
EXPORT_NAME(ze_parser_open)
core::uint64_t ze_parser_open(core::uint64_t src, core::uint64_t src_size) {
  ParserSlot *slot = nullptr;
  core::uint64_t slot_id = 0;
  for (core::uint64_t i = 0; i < abi::kParserSlotMax; ++i) {
    if (!parser_slots[static_cast<core::size_t>(i)].alive) {
      ParserSlot &candidate = parser_slots[static_cast<core::size_t>(i)];
      slot = &candidate;
      slot_id = i;
      break;
    }
  }
  if (slot == nullptr) {
    return abi::pack_error(core::JsonErrc::InsufficientBuffer); // pool full
  }

  // Exact upper bound of Document::open's arena footprint: three blocks of
  // src_size * sizeof(size_t) (index entries, match table, pairing stack)
  // plus per-block alignment waste and a headroom pad.
  const core::uint64_t arena_bytes =
      src_size * sizeof(core::size_t) * 3 + 64;
  void *arena_buf = bump_aligned(arena_bytes);
  auto *arena = ::new (static_cast<void *>(slot->arena_stor))
      ondemand::Arena(arena_buf, static_cast<core::size_t>(arena_bytes));

  auto opened = ondemand::Document::open(
      *arena,
      reinterpret_cast<const char *>(static_cast<core::uintptr_t>(src)),
      static_cast<core::size_t>(src_size));
  if (!opened) {
    // The slot returns to the pool; the carved bump-heap bytes stay
    // committed until ze_reset_heap (no individual free, no grow call).
    arena->~Arena();
    return abi::pack_error(opened.error());
  }
  ::new (static_cast<void *>(slot->doc_stor))
      ondemand::Document(utils::move(opened.value()));
  slot->alive = true;
  return slot_id; // plain handle: kParserSlotMax <= 2^32 keeps this exact
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
EXPORT_NAME(ze_parser_root)
core::uint64_t ze_parser_root(core::uint64_t slot) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return abi::pack_error(core::JsonErrc::InvalidJson);
  }
  const auto root = doc->get_root();
  if (root.has_error()) {
    return abi::pack_error(root.error());
  }
  return static_cast<core::uint64_t>(root.value().token_start()); // plain
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
EXPORT_NAME(ze_container_begin)
core::uint64_t ze_container_begin(core::uint64_t slot,
                                  core::uint64_t container_ts,
                                  core::uint64_t container_cursor) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return abi::pack_error(core::JsonErrc::InvalidJson);
  }
  const ondemand::Value container =
      frame_value(doc, container_ts, container_cursor);
  const core::uint8_t kind = doc->classify(container.token_start_);
  if (kind == static_cast<core::uint8_t>(ondemand::NodeType::Array)) {
    const auto begin = container.begin_array();
    if (!begin) {
      return abi::pack_error(begin.error());
    }
    if (begin.value().done_) {
      return abi::pack_step_done(); // `[]`: framed empty, nothing to deref
    }
    return abi::pack_step(begin.value().index_cursor_,
                          begin.value().token_start_);
  }
  if (kind == static_cast<core::uint8_t>(ondemand::NodeType::Object)) {
    const auto begin = container.begin_object();
    if (!begin) {
      return abi::pack_error(begin.error());
    }
    if (begin.value().done_) {
      return abi::pack_step_done(); // `{}`: framed empty
    }
    return abi::pack_step(begin.value().index_cursor_,
                          begin.value().key_start_); // next key frame
  }
  return abi::pack_error(core::JsonErrc::InvalidJson); // not a container
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
EXPORT_NAME(ze_array_next)
core::uint64_t ze_array_next(core::uint64_t slot, core::uint64_t child_ts,
                             core::uint64_t child_cursor) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return abi::pack_error(core::JsonErrc::InvalidJson);
  }
  ondemand::ArrayIter iter;
  iter.document_ = doc;
  iter.token_start_ = static_cast<core::size_t>(child_ts);
  iter.index_cursor_ = static_cast<core::size_t>(child_cursor);
  ++iter; // the real ArrayIter operator++ — framing + delimiter logic
  if (iter.failed_ != core::JsonErrc::None) {
    return abi::pack_error(iter.failed_);
  }
  if (iter.done_) {
    return abi::pack_step_done();
  }
  return abi::pack_step(iter.index_cursor_, iter.token_start_);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
EXPORT_NAME(ze_object_next)
core::uint64_t ze_object_next(core::uint64_t slot, core::uint64_t value_ts,
                              core::uint64_t value_cursor) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return abi::pack_error(core::JsonErrc::InvalidJson);
  }
  ondemand::ObjectIter iter;
  iter.document_ = doc;
  iter.value_start_ = static_cast<core::size_t>(value_ts);
  iter.index_cursor_ = static_cast<core::size_t>(value_cursor);
  ++iter; // the real ObjectIter operator++ — value framing + ',' / '}'
  if (iter.failed_ != core::JsonErrc::None) {
    return abi::pack_error(iter.failed_);
  }
  if (iter.done_) {
    return abi::pack_step_done();
  }
  return abi::pack_step(iter.index_cursor_,
                        iter.key_start_); // next key frame
}

EXPORT_NAME(ze_object_field)
core::uint64_t ze_object_field(core::uint64_t slot, core::uint64_t key_ts,
                               core::uint64_t key_cursor) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return abi::pack_error(core::JsonErrc::InvalidJson);
  }
  ondemand::ObjectIter iter;
  iter.document_ = doc;
  iter.key_start_ = static_cast<core::size_t>(key_ts);
  iter.index_cursor_ = static_cast<core::size_t>(key_cursor);
  // The real ObjectIter operator*: scans the key token, frames the ':' and
  // the value's launcher, and moves the cursor for the following advance
  // (the forward-only mutable-state contract of P7-4 — discarded here; the
  // consumer replays through ze_object_next with THIS value's frame).
  const auto field = *iter;
  if (field.has_error()) {
    return abi::pack_error(field.error());
  }
  const ondemand::Value &value = field.value().value;
  return abi::pack_step(value.index_cursor_, value.token_start_);
}

EXPORT_NAME(ze_value_kind)
core::uint64_t ze_value_kind(core::uint64_t slot, core::uint64_t token_start) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return 0; // Unknown: dead slot classifies nothing (no error channel)
  }
  return static_cast<core::uint64_t>(
      doc->classify(static_cast<core::size_t>(token_start)));
}

EXPORT_NAME(ze_get_string)
core::uint64_t ze_get_string(core::uint64_t slot, core::uint64_t token_start,
                             core::uint64_t dst, core::uint64_t dst_cap) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return abi::pack_error(core::JsonErrc::InvalidJson);
  }
  const ondemand::Value value = frame_value(doc, token_start, 0);
  const auto span = value.get_string_span(); // grammar + UTF-8 gate (P7-3)
  if (!span) {
    return abi::pack_error(span.error());
  }
  const auto decoded = ondemand::unescape_string(
      span.value().start, span.value().total_len,
      reinterpret_cast<char *>(static_cast<core::uintptr_t>(dst)),
      static_cast<core::size_t>(dst_cap));
  if (!decoded) {
    return abi::pack_error(decoded.error());
  }
  return static_cast<core::uint64_t>(decoded.value()); // bytes, high zero
}

EXPORT_NAME(ze_get_number)
core::uint64_t ze_get_number(core::uint64_t slot, core::uint64_t token_start,
                             core::uint64_t dst, core::uint64_t dst_cap) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return abi::pack_error(core::JsonErrc::InvalidJson);
  }
  if (dst_cap < sizeof(double)) {
    return abi::pack_error(core::JsonErrc::InsufficientBuffer);
  }
  static_assert(sizeof(double) == 8, "f64 travel assumes 64-bit doubles");
  const ondemand::Value value = frame_value(doc, token_start, 0);
  const auto number = value.get_double(); // V8-locked semantics (P7-2)
  if (!number) {
    return abi::pack_error(number.error());
  }
  const double parsed = number.value();
  __builtin_memcpy(
      reinterpret_cast<void *>(static_cast<core::uintptr_t>(dst)), &parsed,
      sizeof(double)); // LE bytes on wasm64; the return stays count-only
  return sizeof(double);
}

EXPORT_NAME(ze_get_bool)
core::uint64_t ze_get_bool(core::uint64_t slot, core::uint64_t token_start) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return abi::pack_error(core::JsonErrc::InvalidJson);
  }
  const ondemand::Value value = frame_value(doc, token_start, 0);
  const auto literal = value.get_bool();
  if (literal.has_error()) {
    return abi::pack_error(literal.error());
  }
  return literal.value() ? 1u : 0u; // ok payload in the low half
}

EXPORT_NAME(ze_is_null)
core::uint64_t ze_is_null(core::uint64_t slot, core::uint64_t token_start) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return 0; // plain classification: dead slot matches nothing
  }
  const ondemand::Value value = frame_value(doc, token_start, 0);
  return value.is_null() ? 1u : 0u;
}

EXPORT_NAME(ze_count_elements)
core::uint64_t ze_count_elements(core::uint64_t slot,
                                 core::uint64_t token_start) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return abi::pack_error(core::JsonErrc::InvalidJson);
  }
  // cursor 0 = re-derive by index walk; count framing is cursor-free in
  // value (begin_* re-searches from the cursor it is handed).
  const ondemand::Value value = frame_value(doc, token_start, 0);
  const auto count = value.count_elements();
  if (!count) {
    return abi::pack_error(count.error());
  }
  return static_cast<core::uint64_t>(count.value()); // v2-style: high zero
}

EXPORT_NAME(ze_parser_close)
core::uint64_t ze_parser_close(core::uint64_t slot) {
  // No-op on an unknown id, silent on double close; placement destruction
  // keeps the bss storage (the wasm binding owns nothing heap-side).
  ParserSlot *slot_ptr = slot_at(slot);
  if (slot_ptr != nullptr) {
    reinterpret_cast<ondemand::Arena *>(slot_ptr->arena_stor)->~Arena();
    reinterpret_cast<ondemand::Document *>(slot_ptr->doc_stor)->~Document();
    slot_ptr->alive = false;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// v4 thin step family (O1-7 — ADDITIVE; contract:
// include/ze_json/abi/cabi.hpp). The SAME core operators as the v3 step
// exports; the packed u64 return is replaced by a caller-owned u32 out
// pair (token_start, cursor — the exact v3 halves) plus an i32 status, so
// the JS side pays zero BigInt boxing per call (scalar args travel as i32
// numbers; only the out pointer is u64 and pre-boxed once by the binding).
// Status: 0 = ok, 1 = done, < 0 = error (-JsonErrc); `out` untouched on
// done/error. Sub-2-GiB ts/cursor bound comes with the i32 halves (stated
// v4 limitation; v3 remains the sub-4-GiB general path).
// ---------------------------------------------------------------------------

core::int32_t thin_status_ok(core::uint64_t out, core::size_t token_start,
                             core::size_t cursor) {
  auto *dst =
      reinterpret_cast<core::uint32_t *>(static_cast<core::uintptr_t>(out));
  dst[0] = static_cast<core::uint32_t>(token_start);
  dst[1] = static_cast<core::uint32_t>(cursor);
  return 0;
}

constexpr core::int32_t thin_status_done() noexcept { return 1; }

constexpr core::int32_t thin_status_error(core::JsonErrc error) noexcept {
  return -static_cast<core::int32_t>(error);
}

EXPORT_NAME(ze_container_begin4)
core::int32_t ze_container_begin4(core::uint32_t slot,
                                  core::uint32_t container_ts,
                                  core::uint32_t container_cursor,
                                  core::uint64_t out) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return thin_status_error(core::JsonErrc::InvalidJson);
  }
  const ondemand::Value container =
      frame_value(doc, container_ts, container_cursor);
  const core::uint8_t kind = doc->classify(container.token_start_);
  if (kind == static_cast<core::uint8_t>(ondemand::NodeType::Array)) {
    const auto begin = container.begin_array();
    if (!begin) {
      return thin_status_error(begin.error());
    }
    if (begin.value().done_) {
      return thin_status_done(); // `[]`: framed empty, nothing to deref
    }
    return thin_status_ok(out, begin.value().token_start_,
                          begin.value().index_cursor_);
  }
  if (kind == static_cast<core::uint8_t>(ondemand::NodeType::Object)) {
    const auto begin = container.begin_object();
    if (!begin) {
      return thin_status_error(begin.error());
    }
    if (begin.value().done_) {
      return thin_status_done(); // `{}`: framed empty
    }
    return thin_status_ok(out, begin.value().key_start_,
                          begin.value().index_cursor_); // next key frame
  }
  return thin_status_error(core::JsonErrc::InvalidJson); // not a container
}

EXPORT_NAME(ze_array_next4)
core::int32_t ze_array_next4(core::uint32_t slot, core::uint32_t child_ts,
                             core::uint32_t child_cursor, core::uint64_t out) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return thin_status_error(core::JsonErrc::InvalidJson);
  }
  ondemand::ArrayIter iter;
  iter.document_ = doc;
  iter.token_start_ = static_cast<core::size_t>(child_ts);
  iter.index_cursor_ = static_cast<core::size_t>(child_cursor);
  ++iter; // the real ArrayIter operator++ — framing + delimiter logic
  if (iter.failed_ != core::JsonErrc::None) {
    return thin_status_error(iter.failed_);
  }
  if (iter.done_) {
    return thin_status_done();
  }
  return thin_status_ok(out, iter.token_start_, iter.index_cursor_);
}

EXPORT_NAME(ze_object_next4)
core::int32_t ze_object_next4(core::uint32_t slot, core::uint32_t value_ts,
                              core::uint32_t value_cursor, core::uint64_t out) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return thin_status_error(core::JsonErrc::InvalidJson);
  }
  ondemand::ObjectIter iter;
  iter.document_ = doc;
  iter.value_start_ = static_cast<core::size_t>(value_ts);
  iter.index_cursor_ = static_cast<core::size_t>(value_cursor);
  ++iter; // the real ObjectIter operator++ — value framing + ',' / '}'
  if (iter.failed_ != core::JsonErrc::None) {
    return thin_status_error(iter.failed_);
  }
  if (iter.done_) {
    return thin_status_done();
  }
  return thin_status_ok(out, iter.key_start_,
                        iter.index_cursor_); // next key frame
}

EXPORT_NAME(ze_object_field4)
core::int32_t ze_object_field4(core::uint32_t slot, core::uint32_t key_ts,
                               core::uint32_t key_cursor, core::uint64_t out) {
  const ondemand::Document *doc = slot_document(slot);
  if (doc == nullptr) {
    return thin_status_error(core::JsonErrc::InvalidJson);
  }
  ondemand::ObjectIter iter;
  iter.document_ = doc;
  iter.key_start_ = static_cast<core::size_t>(key_ts);
  iter.index_cursor_ = static_cast<core::size_t>(key_cursor);
  // The real ObjectIter operator*: scans the key token, frames the ':' and
  // the value's launcher (identical to the v3 replay contract).
  const auto field = *iter;
  if (field.has_error()) {
    return thin_status_error(field.error());
  }
  const ondemand::Value &value = field.value().value;
  return thin_status_ok(out, value.token_start_, value.index_cursor_);
}

// ---------------------------------------------------------------------------
// v5 match-pass decomposition export (ND-13 — ADDITIVE; contract:
// include/ze_json/abi/cabi.hpp). The pairing pass Document::open runs in
// place, exposed standalone so a foreign harness can time the match
// sub-stage of ze_parser_open (s2-open) from its own clock — wasm64 is
// freestanding and carries no clock. The fused open stays the measured
// production bound; the standalone call adds one visible call boundary
// (the harness reports the s5 boundary floor beside it).
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
EXPORT_NAME(ze_match_pass)
core::uint64_t ze_match_pass(core::uint64_t src, core::uint64_t src_size,
                             core::uint64_t indexes,
                             core::uint64_t index_count,
                             core::uint64_t dst_match,
                             core::uint64_t dst_stack,
                             core::uint64_t dst_cap) {
  if (index_count == 0) {
    return 0; // nothing to pair: plain ok, zero closers
  }
  if (dst_cap < index_count * sizeof(core::size_t)) {
    return abi::pack_error(core::JsonErrc::InsufficientBuffer);
  }
  const auto paired = ondemand::match_pass(
      reinterpret_cast<const char *>(static_cast<core::uintptr_t>(src)),
      static_cast<core::size_t>(src_size),
      reinterpret_cast<const core::size_t *>(
          static_cast<core::uintptr_t>(indexes)),
      static_cast<core::size_t>(index_count),
      reinterpret_cast<core::size_t *>(static_cast<core::uintptr_t>(dst_match)),
      reinterpret_cast<core::size_t *>(static_cast<core::uintptr_t>(dst_stack)));
  if (!paired) {
    return abi::pack_error(paired.error());
  }
  return static_cast<core::uint64_t>(paired.value());
}


} // extern "C"

} // namespace zuu

#endif // defined(__wasm__)
