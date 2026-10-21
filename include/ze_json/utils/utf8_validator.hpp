#pragma once

#include <ze_json/lookups/utf8_dfa.hpp>
#include <ze_json/types/integral.hpp>

namespace zuu::utils {

constexpr bool
Utf8Validator(const char* src, types::size_t len) noexcept {
    if (!src) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    const char* ptr       = src;
    const char* const end = src + len;
    types::uint32_t state = 0; // State awal = 0 (ACCEPT)

    while (ptr < end) {
        // TAHAP 1: SWAR ASCII FAST PATH (Keiser & Lemire Section 4.1)
        while (state == 0 && ptr + sizeof(types::uint64_t) <= end) {
            types::uint64_t block;
            __builtin_memcpy(&block, ptr, sizeof(types::uint64_t));

            // Jika ada bit 0x80 (high bit) yang menyala, artinya ada karakter
            // non-ASCII
            if ((block & 0x8080808080808080ULL) != 0) {
                break; // Berhenti dan serahkan ke DFA
            }

            // Semua 8-byte adalah ASCII murni, Melompat 8 byte instan (Zero
            // Branching)
            ptr += sizeof(types::uint64_t);
        }

        if (ptr >= end) {
            break;
        }

        // TAHAP 2: HOEHRMANN DFA STATE MACHINE (Keiser & Lemire Section 5)
        types::uint8_t byte_val = static_cast<types::uint8_t>(*ptr++);

        // 1. Ambil kelas karakter (0..11) dari 256 byte pertama tabel
        types::uint8_t type = lookups::kUtf8Dfa[byte_val];

        // 2. Transisi state menggunakan offset 256 + state_saat_ini + type
        state = lookups::kUtf8Dfa[256 + state + type];

        // State 12 adalah REJECT (Terdeteksi Invalid UTF-8 Sequence)
        if (state == 12) [[unlikely]] {
            return false;
        }
    }

    return state == 0;
}

} // namespace zuu::utils