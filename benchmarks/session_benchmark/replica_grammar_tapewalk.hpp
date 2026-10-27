#pragma once

// ============================================================================
//  replica_grammar_tapewalk.hpp — TAHAP-7 · replika validator gramatikal
//  tape-walk [C2] — DIBEKUKAN untuk pengukuran; TIDAK menyentuh src/.
//
//  Masukan: hasil scanner64 library (src, src_size, tape, tape_len) — apa
//  yang secara nalar akan dipakai pass validator in-process di create_session.
//  Keluaran: accept (true) / reject (false). Satu lintasan O(tape_len):
//  HANYA inspeksi kelas-token di posisi entri + gap di antara entri —
//  TIDAK memindai ulang payload byte-per-byte (angka = gap teks; literal
//  = 1 entri di posisi awal; string = 2 entri pembuka/penutup; sisanya
//  struktural 1 entri — format tape docs/architecture/scanner.md).
//
//  Cakupan yang sengaja DIBIARKAN (dilazy-kan, ranah stage-2 read):
//    - escape liar / karakter kontrol di dalam string (fail15/17/25–28):
//      string opak = pasangan 2 entri.
//    - kedalaman > kMaxDepth (fail18 — depth 21 lolos, cap 4096 jauh di
//      atas praktik; kebijakan depth tetap ranah scanner).
//  Cakupan yang DITOLAK (target fail non-struktural json.org):
//    root non-kontainer (fail1), sampah pasca-akar (fail7/10), kelompok
//    koma/kolon (fail4/5/6/9/19/20/21/22), kunci tak berkutip (fail3/16),
//    angka ilegal (fail13/14/29/30/31), ekspresi JS (fail11/12/23),
//    kunci apostrof (fail24, empiris: 'n' jadi entri tape).
//
//  SEMANTIK — mesin status (docs/architecture/grammar-validation.md):
//    Root   → '[' | '{' wajib (akar skalar ditolak)
//    AOpen  → nilai ATAU tutup-kosong [] / [angka] (gap ws-only atau angka)
//    AValue → nilai WAJIB (']' di sini = koma tergugur → tolak)
//    AAfter → wajib ',' / ']' (gap wajib ws-only/tail-literal)
//    OOpen  → '"' ATAU tutup-kosong {}
//    OKey   → '"' WAJIB ('}' di sini = koma tergugur → tolak)
//    OColon → ':' wajib
//    OVal   → nilai WAJIB; '}'-angka sah ({"a":1}), '}'-kosong ditolak
//    OAfter → wajib ',' / '}'
//    Done   → entri apa pun setelahnya = sampah → tolak
//  Stack u8 kBrArray/kBrObject mengetik kurung (pop wajib cocok konteks
//  state: ']' hanya diterima di state array, '}' di state object).
//
//  Gap: region [lo, pos) — lo = max(prev_pos+1, tail_literal); tail hanya
//  sah pada entri PERTAMA pasca-literal ("true" menyisakan "rue" di gap
//  menuju entri berikutnya — divalidasi literal_matches, bukan ws-check).
//  Angka = region canonical RFC-8259; ws = {spasi, tab, lf, cr} — persis
//  himpunan has_inline_number iterator.
//
//  Debug: validate_grammar_tapewalk_dbg menulis posisi/state/tag penolakan
//  HANYA di jalur gagal — nol biaya di jalur accept (diagnostik gerbang;
//  dipakai juga uji odontologi korpus json.org).
// ============================================================================

#include <ze_json/core/macros.hpp>
#include <ze_json/types/integral.hpp>

