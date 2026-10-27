// ============================================================================
//  scanner/parallel.cpp — ALGORITMA PARALEL (scanner64 / scanner32)
// ============================================================================
//  Varian lebar-kata (SWAR blok 8 byte untuk body string). scanner64
//  memakai validasi UTF-8 TERINTEGRASI ke jalan pemindaian: blok ASCII
//  murni + DFA ACCEPT cukup satu uji high-bit per blok (jalur cepat SWAR
//  tetap); blok ber-non-ASCII diproses per byte sehingga sekuens aman
//  terbelah di batas blok. Biaya integrasi teramortisasi oleh blok —
//  menang E2E -42% pada payload UTF-8-padat (Tahap 4).
//  scanner32 (fallback LE 32-bit) memakai pass validator terpisah dan
//  kini juga membawa kedalaman bertipe (dahulu TANPA validasi bracket
//  sama sekali — payload terpotong diterima diam-diam di 32-bit).
//
//  Pemisahan algoritma dari varian skalar (scalar.cpp) mengikuti filosofi
//  Langdale & Lemire 2019 / branchfree.org: kerja blok-reguler dan kerja
//  langkah-demi-langkah adalah dua algoritma berbeda — masing-masing
//  diberi strategi validasi yang paling cocok.
// ============================================================================
#include <ze_json/lookups/utf8_dfa.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/utils/swar.hpp>
#include <ze_json/utils/utf8_validator.hpp>

namespace zuu::scanner {

namespace {
// Langkah DFA UTF-8 satu byte — validasi menumpang jalan scanner paralel.
// Kembali false bila byte melanggar sekuens (state REJECT = 12).
constexpr bool
utf8_step(types::uint32_t& state, const char* byte_ptr) NOEXCEPT {
    const types::uint8_t type =
        lookups::kUtf8Dfa[static_cast<types::uint8_t>(*byte_ptr)];
    state = lookups::kUtf8Dfa[256 + state + type];
    return state != 12;
}

// ---------------------------------------------------------------------------
//  PROBE SPAN HYBRID (Eksperimen A1, benchmarks/span_iso_benchmark):
//  loop span non-string tetap byte-per-byte sebagai mesin, dengan probe
//  blok murah (2-3 predikat, TANPA mask terminator) yang melompat
//  blok penuh digit-run tanpa ':' ATAU spasi penuh — hanya bila span
//  SEBELUMNYA >= 1 blok. Terukur pada REPLIKA span_iso (artefak
//  spaniso_hybrid_interleaved): menang semua regime in-run. Lompatan aman:
//  blok yang lolos predikat tidak mungkin memuat byte struktural (set
//  terminator tidak memuat digit/spasi; ':' satu-satunya terminator
//  nibble-3 dan dieksplisitkan), dan DFA pas-ACCEPT (utf8 == 0)
//  persyaratan probe. V2: probe berada di helper out-of-line (lihat
//  span_probe_scan) — hiper dalam-loop mengganggu codegen (terukur).
// ---------------------------------------------------------------------------
constexpr types::uint64_t kSpanHighBitMask =
    utils::repeat_byte_v<types::uint64_t, 0x80_u8>;
constexpr types::uint64_t kSpanNibbleFMask =
    utils::repeat_byte_v<types::uint64_t, 0xF0_u8>;
constexpr types::uint64_t kSpanNibble3Rep =
    utils::repeat_byte_v<types::uint64_t, 0x30_u8>;
constexpr types::uint64_t kSpanSpaceRep =
    utils::repeat_byte_v<types::uint64_t, 0x20_u8>;
constexpr types::uint64_t kSpanColonRep =
    utils::repeat_byte_v<types::uint64_t, 0x3A_u8>;

// ---------------------------------------------------------------------------
//  PROBE SPAN —_setelah pengukuran V1 (inline di loop utama): hasil
//  spanreal r1+r2 menunjukkan regresi konsisten (0,70-0,99 vs replika beku)
//  padahal gate nyaris tak pernah menyala di data yang regresinya terparah
//  (github 0,05%, jeopardy 0%) → regresi bukan kerja probe, melainkan
//  DISRUPSI CODEGEN pada loop byte (tumpahan span_start/prev_span_len,
//  CFG lebih lebar). V2 maka menaruh seluruh probe pada HELPER OUT-OF-LINE
//  (noinline+noclone): badan scanner64 kembali identik pra-hybrid, call
//  tak-bersyarat satu per span (prediktabil sempurna), gate dievaluasi
//  di dalam helper sehingga tak ada cabang baru di loop byte.
// ---------------------------------------------------------------------------
[[gnu::noinline, gnu::noclone]] const char*
span_probe_scan(
    const char* ptr, const char* end, types::uint32_t utf8,
    types::size_t prev_span_len) NOEXCEPT {
    if (utf8 != 0 || prev_span_len < sizeof(types::uint64_t)) {
        return ptr;
    }
    while (ptr + sizeof(types::uint64_t) <= end) {
        types::uint64_t block;
        __builtin_memcpy(&block, ptr, sizeof(types::uint64_t));
        if ((block & kSpanHighBitMask) == 0 &&
            ((block ^ kSpanSpaceRep) == 0 ||
             (((block & kSpanNibbleFMask) ^ kSpanNibble3Rep) == 0 &&
              utils::swar_has_zero_byte(block ^ kSpanColonRep) == 0))) {
            ptr += sizeof(types::uint64_t);
            continue;
        }
        break; // predikat gagal — kembalikan pointer: byte-loop mengambil alih
    }
    return ptr;
}
} // namespace

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

