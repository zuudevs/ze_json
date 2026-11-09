// C-ABI surface for the wasm64 binding (TASKS P3-1; layout: TASKS R1).
//
// On wasm targets every function is exported via EXPORT_NAME; on native
// targets the whole translation unit compiles out so the static library
// stays freestanding-clean (no reference to linker-generated symbols).
//
// ABI contract: `include/ze_json/abi/cabi.hpp` (single source of truth,
// mirrored by bindings/js/ze_json.mjs).

#include <ze_json/abi/cabi.hpp>
#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/ondemand/reference.hpp>
#include <ze_json/ondemand/scanner.hpp>

#if defined(__wasm__)

namespace zuu {

extern "C" {

// wasm-ld synthetic global: first byte after the (static) data section.
extern core::uint8_t __heap_base[];

static core::uint8_t *heap_cursor = nullptr;

EXPORT_NAME(ze_abi_version)
core::uint64_t ze_abi_version() { return abi::kAbiVersion; }

EXPORT_NAME(ze_alloc)
core::uint64_t ze_alloc(core::uint64_t size) {
  if (heap_cursor == nullptr) {
    heap_cursor = __heap_base;
  }
  const core::uint64_t aligned = (size + 15u) & ~static_cast<core::uint64_t>(15u);
  const core::uint64_t ptr = reinterpret_cast<core::uint64_t>(heap_cursor);
  heap_cursor += aligned;
  return ptr;
}

EXPORT_NAME(ze_reset_heap)
void ze_reset_heap() { heap_cursor = __heap_base; }

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

#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1 \
	&& (defined(__SSE2__) || defined(__wasm__))

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

} // extern "C"

} // namespace zuu

#endif // defined(__wasm__)