namespace grammar_replica {

using zuu::types::size_t;
using zuu::types::uint8_t;
using zuu::types::uint64_t;

// Cadangan batas kedalaman: dokumen sah lebih dalam dari ini akan ditolak
// (dokumentasi keterbatasan eksplisit — fail18 berkedalaman 21 lolos erat).
constexpr size_t kMaxDepth = 4096;

[[nodiscard]] inline bool
is_ws(const char c) NOEXCEPT {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Region [lo, hi) wajib semuanya whitespace (boleh kosong).
[[nodiscard]] inline bool
region_ws_only(const char* src, const size_t lo, const size_t hi) NOEXCEPT {
    for (size_t k = lo; k < hi; ++k) {
        if (!is_ws(src[k])) {
            return false;
        }
    }
    return true;
}

// Region [lo, hi) = SATU angka canonical RFC-8259 dibungkus whitespace
// (-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?). Gap tanpa angka →
// false (013/0x14/1./1e/1e+/+2/digit-ekor/trailing-junk semuanya tertolak).
[[nodiscard]] inline bool
region_number(const char* src, const size_t lo, const size_t hi) NOEXCEPT {
    size_t i = lo;
    while (i < hi && is_ws(src[i])) {
        ++i;
    }
    if (i == hi) {
        return false; // tidak ada angka di gap (koma berbelok / kosong)
    }
    if (src[i] == '-') {
        ++i;
    }
    if (i == hi || src[i] < '0' || src[i] > '9') {
        return false;
    }
    if (src[i] == '0') {
        ++i; // '0' dipimpin digit lain = leading-zero → ekor uji di bawah
    } else {
        while (i < hi && src[i] >= '0' && src[i] <= '9') {
            ++i;
        }
    }
    if (i < hi && src[i] == '.') {
        ++i;
        if (i == hi || src[i] < '0' || src[i] > '9') {
            return false; // "1." tanpa digit
        }
        while (i < hi && src[i] >= '0' && src[i] <= '9') {
            ++i;
        }
    }
    if (i < hi && (src[i] == 'e' || src[i] == 'E')) {
        ++i;
        if (i < hi && (src[i] == '+' || src[i] == '-')) {
            ++i;
        }
        if (i == hi || src[i] < '0' || src[i] > '9') {
            return false; // "1e" / "1e+" tanpa digit
        }
        while (i < hi && src[i] >= '0' && src[i] <= '9') {
            ++i;
        }
    }
    while (i < hi && is_ws(src[i])) {
        ++i;
    }
    return i == hi; // seluruh region habis dimakan (1 + 2 → ekor tertolak)
}

// Literal persis true/false/null pada posisi entri + terminator non-alnum
// ("truex"/"true1" tertolak; sisa "rue" milik gap berikutnya — pemanggil
// menandai tail = pos + mlen sekali).
[[nodiscard]] inline bool
literal_matches(
    const char* src, const size_t pos, const size_t src_size,
    size_t& mlen) NOEXCEPT {
    bool ok = false;
    switch (src[pos]) {
    case 't':
        ok = pos + 4 <= src_size && src[pos + 1] == 'r' &&
             src[pos + 2] == 'u' && src[pos + 3] == 'e';
        mlen = 4;
        break;
    case 'f':
        ok = pos + 5 <= src_size && src[pos + 1] == 'a' &&
             src[pos + 2] == 'l' && src[pos + 3] == 's' &&
             src[pos + 4] == 'e';
        mlen = 5;
        break;
    case 'n':
        ok = pos + 4 <= src_size && src[pos + 1] == 'u' &&
             src[pos + 2] == 'l' && src[pos + 3] == 'l';
        mlen = 4;
        break;
    default:
        return false;
    }
    if (!ok || pos + mlen == src_size) {
        return ok;
    }
    const char t = src[pos + mlen];
    return !((t >= 'a' && t <= 'z') || (t >= 'A' && t <= 'Z') ||
             (t >= '0' && t <= '9') || t == '_');
}

enum class St : uint8_t {
    Root,   // karakter akar wajib '[' / '{'
    AOpen,  // array: nilai ATAU tutup-kosong (posisi elemen pertama)
    AValue, // array setelah koma: nilai WAJIB; ']' = koma tergugur
    AAfter, // array setelah nilai: wajib ',' / ']'
    OOpen,  // object: '"' key, ATAU tutup-kosong
    OKey,   // object setelah koma: '"' WAJIB ('}' = koma tergugur)
    OColon, // setelah key: ':' wajib
    OVal,   // object-slot nilai WAJIB ('}' tanpa nilai ilegal)
    OAfter, // object setelah nilai: wajib ',' / '}'
    Done,
};

constexpr uint8_t kBrArray  = 0;
constexpr uint8_t kBrObject = 1;

// Pop kontainer: kontainer LANGSUNG pembungkus menentukan state
// "setelah nilai" level luar. Setelah --depth, elemen pembungkus berada
// di stack[depth-1]; elemen yang baru di-pop adalah stack[depth].
// depth==0 tak mungkin di jalur sah (push selalu mendahului pop).
[[nodiscard]] inline St
pop_state(const uint8_t* const stack, size_t& depth) NOEXCEPT {
    if (depth == 0) [[unlikely]] {
        depth = 0;
        return St::Done;
    }
    --depth;
    return depth == 0
               ? St::Done
               : (stack[depth - 1] == kBrArray ? St::AAfter : St::OAfter);
}

[[nodiscard]] inline bool
validate_grammar_tapewalk_dbg(
    const char* const src, const size_t src_size,
    const uint64_t* const tape, const size_t tape_len,
    size_t* const out_pos, int* const out_state) NOEXCEPT {
    if (tape_len == 0) {
        if (out_pos) {
            *out_pos = 0;
        }
        if (out_state) {
            *out_state = -1;
        }
        return false; // akar skalar: bukan '[' / '{'
    }

    uint8_t stack[kMaxDepth];
    size_t depth    = 0;
    size_t i        = 0;
    size_t prev_pos = 0;
    size_t tail     = 0; // satu-melampau-teks-literal (0 = tak ada tail)
    St st           = St::Root;

    // Penolakan berbasis tag — argumen hanya di tulis pada jalur gagal.
#define GR_FAIL(TAG)                                                          \
    do {                                                                      \
        if (out_pos) {                                                        \
            *out_pos = pos;                                                   \
        }                                                                     \
        if (out_state) {                                                      \
            *out_state = static_cast<int>(st);                                \
        }                                                                     \
        return false;                                                         \
    } while (0)

    while (i < tape_len) {
        const size_t pos = static_cast<size_t>(tape[i]);
        if (pos >= src_size) [[unlikely]] {
            GR_FAIL("tape-pos-luar-src");
        }
        const char c = src[pos];

        // Entri-0: gap = [0, pos); entri lain: [prev_pos+1, pos), efektif
        // mulai tail-literal bila ada (tail sah HANYA entri pertama).
        const size_t lo =
            tail != 0 && tail > prev_pos + 1 ? tail
                                             : (i == 0 ? 0 : prev_pos + 1);
        tail     = 0;
        size_t jump = 1;   // penambahan entri iterasi ini
        size_t last_pos = pos; // posisi entri terakhir yang dikonsumsi

        // Konsumsi nilai berbasis entri: gap wajib ws-only yang SUDAH diuji
        // pemanggil. '['/'{' push + child-state; '"' wajib pasangan 2 entri;
        // 't'/'f'/'n' literal persis + tail.
        auto take_value = [&](const St after) -> bool {
            switch (c) {
            case '[':
                if (depth == kMaxDepth) [[unlikely]] {
                    GR_FAIL("depth-cap");
                }
                stack[depth++] = kBrArray;
                st             = St::AOpen;
                return true;
            case '{':
                if (depth == kMaxDepth) [[unlikely]] {
                    GR_FAIL("depth-cap");
                }
                stack[depth++] = kBrObject;
                st             = St::OOpen;
                return true;
            case '"':
                if (i + 1 >= tape_len ||                     // NOLINT
                    static_cast<size_t>(tape[i + 1]) >=      // NOLINT
                        src_size) [[unlikely]] {             // NOLINT
                    GR_FAIL("string-pair-ujung");
                }
                if (src[static_cast<size_t>(tape[i + 1])] != '"') {
                    GR_FAIL("string-pair-tak-kutip");
                }
                jump     = 2;
                last_pos = static_cast<size_t>(tape[i + 1]);
                st       = after;
                return true;
            default: // 't' / 'f' / 'n' — kelas SCAN_NORMAL pada entri
                size_t mlen = 0;
                if (!literal_matches(src, pos, src_size, mlen)) {
                    GR_FAIL("literal-tak-cocok");
                }
                tail = pos + mlen;
                st   = after;
                return true;
            }
        };

        switch (st) {
        case St::Root:
            if (i != 0) [[unlikely]] {
                GR_FAIL("root-lebih-dari-sekali");
            }
            if (!region_ws_only(src, 0, pos)) {
                GR_FAIL("sampah-sebelum-akar");
            }
            if (c == '[') {
                if (depth == kMaxDepth) [[unlikely]] {
                    GR_FAIL("depth-cap");
                }
                stack[depth++] = kBrArray;
                st             = St::AOpen;
            } else if (c == '{') {
                if (depth == kMaxDepth) [[unlikely]] {
                    GR_FAIL("depth-cap");
                }
                stack[depth++] = kBrObject;
                st             = St::OOpen;
            } else {
                GR_FAIL("root-bukan-kontainer");
            }
            break;

        case St::AOpen: // nilai ATAU [] / [angka]
            if (c == ']') {
                if (!region_ws_only(src, lo, pos) &&
                    !region_number(src, lo, pos)) {
                    GR_FAIL("AOpen-gap-non-angka");
                }
                st = pop_state(stack, depth);
            } else if (c == ',') {
                if (!region_number(src, lo, pos)) {
                    GR_FAIL("AOpen-koma-tanpa-nilai");
                }
                st = St::AValue;
            } else {
                if (!region_ws_only(src, lo, pos)) {
                    GR_FAIL("AOpen-sampah-gap-nilai");
                }
                if (!take_value(St::AAfter)) {
                    GR_FAIL("AOpen-take-value");
                }
            }
            break;

        case St::AValue: // nilai WAJIB; ']' ws-only = koma tergugur
            if (c == ']') {
                if (!region_number(src, lo, pos)) {
                    GR_FAIL("AValue-koma-tergugur");
                }
                st = pop_state(stack, depth);
            } else if (c == ',') {
                if (!region_number(src, lo, pos)) {
                    GR_FAIL("AValue-koma-ganda");
                }
                // tetap AValue: nilai baru wajib
            } else {
                if (!region_ws_only(src, lo, pos)) {
                    GR_FAIL("AValue-sampah-gap-nilai");
                }
                if (!take_value(St::AAfter)) {
                    GR_FAIL("AValue-take-value");
                }
            }
            break;

        case St::AAfter:
            if (c == ',') {
                if (!region_ws_only(src, lo, pos)) {
                    GR_FAIL("AAfter-nilai-beda-ws");
                }
                st = St::AValue;
            } else if (c == ']') {
                if (!region_ws_only(src, lo, pos)) {
                    GR_FAIL("AAfter-tutup-ganjil");
                }
                st = pop_state(stack, depth);
            } else {
                GR_FAIL("AAfter-token-tak-didirikan");
            }
            break;

        case St::OOpen: // '"' ATAU {}
            if (c == '}') {
                if (!region_ws_only(src, lo, pos)) {
                    GR_FAIL("OOpen-tutup-ganjil");
                }
                st = pop_state(stack, depth);
            } else if (c == '"') {
                if (!region_ws_only(src, lo, pos)) {
                    GR_FAIL("OOpen-sampah-gap-key");
                }
                if (!take_value(St::OColon)) {
                    GR_FAIL("OOpen-take-value");
                }
            } else {
                GR_FAIL("OOpen-kunci-tak-berkutip");
            }
            break;

        case St::OKey: // '"' WAJIB (koma tergugur → '}' ditolak)
            if (c != '"') {
                GR_FAIL("OKey-koma-tergugur");
            }
            if (!region_ws_only(src, lo, pos)) {
                GR_FAIL("OKey-sampah-gap-key");
            }
            if (!take_value(St::OColon)) {
                GR_FAIL("OKey-take-value");
            }
            break;

        case St::OColon:
            if (c != ':') {
                GR_FAIL("OColon-bukan-kolon");
            }
            if (!region_ws_only(src, lo, pos)) {
                GR_FAIL("OColon-ganjil");
            }
            st = St::OVal;
            break;

        case St::OVal: // nilai WAJIB; '}-angka sah, '}-kosong ditolak
            if (c == '}') {
                if (!region_number(src, lo, pos)) {
                    GR_FAIL("OVal-tutup-tanpa-nilai");
                }
                st = pop_state(stack, depth);
            } else if (c == ',') {
                if (!region_number(src, lo, pos)) {
                    GR_FAIL("OVal-koma-tanpa-nilai");
                }
                st = St::OKey;
            } else {
                if (!region_ws_only(src, lo, pos)) {
                    GR_FAIL("OVal-sampah-gap-nilai");
                }
                if (!take_value(St::OAfter)) {
                    GR_FAIL("OVal-take-value");
                }
            }
            break;

        case St::OAfter:
            if (c == ',') {
                if (!region_ws_only(src, lo, pos)) {
                    GR_FAIL("OAfter-nilai-beda-ws");
                }
                st = St::OKey;
            } else if (c == '}') {
                if (!region_ws_only(src, lo, pos)) {
                    GR_FAIL("OAfter-tutup-ganjil");
                }
                st = pop_state(stack, depth);
            } else {
                GR_FAIL("OAfter-token-tak-didirikan");
            }
            break;

        case St::Done:
        default:
            GR_FAIL("sampah-pasca-akar");
        }

        i += jump;
        prev_pos = last_pos;
    }
#undef GR_FAIL

    // Loop sah akhiri dengan pop-akar; koma tergugur/akar terpotong
    // meninggalkan state != Done → tolak (fail4 / fail9 / fail33 kelas).
    if (st != St::Done) {
        if (out_pos) {
            *out_pos = prev_pos;
        }
        if (out_state) {
            *out_state = static_cast<int>(st);
        }
        return false;
    }

    // Sisa payload pasca entri terakhir wajib ws-only (sampah tanpa token
    // struktural — mis. angka — tertolak di sini).
    if (!region_ws_only(src, prev_pos + 1, src_size)) {
        if (out_pos) {
            *out_pos = prev_pos + 1;
        }
        if (out_state) {
            *out_state = static_cast<int>(St::Done);
        }
        return false;
    }
    return true;
}

[[nodiscard]] inline bool
validate_grammar_tapewalk(
    const char* const src, const size_t src_size,
    const uint64_t* const tape, const size_t tape_len) NOEXCEPT {
    return validate_grammar_tapewalk_dbg(
        src, src_size, tape, tape_len, nullptr, nullptr);
}

} // namespace grammar_replica