    auto ptr              = src;
    const auto end        = src + src_size;
    auto cursor           = dst;
    const auto end_cursor = dst + dst_size;

    // Kedalaman bracket BERTIPE via STACK-BIT (Langdale & Lemire 2019 §3.2)
    // — BRANCHLESS di jalur panas struktural: satu shift + XOR per bracket.
    // Bit-31..0 menumpuk tipe (0=array ']', 1=object '}'): pembuka push bit,
    // penutup pop bit dan WAJIB sama (mismatch → reject). Underflow (stack
    // kosong) ditolak. Saling-silang [{]} TIDAK terdeteksi (batas 2-tipe
    // tanpa urutan penuh — ranah scope-stack tahap-2 paper §3.2).
    //
    //    open  '{' : stack = (stack << 1) | 1; depth++
    //    open  '[' : stack = (stack << 1);     depth++
    //    close '}' : reject if depth==0 or (stack & 1)==0; stack >>= 1; depth--
    //    close ']' : reject if depth==0 or (stack & 1)==1; stack >>= 1; depth--
    types::uint64_t bracket_stack = 0; // bit tipe kontainer terbuka
    types::int64_t bracket_depth  = 0;

    // State DFA UTF-8 (0 = ACCEPT) — validasi menumpang jalan scanner.
    types::uint32_t utf8 = 0;

    // Sinyal adaptivitas probe span hybrid (lihat konstanta di atas):
    // panjang span SEBELUMNYA; span pertama diizinkan probe (start state).
    types::size_t prev_span_len = sizeof(types::uint64_t);

    while (ptr < end && cursor < end_cursor) {
        const char* const span_start = ptr;

        // Probe V2 out-of-line — seluruh logika lompat blok di helper
        // (dagang tetap sama; codegen loop byte tidak tersentuh).
        ptr = span_probe_scan(ptr, end, utf8, prev_span_len);

        while (ptr < end &&
               lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
            if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
                if (!utf8_step(utf8, ptr)) [[unlikely]] {
                    return types::Unexpected{core::JsonErrc::InvalidUtf8};
                }
            }
            ptr++;
        }
        // Panjang span TOTAL (probe + byte-loop) — sinyal adaptivitas.
        prev_span_len = static_cast<types::size_t>(ptr - span_start);

