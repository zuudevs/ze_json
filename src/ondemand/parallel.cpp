#include <ze_json/core/lookups.hpp>
#include <ze_json/ondemand/scanner.hpp>
#include <ze_json/utils/swar.hpp>
#include <ze_json/utils/utf8_validator.hpp>

namespace zuu::ondemand {

#if __LITTLE_ENDIAN__ == 1

#if __IS_64_BIT__ == 1

core::Result<core::size_t, core::JsonErrc>
scanner64(const char *const RESTRICT src, core::size_t src_size,
          core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT {
  // TODO
}

#elif __IS_64_BIT__ == 0

core::Result<core::size_t, core::JsonErrc>
scanner32(const char *const RESTRICT src, core::size_t src_size,
          core::size_t *RESTRICT dst, core::size_t dst_size) NOEXCEPT {
  // TODO
}
#endif // __IS_64_BIT__ == 0
#endif // __LITTLE_ENDIAN__ == 1

} // namespace zuu::ondemand
