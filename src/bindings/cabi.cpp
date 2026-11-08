// C-ABI surface for the wasm64 binding (TASKS P3-1).
//
// On wasm targets every function is exported via EXPORT_NAME; on native
// targets the whole translation unit compiles out so the static library
// stays freestanding-clean (no reference to linker-generated symbols).
//
// ABI contract (mirrored by bindings/js/ze_json.mjs):
//   ze_abi_version() -> 1
//   ze_alloc(size)   -> pointer (16-byte aligned bump allocator starting at
//                       the linker-defined __heap_base; no free — reset with
//                       ze_reset_heap between corpora)
//   ze_reset_heap()  -> void
//   ze_scanner8(src, src_size, dst, dst_cap)      -> u64 packed result
//   ze_reference_scan(src, src_size, dst, dst_cap) -> u64 packed result
//       packed result = ok    : count (high 32 bits are zero)
//                       error : (JsonErrc_code << 32)
//       dst entries are core::size_t (8 bytes on wasm64).

#include <ze_json/core/error.hpp>
#include <ze_json/core/integral.hpp>
#include <ze_json/core/macro.hpp>
#include <ze_json/ondemand/reference.hpp>
#include <ze_json/ondemand/scanner.hpp>

#if defined(__wasm__)

namespace zuu {

namespace {

constexpr core::uint64_t kErrorShift = 32;

core::uint64_t pack_ok(core::size_t count) {
  return static_cast<core::uint64_t>(count);
}

core::uint64_t pack_error(core::JsonErrc error) {
  return static_cast<core::uint64_t>(static_cast<core::uint32_t>(error)) << kErrorShift;
}

} // namespace

extern "C" {

// wasm-ld synthetic global: first byte after the (static) data section.
extern core::uint8_t __heap_base[];

static core::uint8_t *heap_cursor = nullptr;

EXPORT_NAME(ze_abi_version)
core::uint64_t ze_abi_version() { return 1; }

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
  return result.has_value() ? pack_ok(result.value()) : pack_error(result.error());
}

EXPORT_NAME(ze_reference_scan)
core::uint64_t ze_reference_scan(core::uint64_t src, core::uint64_t src_size,
                                 core::uint64_t dst, core::uint64_t dst_cap) {
  const auto result = ondemand::reference_scan(
      reinterpret_cast<const char *>(static_cast<core::uintptr_t>(src)),
      static_cast<core::size_t>(src_size),
      reinterpret_cast<core::size_t *>(static_cast<core::uintptr_t>(dst)),
      static_cast<core::size_t>(dst_cap));
  return result.has_value() ? pack_ok(result.value()) : pack_error(result.error());
}

} // extern "C"

} // namespace zuu

#endif // defined(__wasm__)