        if (ptr >= end) {
            break;
        }

        // Byte struktural pun masuk DFA: ASCII dari state ACCEPT no-op,
        // tetapi kutip penutup di tengah sekuens wajib ditolak.
        if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
            if (!utf8_step(utf8, ptr)) [[unlikely]] {
                return types::Unexpected{core::JsonErrc::InvalidUtf8};
            }
        }

        const auto flags =
            lookups::kStructural[static_cast<types::uint8_t>(*ptr)];
        if (flags & lookups::PAIR_CLOSE) {
            // Tutup tanpa pasangan buwa JENIS yang sama → JSON mati.
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
                        (block & 0x8080808080808080ULL) == 0) [[likely]] {
                        // Blok ASCII murni + DFA ACCEPT: seluruh blok lolos
                        // validasi tanpa DFA — jalur SWAR pencarian
                        // quote/backslash seperti biasa.
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

                    // Blok ber-non-ASCII ATAU DFA sedang di tengah sekuens:
                    // proses byte-per-byte — DFA + deteksi quote/backslash
                    // (sekuens boleh terbelah lintas batas blok 8 byte).
                    types::size_t k = 0;
                    for (; k < sizeof(types::uint64_t); ++k) {
                        const char* RESTRICT p = ptr + k;
                        if (utf8 != 0 || (*p & 0x80) != 0) {
                            if (!utf8_step(utf8, p)) [[unlikely]] {
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
                        break; // special char diserahkan ke ekor scalar
                    }
                    // Seluruh blok bersih — lanjut blok berikutnya.
                    ptr += sizeof(types::uint64_t);
                }

                if (ptr >= end) {
                    return types::Unexpected{core::JsonErrc::UnexpectedQuote};
                }

                if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
                    if (!utf8_step(utf8, ptr)) [[unlikely]] {
                        return types::Unexpected{core::JsonErrc::InvalidUtf8};
                    }
                }

                if (*ptr == '\\') {
                    if (ptr + 1 >= end) {
                        return types::Unexpected{
                            core::JsonErrc::UnexpectedQuote};
                    }
                    if (utf8 != 0 || (ptr[1] & 0x80) != 0) [[unlikely]] {
                        if (!utf8_step(utf8, ptr + 1)) [[unlikely]] {
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

    // Wajib semua bracket tertutup: input terpotong (depth > 0) ditolak.
    if (bracket_depth != 0) {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }

    // Sekuens UTF-8 terpotong di akhir payload (state belum kembali ACCEPT).
    if (utf8 != 0) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }
    return static_cast<types::size_t>(cursor - dst);
}
#elif __IS_64_BIT__ == 0
// ---------------------------------------------------------------------------
//  scanner32 — fallback LE 32-bit. Tidak teruji di rig ini (preset mesin
//  64-bit); logika depth bertipe DIKUNCI sama persis dengan scanner8 agar
//  kontrak penolakan lintas varian identik.
// ---------------------------------------------------------------------------
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

    // Kedalaman bracket BERTIPE via STACK-BIT — mirror scanner8 (scalar.cpp).
    types::uint64_t bracket_stack = 0;
    types::int64_t bracket_depth  = 0;

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

        if (flags & lookups::PAIR_CLOSE) {
            if (*ptr == '}') {
                if (bracket_depth == 0 || (bracket_stack & 1) == 0) {
                    return types::Unexpected{core::JsonErrc::InvalidJson};
                }
                bracket_stack >>= 1;
                bracket_depth--;
            } else { // ']'
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

    // Wajib semua bracket tertutup: input terpotong (depth > 0) ditolak.
    if (bracket_depth != 0) {
        return types::Unexpected{core::JsonErrc::InvalidJson};
    }
    return static_cast<types::size_t>(cursor - dst);
}
#endif // __IS_64_BIT__ == 0
#endif // __LITTLE_ENDIAN__ == 1

} // namespace zuu::scanner
