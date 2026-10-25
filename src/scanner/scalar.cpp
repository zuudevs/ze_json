// ============================================================================
//  scanner/scalar.cpp — ALGORITMA SKALAR (scanner8)
// ============================================================================
//  Varian portabel byte-per-byte. STRATEGI UTF-8: pass validator TERPISAH
//  di depan pemindaian (utils::Utf8Validator — jalur cepat SWAR 8-byte
//  untuk blok ASCII murni, DFA per byte hanya untuk non-ASCII).
//
//  Mengapa terpisah? Loop skalar step-by-step tidak mendapat apa pun dari
//  cek DFA inline: uji `utf8 != 0 || high-bit` per byte di jalur inti
//  terukur MENGGERUS throughput -27..43% pada korpus (regresi Tahap 4,
//  terlihat di scanner_benchmark artefak 47e9cb7 vs 26c8330), sementara
//  validator SWAR memproses ASCII 8 byte/iterasi. Filosofi Langdale &
//  Lemire 2019 / branchfree.org: kerja blok-reguler dan kerja langkah-
//  demi-langkah adalah DUA algoritma — jangan digabungkan.
//  (Validasi terintegrasi dipertahankan di varian paralel — parallel.cpp.)
// ============================================================================
#include <ze_json/scanner/scanner.hpp>
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

    // Pass validasi UTF-8 terpisah — blok ASCII murni dibayar per-8-byte.
    if (!utils::Utf8Validator(src, src_size)) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }

    auto ptr              = src;
    const auto end        = src + src_size;
    auto cursor           = dst;
    const auto end_cursor = dst + dst_size;

    // Kedalaman bracket BERTIPE via STACK-BIT (Langdale & Lemire 2019 §3.2:
    // kontainer dibuka '{' wajib ditutup '}', '[' wajib ']', DAN urutan
    // tumpukan wajib konsisten). BRANCHLESS di jalur struktural: satu
    // shift + XOR per bracket. Bit menumpuk tipe (0=array ']', 1=object
    // '}'): pembuka push, penutup pop + wajib cocok (mismatch → reject),
    // underflow stack → reject. SALING-SILANG [{]} pun tertangkap.
    types::uint64_t bracket_stack = 0; // bit tipe kontainer terbuka
    types::int64_t bracket_depth  = 0;

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

        if (flags & lookups::PAIR_CLOSE) {
            // Tutup tanpa pasangan buwa JENIS yang sama → JSON bermata.
            if (*ptr == '}') {
                if (bracket_depth == 0 || (bracket_stack & 1) == 0) {
                    return types::Unexpected{core::JsonErrc::InvalidJson};
                }
                bracket_stack >>= 1;
                bracket_depth--;
            } else { // ']' — satu-satunya PAIR_CLOSE lain
                if (bracket_depth == 0 || (bracket_stack & 1) != 0) {
                    return types::Unexpected{core::JsonErrc::InvalidJson};
                }
                bracket_stack >>= 1;
                bracket_depth--;
            }
        } else if (flags & lookups::PAIR_OPEN) {
            bracket_stack = (bracket_stack << 1) | (*ptr == '{' ? 1u : 0u);
            bracket_depth++;
        }

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

    // Wajib semua bracket tertutup: input terpotong (depth > 0) ditolak.
    if (bracket_depth != 0) {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }

    return static_cast<types::size_t>(cursor - dst);
}

} // namespace zuu::scanner
