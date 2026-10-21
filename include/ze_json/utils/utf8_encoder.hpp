#pragma once

#include <ze_json/types/integral.hpp>
#include <ze_json/types/string_view.hpp>
#include <ze_json/utils/repeat_byte.hpp>
#include <ze_json/utils/swar.hpp>

namespace zuu::utils {

constexpr int
hex_to_int(char c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

constexpr bool
contains_backslash(const char* src, types::size_t len) noexcept {
    types::size_t i = 0;

    while (i + sizeof(types::uint64_t) <= len) {
        types::uint64_t block;
        __builtin_memcpy(&block, src + i, sizeof(types::uint64_t));

        // Masking bitwise XOR untuk mencari byte 0x5C ('\\')
        types::uint64_t x =
            block ^ utils::repeat_byte_v<types::uint64_t, 0x5C_u8>;
        if (utils::swar_has_zero_byte(x) != 0) {
            return true;
        }
        i += sizeof(types::uint64_t);
    }

    while (i < len) {
        if (src[i] == '\\')
            return true;
        i++;
    }
    return false;
}

constexpr types::size_t
encode_utf8(types::uint32_t cp, char* dst) noexcept {
    if (cp <= 0x7F) {
        dst[0] = static_cast<char>(cp);
        return 1;
    } else if (cp <= 0x7FF) {
        dst[0] = static_cast<char>(0xC0 | (cp >> 6));
        dst[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        dst[0] = static_cast<char>(0xE0 | (cp >> 12));
        dst[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        dst[0] = static_cast<char>(0xF0 | (cp >> 18));
        dst[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        dst[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        dst[3] = static_cast<char>(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

template <typename ArenaPtr>
types::StringView
unescape(const char* src, types::size_t len, ArenaPtr arena) noexcept {
    // TAHAP 1: ZERO-COPY FAST PATH
    // Jika tidak ada karakter backslash '\\', kembalikan StringView langsung ke
    // payload asli
    if (!contains_backslash(src, len)) [[likely]] {
        return types::StringView(src, len);
    }

    // TAHAP 2: SLOW PATH (Membutuhkan Arena Allocator untuk menyimpan string
    // unescaped)
    if (!arena) [[unlikely]] {
        // Fallback jika Arena tidak disediakan
        return types::StringView(src, len);
    }

    char* dst = static_cast<char*>(arena->allocate(len));
    if (!dst) [[unlikely]]
        return types::StringView(src, len);

    types::size_t src_idx = 0;
    types::size_t dst_idx = 0;

    while (src_idx < len) {
        char c = src[src_idx++];

        if (c != '\\') {
            dst[dst_idx++] = c;
            continue;
        }

        if (src_idx >= len)
            break; // Terpotong di akhir
        char escaped = src[src_idx++];

        switch (escaped) {
        case '"':
            dst[dst_idx++] = '"';
            break;
        case '\\':
            dst[dst_idx++] = '\\';
            break;
        case '/':
            dst[dst_idx++] = '/';
            break;
        case 'b':
            dst[dst_idx++] = '\b';
            break;
        case 'f':
            dst[dst_idx++] = '\f';
            break;
        case 'n':
            dst[dst_idx++] = '\n';
            break;
        case 'r':
            dst[dst_idx++] = '\r';
            break;
        case 't':
            dst[dst_idx++] = '\t';
            break;
        case 'u': {
            if (src_idx + 4 > len)
                break;

            int h1 = hex_to_int(src[src_idx]);
            int h2 = hex_to_int(src[src_idx + 1]);
            int h3 = hex_to_int(src[src_idx + 2]);
            int h4 = hex_to_int(src[src_idx + 3]);

            if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0)
                break;

            types::uint32_t codepoint = static_cast<types::uint32_t>(
                (h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
            src_idx += 4;

            // Penanganan UTF-16 Surrogate Pair (\uD800..\uDBFF diikuti
            // \uDC00..\uDFFF)
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                if (src_idx + 6 <= len && src[src_idx] == '\\' &&
                    src[src_idx + 1] == 'u') {
                    int l1 = hex_to_int(src[src_idx + 2]);
                    int l2 = hex_to_int(src[src_idx + 3]);
                    int l3 = hex_to_int(src[src_idx + 4]);
                    int l4 = hex_to_int(src[src_idx + 5]);

                    if (l1 >= 0 && l2 >= 0 && l3 >= 0 && l4 >= 0) {
                        types::uint32_t low_surrogate =
                            static_cast<types::uint32_t>(
                                (l1 << 12) | (l2 << 8) | (l3 << 4) | l4);
                        if (low_surrogate >= 0xDC00 &&
                            low_surrogate <= 0xDFFF) {
                            codepoint =
                                0x10000 + (((codepoint - 0xD800) << 10) |
                                           (low_surrogate - 0xDC00));
                            src_idx += 6;
                        }
                    }
                }
            }

            dst_idx += encode_utf8(codepoint, dst + dst_idx);
            break;
        }
        default:
            dst[dst_idx++] = escaped;
            break;
        }
    }

    return types::StringView(dst, dst_idx);
}

} // namespace zuu::utils