// ============================================================================
//  scanner/scalar.cpp — GOTO-BASED AUTOMATON (scanner8, portabel)
// ============================================================================
//  Filosofi Langdale & Lemire 2019 §3.2 (branchfree.org):
//  "Stage 2 operates as a goto-based automata with a stack."
//
//  Kontrol aliran EKSPLISIT via goto — mengeliminasi misprediksi percabangan
//  pada jalur panas; stack menyimpan state return untuk kontainer bersarang.
//  Automaton memvalidasi GRAMATIKAL JSON selama pemindaian (kunci wajib
//  berkutip, nilai wajib setelah ':', koma wajib antar member, penutup
//  wajib cocok JENIS dan URUTAN).
//
//  STRATEGI UTF-8: pass validator TERPISAH (utils::Utf8Validator —
//  SWAR blok 8-byte ASCII murni, DFA per byte hanya non-ASCII).
//  Loop skalar tidak untung dari DFA inline (terukur -27..43% Tahap 4).
// ============================================================================
#include <ze_json/core/macros.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/utils/utf8_validator.hpp>

namespace zuu::scanner {

namespace {

// Advance ptr ke karakter struktural berikutnya; kembalikan karakter
// ('\0' bila payload habis). Byte non-struktural (whitespace, angka)
// dilewati tanpa inspeksi.
inline char
peek_structural(const char*& ptr, const char* end) NOEXCEPT {
    while (ptr < end &&
           lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
        ptr++;
    }
    return (ptr < end) ? *ptr : '\0';
}

// Rekam posisi byte struktural ke tape; majukan ptr.
// Kembali false bila buffer tape penuh.
inline bool
record_tape(types::size_t*& cursor, const types::size_t* end_cursor,
            const char*& ptr, const char* src) NOEXCEPT {
    if (cursor >= end_cursor) [[unlikely]] {
        return false;
    }
    *cursor++ = static_cast<types::size_t>(ptr - src);
    ptr++;
    return true;
}

// True bila SEMUA byte [pos, ptr) adalah whitespace JSON.
inline bool
ws_only(const char* pos, const char* ptr) NOEXCEPT {
    for (const char* q = pos; q < ptr; ++q) {
        if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') {
            return false;
        }
    }
    return true;
}

} // namespace

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

    const char* ptr       = src;
    const char* const end = src + src_size;

    types::size_t* cursor           = dst;
    const types::size_t* end_cursor = dst + dst_size;

    // Stack kontainer: state return setelah kontainer ditutup.
    // 0 = selesai (root), 1 = obj_next (harap ','/'}'), 2 = arr_next
    // (harap ','/']').  ALIGNED(8) untuk akses efisien.
    ALIGNED(8) types::uint8_t return_stack[256];
    types::size_t return_depth = 0;

#define RECORD()                                                             \
    do {                                                                     \
        if (!record_tape(cursor, end_cursor, ptr, src)) [[unlikely]] {         \
            return types::Unexpected{core::JsonErrc::InsufficientBuffer};     \
        }                                                                    \
    } while (0)

    goto doc_start;

    // --- DOKUMEN: root wajib object atau array --------------------------
doc_start:
    {
        const char c = peek_structural(ptr, end);
        if (c == '{') {
            RECORD();
            return_stack[return_depth++] = 0;
            goto obj_key;
        }
        if (c == '[') {
            RECORD();
            return_stack[return_depth++] = 0;
            goto arr_val;
        }
        goto error;
    }

    // --- OBJECT: kunci pertama atau '}' (object kosong) -------------------
obj_key:
    {
        const char c = peek_structural(ptr, end);
        if (c == '"') {
            RECORD();
            goto str_key;
        }
        if (c == '}') {
            RECORD();
            goto close;
        }
        goto error;
    }

    // --- OBJECT: kunci setelah ',' ('}' = koma ekstra = error) ------------
obj_key_comma:
    {
        const char c = peek_structural(ptr, end);
        if (c == '"') {
            RECORD();
            goto str_key;
        }
        // '{' '}' ',' di sini = koma ekstra / penutup tanpa kunci.
        goto error;
    }

str_key:
    // Body string kunci → harap ':' setelahnya.
    {
        while (ptr < end) {
            if (*ptr == '\\') {
                if (ptr + 1 >= end) [[unlikely]] {
                    return types::Unexpected{core::JsonErrc::UnexpectedQuote};
                }
                ptr += 2;
                continue;
            }
            if (*ptr == '"') {
                break;
            }
            ptr++;
        }
        if (ptr >= end) [[unlikely]] {
            return types::Unexpected{core::JsonErrc::UnexpectedQuote};
        }
        RECORD(); // penutup kunci
        goto obj_colon;
    }

obj_colon:
    {
        const char c = peek_structural(ptr, end);
        if (c == ':') {
            RECORD();
            goto obj_val;
        }
        goto error;
    }

    // --- OBJECT: nilai setelah ':' ---------------------------------------
