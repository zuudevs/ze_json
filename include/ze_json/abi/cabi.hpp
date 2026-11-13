#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/integral.hpp>

namespace zuu::abi {

/**
 * @brief C-ABI contract shared by every `ze_*` export (wasm64 binding, v3).
 *
 * This header is the single source of truth for the surface consumed by
 * `bindings/js/ze_json.mjs` (and any future foreign-function consumer).
 * Rules:
 *  - exports are append-only: new functions may be added between versions,
 *    but packing semantics or existing signatures never change silently;
 *  - every bump of kAbiVersion must be mirrored by `ABI_VERSION` in
 *    `bindings/js/ze_json.mjs` (the binding fails loud on mismatch);
 *  - linear memory is module-EXPORTED (memory64, 64 MiB initial) and
 *    pointers travel as u64 offsets into that memory.
 *
 * v2 export table (v1 + scanner-mode surface, TASKS R2):
 *   ze_abi_version() -> u64                         constant kAbiVersion
 *   ze_alloc(size) -> ptr                           bump allocator from
 *                       linker-defined __heap_base, 16-byte aligned, no
 *                       individual free (rewind with ze_reset_heap)
 *   ze_reset_heap() -> void
 *
 * Scanner-mode exports (all share the packed result; JS picks via
 * SCANNER_MODES in ze_json.mjs — the JS-visible scanner selection axis):
 *   ze_reference_scan(src, src_size, dst, dst_cap) -> u64   C++ scalar anchor
 *   ze_scanner8    (src, src_size, dst, dst_cap) -> u64     table-driven
 *                       (measured winner, P4-2 — the structural default)
 *   ze_scanner64   (src, src_size, dst, dst_cap) -> u64     SWAR (rejected on
 *                       native, kept for the comparison matrix)
 *   ze_scanner128  (src, src_size, dst, dst_cap) -> u64     128-bit SIMD
 *                       via core::simd_* (wasm-simd128 on this target)
 *   ze_structural_scan (src, src_size, dst, dst_cap) -> u64 the library's
 *                       static dispatch — production entry point
 *
 * Packed scan result (all scan exports):
 *   ok    : count (high 32 bits zero) — structural indices written to dst
 *   error : (JsonErrc code << 32)
 * `dst` entries are core::size_t (8 bytes on wasm64).
 *
 * v3 export table (v2 + on-demand parser surface, TASKS P7-5). Every call
 * operates on one parser SLOT of a static pool (kParserSlotMax entries,
 * one Arena + one Document placed per slot; slot = the handle). Traversal
 * continuation state is the (cursor, token_start) frame pair a step result
 * hands back — full-width u64 ARGUMENTS stay unpacked, so re-entry is
 * stateless: the exports rebuild the exact C++ iterator from the pair and
 * drive the real on-demand operators (no framing logic is duplicated).
 * @cite 10.1002_spe.3313 on-demand JSON (Keiser, Lemire) — the single
 *       forward iterator this C surface replays token by token.
 * @cite 10.14778_3115404.3115416 Mison — the structural-index walk that
 *       backs cursor re-entry without a materialized DOM.
 *
 *   ze_parser_open (src, src_size) -> u64      Document::open on the slot
 *                       (arena carve src_size*8*3+64 bytes from the bump
 *                       heap); ok => slot id in the low half. Fails loud
 *                       (InsufficientBuffer) when the pool is exhausted —
 *                       closing slots first is the caller's job.
 *   ze_parser_root (slot) -> u64               Document::get_root; ok =>
 *                       root token_start (plain, 0 is a valid root).
 *   ze_parser_close (slot) -> u64              destruct the slot; plain 0.
 *   ze_reset_heap () -> void                   v2 rewind EXTENDED: closes
 *                       every parser slot first (buffers share the bump
 *                       heap, so a reset invalidates open documents).
 *   ze_container_begin (slot, container_ts, container_cursor) -> u64
 *                       first child frame of the array/object at ts; ok =>
 *                       (cursor << 32) | child_ts — of an OBJECT the
 *                       "child" is the STRING KEY token (its cursor is
 *                       the ':' index); done (empty container) =>
 *                       kStepSentinel << 32.
 *   ze_array_next (slot, child_ts, child_cursor) -> u64
 *                       ArrayIter++ replay; same step packing.
 *   ze_object_next (slot, value_ts, value_cursor) -> u64
 *                       ObjectIter++ replay; ok => next key frame
 *                       (cursor << 32) | key_ts; done => kStepSentinel.
 *   ze_object_field (slot, key_ts, key_cursor) -> u64
 *                       ObjectIter* replay (the key itself is a string
 *                       token: read it back with ze_value_kind + 
 *                       ze_get_string); ok => (value_cursor << 32) |
 *                       value_ts — the same frame shape every consumer
 *                       feeds back into ze_container_begin /
 *                       ze_array_next.
 *   ze_value_kind (slot, token_start) -> u64   plain classification
 *                       kOndemandNodeType value 0..7; 0 = Unknown (also
 *                       for out-of-range ts — no error channel).
 *   ze_get_string  (slot, token_start, dst, dst_cap) -> u64
 *                       full P7-3 unescape into dst (V8-locked semantics:
 *                       lone-surrogate escapes pass through verbatim); ok
 *                       => decoded byte count (0 = valid empty string);
 *                       too-small cap => InsufficientBuffer, dst untouched.
 *   ze_get_number  (slot, token_start, dst, dst_cap) -> u64
 *                       V8-parity double (P7-2 parse_number) written to
 *                       dst as 8 little-endian bytes (dst_cap >= 8); ok =>
 *                       8. The bits do NOT travel in the packed return —
 *                       -0.0 and subnormal bit patterns would alias the
 *                       error-half-non-zero packing.
 *   ze_get_bool    (slot, token_start) -> u64  ok => literal check, low
 *                       half 0 (false) / 1 (true); non-literal ts =>
 *                       InvalidJson.
 *   ze_is_null     (slot, token_start) -> u64  plain 0/1 literal match
 *                       (no error channel — classification, not scan).
 *   ze_count_elements (slot, token_start) -> u64
 *                       Value::count_elements; ok => count (index-walk
 *                       framing only — the skip latitude is INTENTIONAL),
 *                       error => (JsonErrc << 32).
 *
 * v3 result packing:
 *   ok (scalar family: open/root/count/string/number/bool)
 *                       payload in the LOW 32 bits, high 32 bits zero;
 *   ok (step family: container_begin/array_next/object_next/object_field)
 *                       frame (cursor << 32) | token_start — BOTH halves
 *                       are always non-zero (every child/key/value sits
 *                       after its launcher byte, and a container owns at
 *                       least one index before any child), so the error
 *                       test `high != 0 && low == 0` stays exact;
 *   done (step family)  (kStepSentinel << 32)  — kStepSentinel = 0xffffffff
 *                       can never be a cursor for any sub-4-GiB document
 *                       (cursors are structural-index positions, bounded
 *                       by the source size);
 *   plain (kind/is_null/close)   the value itself;
 *   error (everything)  (JsonErrc code << 32) — matching v2.
 *
 * v4 export table (v3 + thin step family, O1-7 — ADDITIVE: every v3
 * export keeps its exact signature and packing; nothing is removed or
 * re-packed). The thin family removes the per-call BigInt boxing on the
 * JS side: scalar arguments pass as i32 JS numbers (no `BigInt(...)` per
 * call), the step result travels through a caller-owned 8-byte out pair
 * instead of a packed u64 return, and the status is a plain i32. The out
 * POINTER is the only u64 argument and is pre-boxed ONCE by the binding.
 * @cite 10.1145_3744916.3764533 — per-call marshalling overhead dominates
 *       wasm-in-Node integrations at KB-scale payloads (ND-8/ND-9: the
 *       count path's remaining cost sits at the call boundary shape).
 *
 *   ze_container_begin4 (u32 slot, u32 container_ts, u32 container_cursor,
 *                        u64 out) -> i32
 *   ze_array_next4      (u32 slot, u32 child_ts, u32 child_cursor,
 *                        u64 out) -> i32
 *   ze_object_next4     (u32 slot, u32 value_ts, u32 value_cursor,
 *                        u64 out) -> i32
 *   ze_object_field4    (u32 slot, u32 key_ts, u32 key_cursor,
 *                        u64 out) -> i32
 *   Status: 0 = ok (out[0] = token_start, out[1] = cursor, two
 *                       little-endian u32 — the exact halves v3 packs as
 *                       (cursor << 32) | token_start);
 *           1 = done (step family exhaustion; `out` untouched);
 *           < 0 = error (-JsonErrc code; `out` untouched).
 *   `out` semantics: 8 bytes, 4-byte alignment, caller-owned scratch
 *                       (the binding re-carves it after each heap rewind).
 *   Stated limitation: i32 halves bound ts/cursor to sub-2-GiB documents
 *                       (SAQ workload is KB-scale; the v3 surface remains
 *                       the sub-4-GiB general path).
 */
inline constexpr core::uint64_t kAbiVersion = 4;

inline constexpr core::uint32_t kErrorShift = 32;

/** Static parser-slot pool size (slot ids are 0-based arrays into it). */
inline constexpr core::uint32_t kParserSlotMax = 64;

/** Step-family exhaustion sentinel (carried in the high 32 bits). */
inline constexpr core::uint32_t kStepSentinel = 0xffffffffU;

[[nodiscard]] constexpr core::uint64_t pack_ok(core::size_t count) noexcept {
  return static_cast<core::uint64_t>(count);
}

[[nodiscard]] constexpr core::uint64_t
pack_error(core::JsonErrc error) noexcept {
  return static_cast<core::uint64_t>(static_cast<core::uint32_t>(error))
         << kErrorShift;
}

/** Step-family ok frame: `cursor` and `token_start` BOTH in a u32 half. */
[[nodiscard]] constexpr core::uint64_t
pack_step(core::size_t cursor, core::size_t token_start) noexcept {
  return (static_cast<core::uint64_t>(cursor) << kErrorShift) |
         static_cast<core::uint64_t>(token_start);
}

[[nodiscard]] constexpr core::uint64_t pack_step_done() noexcept {
  return static_cast<core::uint64_t>(kStepSentinel) << kErrorShift;
}

} // namespace zuu::abi
