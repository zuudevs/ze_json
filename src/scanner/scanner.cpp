#include <ze_json/scanner/scanner.hpp>
#include <ze_json/utils/swar.hpp>
#include <ze_json/utils/utf8_validator.hpp>

namespace zuu::scanner {

types::Result<types::size_t, core::JsonErrc>
scanner8(
    const char* const RESTRICT src, types::size_t src_size,
    types::size_t* RESTRICT dst, types::size_t dst_size) NOEXCEPT {
    if (!src || src_size <= 1) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }

    if (!dst || dst_size <= 1) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InsufficientBuffer};
    }

    if (!utils::Utf8Validator(src, src_size)) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }

    auto ptr              = src;
    const auto end        = src + src_size;
    auto cursor           = dst;
    const auto end_cursor = dst + dst_size;

    while (ptr < end && cursor < end_cursor) {
        while (ptr < end &&
               (lookups::kStructural[static_cast<types::uint8_t>(*ptr)]) == 0) {
            ptr++;
        }

        if (ptr >= end) {
            break;
        }

        const auto flags =
            lookups::kStructural[static_cast<types::uint8_t>(*ptr)];

        if (flags & lookups::SCAN_NORMAL) {
            *cursor++ = static_cast<types::size_t>(ptr - src);
            ptr++;
        } else if (flags & lookups::SCAN_QUOTE) {
            *cursor++ = static_cast<types::size_t>(ptr - src);
            ptr++;

            while (true) { // no has evaluation
                if (ptr >= end) {
                    return types::Unexpected{core::JsonErrc::UnexpectedQuote};
                }
                if (*ptr == '\\') {
                    if (ptr + 1 >= end) {
                        return types::Unexpected{
                            core::JsonErrc::UnexpectedQuote};
                    }
                    ptr += 2;
                    continue;
                }

                if (*ptr == '\"') {
                    break;
                }
                ptr++;
            }

            if (cursor == end_cursor) {
                return types::Unexpected{core::JsonErrc::InsufficientBuffer};
            }

            *cursor++ = static_cast<types::size_t>(ptr - src);
            ptr++;
        }
    }

    return static_cast<types::size_t>(cursor - dst);
}
#if __LITTLE_ENDIAN__ == 1
#if __IS_64_BIT__ == 1
types::Result<types::size_t, core::JsonErrc>
scanner64(
    const char* const RESTRICT src, types::size_t src_size,
    types::size_t* RESTRICT dst, types::size_t dst_size) NOEXCEPT {
    if (!src || src_size <= 1) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }

    if (!dst || dst_size <= 1) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InsufficientBuffer};
    }

    if (!utils::Utf8Validator(src, src_size)) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }

    auto ptr              = src;
    const auto end        = src + src_size;
    auto cursor           = dst;
    const auto end_cursor = dst + dst_size;

    while (ptr < end && cursor < end_cursor) {
        while (ptr < end &&
               lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
            ptr++;
        }

        if (ptr >= end) {
            break;
        }

        const auto flags =
            lookups::kStructural[static_cast<types::uint8_t>(*ptr)];
        if (flags & lookups::SCAN_NORMAL) {
            *cursor++ = static_cast<types::uint64_t>(ptr - src);
            ptr++;
        } else if (flags & lookups::SCAN_QUOTE) {
            *cursor++ = static_cast<types::uint64_t>(ptr - src);
            ptr++;

            while (true) {
                while (ptr + sizeof(types::uint64_t) <= end) {
                    types::uint64_t block;
                    __builtin_memcpy(&block, ptr, sizeof(types::uint64_t));

                    types::uint64_t mask =
                        utils::swar_has_zero_byte(
                            block ^
                            utils::repeat_byte_v<types::uint64_t, 0x22_u8>) |
                        utils::swar_has_zero_byte(
                            block ^
                            utils::repeat_byte_v<types::uint64_t, 0x5C_u8>);

                    if (mask != 0) {
                        ptr += __builtin_ctzll(mask) >> 3;
                        break;
                    } else {
                        ptr += sizeof(types::uint64_t);
                        continue;
                    }
                }

                if (ptr >= end) {
                    return types::Unexpected{core::JsonErrc::UnexpectedQuote};
                }

                if (*ptr == '\\') {
                    if (ptr + 1 >= end) {
                        return types::Unexpected{
                            core::JsonErrc::UnexpectedQuote};
                    }
                    ptr += 2;
                    continue;
                }

                if (*ptr == '\"') {
                    break;
                }
                ptr++;
            }

            if (cursor == end_cursor) {
                return types::Unexpected{core::JsonErrc::InsufficientBuffer};
            }

            *cursor++ = static_cast<types::uint64_t>(ptr - src);
            ptr++;
        }
    }
    return static_cast<types::size_t>(cursor - dst);
}
#elif __IS_64_BIT__ == 0
types::Result<types::size_t, core::JsonErrc>
scanner32(
    const char* const RESTRICT src, types::size_t src_size,
    types::size_t* RESTRICT dst, types::size_t dst_size) NOEXCEPT {
    if (!src || src_size <= 1) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }

    if (!dst || dst_size <= 1) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InsufficientBuffer};
    }

    if (!utils::Utf8Validator(src, src_size)) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }

    auto ptr              = src;
    const auto end        = src + src_size;
    auto cursor           = dst;
    const auto end_cursor = dst + dst_size;

    while (ptr < end && cursor < end_cursor) {
        while (ptr < end &&
               lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
            ptr++;
        }

        if (ptr >= end) {
            break;
        }

        const auto flags =
            lookups::kStructural[static_cast<types::uint8_t>(*ptr)];
        if (flags & lookups::SCAN_NORMAL) {
            *cursor++ = static_cast<types::uint64_t>(ptr - src);
            ptr++;
        } else if (flags & lookups::SCAN_QUOTE) {
            *cursor++ = static_cast<types::uint64_t>(ptr - src);
            ptr++;

            while (true) {
                while (ptr + sizeof(types::uint64_t) <= end) {
                    types::uint64_t block;
                    __builtin_memcpy(&block, ptr, sizeof(types::uint64_t));

                    types::uint64_t mask =
                        utils::swar_has_zero_byte(
                            block ^
                            utils::repeat_byte_v<types::uint64_t, 0x22_u8>) |
                        utils::swar_has_zero_byte(
                            block ^
                            utils::repeat_byte_v<types::uint64_t, 0x5C_u8>);

                    if (mask != 0) {
                        ptr += __builtin_ctzl(mask) >> 3;
                        break;
                    } else {
                        ptr += sizeof(types::uint64_t);
                        continue;
                    }
                }

                if (ptr >= end) {
                    return types::Unexpected{core::JsonErrc::UnexpectedQuote};
                }

                if (*ptr == '\\') {
                    if (ptr + 1 >= end) {
                        return types::Unexpected{
                            core::JsonErrc::UnexpectedQuote};
                    }
                    ptr += 2;
                    continue;
                }

                if (*ptr == '\"') {
                    break;
                }
                ptr++;
            }

            if (cursor == end_cursor) {
                return types::Unexpected{core::JsonErrc::InsufficientBuffer};
            }

            *cursor++ = static_cast<types::uint64_t>(ptr - src);
            ptr++;
        }
    }
    return static_cast<types::size_t>(cursor - dst);
}
#endif // __IS_64_BIT__ == 0
#endif // __LITTLE_ENDIAN__ == 1

} // namespace zuu::scanner