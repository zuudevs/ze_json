#pragma once

#include <ze_json/core/error.hpp>
#include <ze_json/core/integral.hpp>

namespace zuu::abi {

/**
 * @brief C-ABI contract shared by every `ze_*` export (wasm64 binding, v2).
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
 */
inline constexpr core::uint64_t kAbiVersion = 2;

inline constexpr core::uint32_t kErrorShift = 32;

[[nodiscard]] constexpr core::uint64_t pack_ok(core::size_t count) noexcept {
  return static_cast<core::uint64_t>(count);
}

[[nodiscard]] constexpr core::uint64_t
pack_error(core::JsonErrc error) noexcept {
  return static_cast<core::uint64_t>(static_cast<core::uint32_t>(error))
         << kErrorShift;
}

} // namespace zuu::abi
