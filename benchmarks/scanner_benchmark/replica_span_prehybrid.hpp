// ============================================================================
//  REPLIKA scanner64 pra-hybrid span (baseline INTERLEAVED satu proses).
// ============================================================================
//  Jalur jam antar-proses di rig ini bergetar 15-45%; satu-satunya
//  perbandingan sahih adalah bersebelahan DALAM satu proses. File ini
//  membekukan scanner64 SEBELUM eksperimen A1 (replika probe hybrid span)
//  — loop span non-string byte-per-byte verbatim v0.1.6 (HEAD 532039c).
//  Jangan dirapikan/diubah — replika HARUS membekukan algoritma lawas.
//  Bandingkan lawan library scanner64 (yang kini hybrid) lewat pasangan
//  `spanhybrid_library_vs_spanbaseline` di notebook §5.
// ============================================================================
#include <benchmark/benchmark.h>
#include <cstddef>
#include <ze_json/lookups/utf8_dfa.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/utils/swar.hpp>

namespace replica_span_prehybrid {

namespace {
constexpr bool
utf8_step(zuu::types::uint32_t& state, const char* byte_ptr) NOEXCEPT {
    const zuu::types::uint8_t type =
        zuu::lookups::kUtf8Dfa[static_cast<zuu::types::uint8_t>(*byte_ptr)];
    state = zuu::lookups::kUtf8Dfa[256 + state + type];
    return state != 12;
}
} // namespace

[[nodiscard]] zuu::types::Result<zuu::types::size_t, zuu::core::JsonErrc>
scanner64_prehybrid_span_replica(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size) NOEXCEPT {
    using zuu::types::uint64_t;
    if (!src || src_size <= 1) [[unlikely]] {
        return zuu::types::Unexpected{zuu::core::JsonErrc::InvalidJson};
    }

    if (!dst || dst_size <= 1) [[unlikely]] {
        return zuu::types::Unexpected{zuu::core::JsonErrc::InsufficientBuffer};
    }

    auto ptr              = src;
    const auto end        = src + src_size;
    auto cursor           = dst;
    const auto end_cursor = dst + dst_size;

    // Kedalaman bracket BERTIPE via STACK-BIT (Langdale & Lemire 2019 §3.2)
    zuu::types::uint64_t bracket_stack = 0;
    zuu::types::int64_t bracket_depth  = 0;

    // State DFA UTF-8 (0 = ACCEPT).
    zuu::types::uint32_t utf8 = 0;

    while (ptr < end && cursor < end_cursor) {
        while (ptr < end &&
               zuu::lookups::kStructural[static_cast<zuu::types::uint8_t>(
                   *ptr)] == 0) {
            if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
                if (!utf8_step(utf8, ptr)) [[unlikely]] {
                    return zuu::types::Unexpected{
                        zuu::core::JsonErrc::InvalidUtf8};
                }
            }
            ptr++;
        }

        if (ptr >= end) {
            break;
        }

        if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
            if (!utf8_step(utf8, ptr)) [[unlikely]] {
                return zuu::types::Unexpected{
                    zuu::core::JsonErrc::InvalidUtf8};
            }
        }

        const auto flags =
            zuu::lookups::kStructural[static_cast<zuu::types::uint8_t>(*ptr)];
        if (flags & zuu::lookups::PAIR_CLOSE) {
            if (*ptr == '}') {
                if (bracket_depth == 0 || (bracket_stack & 1) == 0) {
                    return zuu::types::Unexpected{
                        zuu::core::JsonErrc::InvalidJson};
                }
                bracket_stack >>= 1;
                bracket_depth--;
            } else {
                if (bracket_depth == 0 || (bracket_stack & 1) != 0) {
                    return zuu::types::Unexpected{
                        zuu::core::JsonErrc::InvalidJson};
                }
                bracket_stack >>= 1;
                bracket_depth--;
            }
        } else if (flags & zuu::lookups::PAIR_OPEN) {
            bracket_stack = (bracket_stack << 1) | (*ptr == '{' ? 1u : 0u);
            bracket_depth++;
        }

        if (flags & zuu::lookups::SCAN_NORMAL) {
            *cursor++ = static_cast<zuu::types::uint64_t>(ptr - src);
            ptr++;
        } else if (flags & zuu::lookups::SCAN_QUOTE) {
            *cursor++ = static_cast<zuu::types::uint64_t>(ptr - src);
            ptr++;

            while (true) {
                while (ptr + sizeof(uint64_t) <= end) {
                    uint64_t block;
                    __builtin_memcpy(&block, ptr, sizeof(uint64_t));

                    if (utf8 == 0 &&
                        (block & 0x8080808080808080ULL) == 0) [[likely]] {
                        zuu::types::uint64_t mask =
                            zuu::utils::swar_has_zero_byte(
                                block ^
                                zuu::utils::repeat_byte_v<uint64_t,
                                                          0x22_u8>) |
                            zuu::utils::swar_has_zero_byte(
                                block ^
                                zuu::utils::repeat_byte_v<uint64_t,
                                                          0x5C_u8>);

                        if (mask != 0) {
                            ptr += __builtin_ctzll(mask) >> 3;
                            break;
                        } else {
                            ptr += sizeof(uint64_t);
                            continue;
                        }
                    }

                    zuu::types::size_t k = 0;
                    for (; k < sizeof(uint64_t); ++k) {
                        const char* RESTRICT p = ptr + k;
                        if (utf8 != 0 || (*p & 0x80) != 0) {
                            if (!utf8_step(utf8, p)) [[unlikely]] {
                                return zuu::types::Unexpected{
                                    zuu::core::JsonErrc::InvalidUtf8};
                            }
                        }
                        if (*p == '"' || *p == '\\') {
                            break;
                        }
                    }
                    if (k < sizeof(uint64_t)) {
                        ptr += k;
                        break;
                    }
                    ptr += sizeof(uint64_t);
                }

                if (ptr >= end) {
                    return zuu::types::Unexpected{
                        zuu::core::JsonErrc::UnexpectedQuote};
                }

                if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
                    if (!utf8_step(utf8, ptr)) [[unlikely]] {
                        return zuu::types::Unexpected{
                            zuu::core::JsonErrc::InvalidUtf8};
                    }
                }

                if (*ptr == '\\') {
                    if (ptr + 1 >= end) {
                        return zuu::types::Unexpected{
                            zuu::core::JsonErrc::UnexpectedQuote};
                    }
                    if (utf8 != 0 || (ptr[1] & 0x80) != 0) [[unlikely]] {
                        if (!utf8_step(utf8, ptr + 1)) [[unlikely]] {
                            return zuu::types::Unexpected{
                                zuu::core::JsonErrc::InvalidUtf8};
                        }
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
                return zuu::types::Unexpected{
                    zuu::core::JsonErrc::InsufficientBuffer};
            }

            *cursor++ = static_cast<zuu::types::uint64_t>(ptr - src);
            ptr++;
        }
    }

    if (bracket_depth != 0) {
        return zuu::types::Unexpected{zuu::core::JsonErrc::InvalidJson};
    }

    if (utf8 != 0) [[unlikely]] {
        return zuu::types::Unexpected{zuu::core::JsonErrc::InvalidUtf8};
    }
    return static_cast<zuu::types::size_t>(cursor - dst);
}

} // namespace replica_span_prehybrid