obj_val:
    {
        const char* value_start = ptr;
        const char c            = peek_structural(ptr, end);

        if (c == '"') {
            RECORD();
            goto str_obj_val;
        }
        if (c == '{') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] {
                goto error;
            }
            return_stack[return_depth++] = 1;
            goto obj_key;
        }
        if (c == '[') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] {
                goto error;
            }
            return_stack[return_depth++] = 1;
            goto arr_val;
        }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!ws_only(value_start, ptr)) [[unlikely]] {
                goto error; // pseudo-structural: garbage sebelum literal
            }
            RECORD();
            goto obj_next;
        }
        // Angka: byte non-struktural antara ':' dan sini.
        // Semua whitespace → tidak ada nilai → error.
        if (!ws_only(value_start, ptr)) {
            goto obj_next; // angka (0 entry tape)
        }
        goto error; // {"a":,} atau {"a": ,} — tidak ada nilai
    }

str_obj_val:
    {
        while (ptr < end) {
            if (*ptr == '\\') {
                if (ptr + 1 >= end) [[unlikely]] {
                    return types::Unexpected{core::JsonErrc::UnexpectedQuote};
                }
                ptr += 2;
                continue;
            }
            if (*ptr == '"') {
                break;
            }
            ptr++;
        }
        if (ptr >= end) [[unlikely]] {
            return types::Unexpected{core::JsonErrc::UnexpectedQuote};
        }
        RECORD();
        goto obj_next;
    }

    // --- OBJECT: ',' menuju kunci berikut, '}' menutup -------------------
obj_next:
    {
        const char c = peek_structural(ptr, end);
        if (c == ',') {
            RECORD();
            goto obj_key_comma;
        }
        if (c == '}') {
            RECORD();
            goto close;
        }
        goto error;
    }

    // --- ARRAY: nilai pertama atau ']' (array kosong) ---------------------
arr_val:
    {
        const char* value_start = ptr;
        const char c            = peek_structural(ptr, end);

        if (c == ']') {
            if (ws_only(value_start, ptr)) {
                RECORD(); // array kosong []
                goto close;
            }
            // Nilai tunggal (angka) diikuti ']' — goto arr_next
            // yang akan merekam ']' dan menutup.
            goto arr_next;
        }

        if (c == '"') {
            RECORD();
            goto str_arr_val;
        }
        if (c == '{') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] {
                goto error;
            }
            return_stack[return_depth++] = 2;
            goto obj_key;
        }
        if (c == '[') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] {
                goto error;
            }
            return_stack[return_depth++] = 2;
            goto arr_val;
        }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!ws_only(value_start, ptr)) [[unlikely]] {
                goto error;
            }
            RECORD();
            goto arr_next;
        }
        if (!ws_only(value_start, ptr)) {
            goto arr_next; // angka (0 entry tape)
        }
        goto error; // [whitespace] atau [,]
    }

str_arr_val:
    {
        while (ptr < end) {
            if (*ptr == '\\') {
                if (ptr + 1 >= end) [[unlikely]] {
                    return types::Unexpected{core::JsonErrc::UnexpectedQuote};
                }
                ptr += 2;
                continue;
            }
            if (*ptr == '"') {
                break;
            }
            ptr++;
        }
        if (ptr >= end) [[unlikely]] {
            return types::Unexpected{core::JsonErrc::UnexpectedQuote};
        }
        RECORD();
        goto arr_next;
    }

    // --- ARRAY: nilai setelah ',' (']' = koma ekstra = error) --------------
arr_val_comma:
    {
        const char* value_start = ptr;
        const char c            = peek_structural(ptr, end);

        if (c == ']') {
            if (ws_only(value_start, ptr)) {
                goto error; // [1,] — koma ekstra TANPA nilai
            }
            // Angka sebelum ']' — nilai valid; goto arr_next untuk menutup.
            goto arr_next;
        }

        if (c == '"') {
            RECORD();
            goto str_arr_val;
        }
        if (c == '{') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] {
                goto error;
            }
            return_stack[return_depth++] = 2;
            goto obj_key;
        }
        if (c == '[') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] {
                goto error;
            }
            return_stack[return_depth++] = 2;
            goto arr_val;
        }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!ws_only(value_start, ptr)) [[unlikely]] {
                goto error;
            }
            RECORD();
            goto arr_next;
        }
        if (!ws_only(value_start, ptr)) {
            goto arr_next; // angka
        }
        goto error; // [1, ,] — tidak ada nilai setelah koma
    }

    // --- ARRAY: ',' menuju nilai berikut, ']' menutup ---------------------
arr_next:
    {
        const char c = peek_structural(ptr, end);
        if (c == ',') {
            RECORD();
            goto arr_val_comma;
        }
        if (c == ']') {
            RECORD();
            goto close;
        }
        goto error;
    }

    // --- PENUTUP KONTAINER: pop stack, kembali ke state parent -------------
close:
    {
        if (return_depth == 0) [[unlikely]] {
            goto error;
        }
        const types::uint8_t ret = return_stack[--return_depth];
        if (ret == 0) {
            goto done;
        }
        if (ret == 1) {
            goto obj_next;
        }
        if (ret == 2) {
            goto arr_next;
        }
        goto error;
    }

    // --- SELESAI: payload sisa wajib whitespace-only -----------------------
done:
    {
        for (const char* q = ptr; q < end; ++q) {
            if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') {
                goto error; // trailing garbage (fail7/fail10)
            }
        }
        return static_cast<types::size_t>(cursor - dst);
    }

error:
    return types::Unexpected{core::JsonErrc::InvalidJson};

#undef RECORD
}

} // namespace zuu::scanner
