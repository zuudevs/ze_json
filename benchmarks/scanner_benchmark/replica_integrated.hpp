#include <benchmark/benchmark.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <unordered_map>
#include <ze_json/scanner/scanner.hpp>

// ============================================================================
//  REPLIKA algoritma lawas — baseline INTERLEAVED dalam satu proses.
// ============================================================================
//  Jam antar-proses di rig ini bergetar 15-45% (bahkan run binary-identik
//  bisa berbeda ±25%); satu-satunya perbandingan yang sahih adalah
//  bersebelahan DALAM satu proses. Replika di bawah menyalin algoritma
//  PRA-pemisahan (Tahap 4: validasi UTF-8 terintegrasi ke loop skalar +
//  depth agregat) untuk diadu melawan varian tersimpan di library.
//  Jangan dirapikan/diubah — replika HARUS membekukan algoritma lawas.

namespace {

constexpr bool
utf8_step_replica(
    zuu::types::uint32_t& state, const char* byte_ptr) noexcept {
    const auto type =
        zuu::lookups::kUtf8Dfa[static_cast<zuu::types::uint8_t>(*byte_ptr)];
    state = zuu::lookups::kUtf8Dfa[256 + state + type];
    return state != 12;
}

// --- scanner8 TERINTEGRASI (Tahap 4, pra-pemisahan v0.1.5) --------------
//   Validasi UTF-8 per byte inline di loop skalar + kedalaman bertipe.
//   Ini algoritma yang menurunkan throughput skalar -27..43%.
zuu::types::Result<zuu::types::size_t, zuu::core::JsonErrc>
scanner8_integrated_replica(
    const char* src, zuu::types::size_t src_size,
    zuu::types::size_t* dst, zuu::types::size_t dst_size) noexcept {
    using namespace zuu;
    if (!src || src_size <= 1) {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }
    if (!dst || dst_size <= 1) {
        return types::Unexpected{core::JsonErrc::InsufficientBuffer};
    }

    auto ptr              = src;
    const auto end        = src + src_size;
    auto cursor           = dst;
    const auto end_cursor = dst + dst_size;

    types::int64_t curly_depth  = 0;
    types::int64_t square_depth = 0;
    types::uint32_t utf8         = 0;

    while (ptr < end && cursor < end_cursor) {
        while (ptr < end &&
               (lookups::kStructural[static_cast<types::uint8_t>(*ptr)]) == 0) {
            if (utf8 != 0 || (*ptr & 0x80) != 0) {
                if (!utf8_step_replica(utf8, ptr)) {
                    return types::Unexpected{core::JsonErrc::InvalidUtf8};
                }
            }
            ptr++;
        }
        if (ptr >= end) {
            break;
        }
        if (utf8 != 0 || (*ptr & 0x80) != 0) {
            if (!utf8_step_replica(utf8, ptr)) {
                return types::Unexpected{core::JsonErrc::InvalidUtf8};
            }
        }
        const auto flags =
            lookups::kStructural[static_cast<types::uint8_t>(*ptr)];
        if (flags & lookups::PAIR_CLOSE) {
            if (*ptr == '}') {
                if (curly_depth == 0) {
                    return types::Unexpected{core::JsonErrc::InvalidJson};
                }
                curly_depth--;
            } else {
                if (square_depth == 0) {
                    return types::Unexpected{core::JsonErrc::InvalidJson};
                }
                square_depth--;
            }
        } else if (flags & lookups::PAIR_OPEN) {
            if (*ptr == '{') {
                curly_depth++;
            } else {
                square_depth++;
            }
        }
        if (flags & lookups::SCAN_NORMAL) {
            *cursor++ = static_cast<types::size_t>(ptr - src);
            ptr++;
        } else if (flags & lookups::SCAN_QUOTE) {
            *cursor++ = static_cast<types::size_t>(ptr - src);
            ptr++;

            while (true) {
                if (ptr >= end) {
                    return types::Unexpected{core::JsonErrc::UnexpectedQuote};
                }
                if (utf8 != 0 || (*ptr & 0x80) != 0) {
                    if (!utf8_step_replica(utf8, ptr)) {
                        return types::Unexpected{core::JsonErrc::InvalidUtf8};
                    }
                }
                if (*ptr == '\\') {
                    if (ptr + 1 >= end) {
                        return types::Unexpected{
                            core::JsonErrc::UnexpectedQuote};
                    }
                    if (utf8 != 0 || (ptr[1] & 0x80) != 0) {
                        if (!utf8_step_replica(utf8, ptr + 1)) {
                            return types::Unexpected{
                                core::JsonErrc::InvalidUtf8};
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
                return types::Unexpected{core::JsonErrc::InsufficientBuffer};
            }
            *cursor++ = static_cast<types::size_t>(ptr - src);
            ptr++;
        }
    }

    if (curly_depth != 0 || square_depth != 0) {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }
    if (utf8 != 0) {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }
    return static_cast<types::size_t>(cursor - dst);
}

// --- scanner64 UNTYPED (depth agregat, pra-v0.1.5) -----------------------
//   Isolasi biaya kedalaman bertipe pada jalur struktural scanner64.
//   Identik dengan scanner64 saat ini KECUALI satu counter agregat.
zuu::types::Result<zuu::types::size_t, zuu::core::JsonErrc>
scanner64_untyped_replica(
    const char* src, zuu::types::size_t src_size,
    zuu::types::size_t* dst, zuu::types::size_t dst_size) noexcept {
    using namespace zuu;
    if (!src || src_size <= 1) {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }
    if (!dst || dst_size <= 1) {
        return types::Unexpected{core::JsonErrc::InsufficientBuffer};
    }

    auto ptr              = src;
    const auto end        = src + src_size;
    auto cursor           = dst;
    const auto end_cursor = dst + dst_size;

    types::int64_t depth = 0; // ← agregat (bukan per-jenis)
    types::uint32_t utf8 = 0;

    while (ptr < end && cursor < end_cursor) {
        while (ptr < end &&
               lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
            if (utf8 != 0 || (*ptr & 0x80) != 0) {
                if (!utf8_step_replica(utf8, ptr)) {
                    return types::Unexpected{core::JsonErrc::InvalidUtf8};
                }
            }
            ptr++;
        }
        if (ptr >= end) {
            break;
        }
        if (utf8 != 0 || (*ptr & 0x80) != 0) {
            if (!utf8_step_replica(utf8, ptr)) {
                return types::Unexpected{core::JsonErrc::InvalidUtf8};
            }
        }
        const auto flags =
            lookups::kStructural[static_cast<types::uint8_t>(*ptr)];
        if (flags & lookups::PAIR_CLOSE) {
            if (depth == 0) {
                return types::Unexpected{core::JsonErrc::InvalidJson};
            }
            depth--;
        } else if (flags & lookups::PAIR_OPEN) {
            depth++;
        }
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

                    if (utf8 == 0 &&
                        (block & 0x8080808080808080ULL) == 0) {
                        types::uint64_t mask =
                            utils::swar_has_zero_byte(
                                block ^
                                utils::repeat_byte_v<types::uint64_t,
                                                    0x22_u8>) |
                            utils::swar_has_zero_byte(
                                block ^
                                utils::repeat_byte_v<types::uint64_t,
                                                    0x5C_u8>);

                        if (mask != 0) {
                            ptr += __builtin_ctzll(mask) >> 3;
                            break;
                        } else {
                            ptr += sizeof(types::uint64_t);
                            continue;
                        }
                    }

                    types::size_t k = 0;
                    for (; k < sizeof(types::uint64_t); ++k) {
                        const char* RESTRICT p = ptr + k;
                        if (utf8 != 0 || (*p & 0x80) != 0) {
                            if (!utf8_step_replica(utf8, p)) {
                                return types::Unexpected{
                                    core::JsonErrc::InvalidUtf8};
                            }
                        }
                        if (*p == '"' || *p == '\\') {
                            break;
                        }
                    }
                    if (k < sizeof(types::uint64_t)) {
                        ptr += k;
                        break;
                    }
                    ptr += sizeof(types::uint64_t);
                }

                if (ptr >= end) {
                    return types::Unexpected{core::JsonErrc::UnexpectedQuote};
                }
                if (utf8 != 0 || (*ptr & 0x80) != 0) {
                    if (!utf8_step_replica(utf8, ptr)) {
                        return types::Unexpected{core::JsonErrc::InvalidUtf8};
                    }
                }
                if (*ptr == '\\') {
                    if (ptr + 1 >= end) {
                        return types::Unexpected{
                            core::JsonErrc::UnexpectedQuote};
                    }
                    if (utf8 != 0 || (ptr[1] & 0x80) != 0) {
                        if (!utf8_step_replica(utf8, ptr + 1)) {
                            return types::Unexpected{
                                core::JsonErrc::InvalidUtf8};
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
                return types::Unexpected{core::JsonErrc::InsufficientBuffer};
            }
            *cursor++ = static_cast<types::uint64_t>(ptr - src);
            ptr++;
        }
    }

    if (depth != 0) {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }
    if (utf8 != 0) {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }
    return static_cast<types::size_t>(cursor - dst);
}

} // namespace
