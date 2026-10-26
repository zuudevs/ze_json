// ============================================================================
//  scanner/parallel.cpp — GOTO-BASED AUTOMATON + SWAR (scanner64/scanner32)
// ============================================================================
//  Filosofi Langdale & Lemire 2019 §3.2: goto-based automaton dengan stack.
//  Scanner64 menambah SWAR body string (8-byte block) + UTF-8 terintegrasi.
// ============================================================================
#include <ze_json/core/macros.hpp>
#include <ze_json/lookups/utf8_dfa.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/utils/swar.hpp>
#include <ze_json/utils/utf8_validator.hpp>

namespace zuu::scanner {

namespace {

constexpr bool
utf8_step(types::uint32_t& state, const char* byte_ptr) NOEXCEPT {
    const types::uint8_t type =
        lookups::kUtf8Dfa[static_cast<types::uint8_t>(*byte_ptr)];
    state = lookups::kUtf8Dfa[256 + state + type];
    return state != 12;
}

// Peek struktural + UTF-8 DFA terintegrasi. Kembalikan '\xFF' bila UTF-8
// rusak (pemanggil mengecek dan return InvalidUtf8).
inline char
peek_structural(const char*& ptr, const char* end,
                types::uint32_t& utf8) NOEXCEPT {
    while (ptr < end) {
        if (utf8 != 0 || (*ptr & 0x80) != 0) {
            if (!utf8_step(utf8, ptr)) [[unlikely]] {
                return '\xFF'; // sentinel InvalidUtf8
            }
        }
        if (lookups::kStructural[static_cast<types::uint8_t>(*ptr)] != 0) {
            return *ptr;
        }
        ptr++;
    }
    return '\0';
}

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

inline bool
ws_only(const char* pos, const char* ptr) NOEXCEPT {
    for (const char* q = pos; q < ptr; ++q) {
        if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') {
            return false;
        }
    }
    return true;
}

// Scan body string: UTF-8 DFA terintegrasi + deteksi quote/backslash.
// WASM BUG: SWAR blok 8-byte (memcpy + mask + ctz) menghasilkan INVALID_UTF8
// palsu pada wasm64 untuk payload >64 KiB (native OK, scalar OK, SWAR
// tidak — flag+break pun gagal, manual byte build pun gagal).
// Sementara: loop scalar portabel. TODO: re-enable SWAR setelah akar
// masalah compiler wasm64 ditemukan (lihat artefak test_scanner_direct).
inline bool
scan_string_body(const char*& ptr, const char* end,
                types::uint32_t& utf8) NOEXCEPT {
    while (ptr < end) {
        if (utf8 != 0 || (*ptr & 0x80) != 0) {
            if (!utf8_step(utf8, ptr)) [[unlikely]] {
                return false;
            }
        }
        if (*ptr == '\\') {
            if (ptr + 1 >= end) [[unlikely]] {
                return false;
            }
            if (utf8 != 0 || (ptr[1] & 0x80) != 0) {
                if (!utf8_step(utf8, ptr + 1)) [[unlikely]] {
                    return false;
                }
            }
            ptr += 2;
            continue;
        }
        if (*ptr == '"') {
            return true;
        }
        ptr++;
    }
    return false;
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

    const char* ptr       = src;
    const char* const end = src + src_size;
    types::size_t* cursor           = dst;
    const types::size_t* end_cursor = dst + dst_size;

    ALIGNED(8) types::uint8_t return_stack[256];
    types::size_t return_depth = 0;
    types::uint32_t utf8        = 0;

#define RECORD()                                                             \
    do {                                                                     \
        if (!record_tape(cursor, end_cursor, ptr, src)) [[unlikely]] {         \
            return types::Unexpected{core::JsonErrc::InsufficientBuffer};     \
        }                                                                    \
    } while (0)

#define PEEK() peek_structural(ptr, end, utf8)

    goto doc_start;

doc_start:
    {
        const char c = PEEK();
        if (c == '\xFF') goto utf8_err;
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

obj_key:
    {
        const char c = PEEK();
        if (c == '\xFF') goto utf8_err;
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

obj_key_comma:
    {
        const char c = PEEK();
        if (c == '\xFF') goto utf8_err;
        if (c == '"') {
            RECORD();
            goto str_key;
        }
        goto error;
    }

str_key:
    {
        if (!scan_string_body(ptr, end, utf8)) {
            if (utf8 == 12) goto utf8_err;
            return types::Unexpected{core::JsonErrc::UnexpectedQuote};
        }
        RECORD();
        goto obj_colon;
    }

obj_colon:
    {
        const char c = PEEK();
        if (c == '\xFF') goto utf8_err;
        if (c == ':') {
            RECORD();
            goto obj_val;
        }
        goto error;
    }

obj_val:
    {
        const char* value_start = ptr;
        const char c            = PEEK();
        if (c == '\xFF') goto utf8_err;

        if (c == '"') {
            RECORD();
            goto str_obj_val;
        }
        if (c == '{') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] { goto error; }
            return_stack[return_depth++] = 1;
            goto obj_key;
        }
        if (c == '[') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] { goto error; }
            return_stack[return_depth++] = 1;
            goto arr_val;
        }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!ws_only(value_start, ptr)) [[unlikely]] { goto error; }
            RECORD();
            goto obj_next;
        }
        if (!ws_only(value_start, ptr)) {
            goto obj_next; // angka (0 entry)
        }
        goto error;
    }

str_obj_val:
    {
        if (!scan_string_body(ptr, end, utf8)) {
            if (utf8 == 12) goto utf8_err;
            return types::Unexpected{core::JsonErrc::UnexpectedQuote};
        }
        RECORD();
        goto obj_next;
    }

obj_next:
    {
        const char c = PEEK();
        if (c == '\xFF') goto utf8_err;
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

arr_val:
    {
        const char* value_start = ptr;
        const char c            = PEEK();
        if (c == '\xFF') goto utf8_err;

        if (c == ']') {
            if (ws_only(value_start, ptr)) {
                RECORD();
                goto close;
            }
            goto arr_next; // angka sebelum ']'
        }
        if (c == '"') {
            RECORD();
            goto str_arr_val;
        }
        if (c == '{') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] { goto error; }
            return_stack[return_depth++] = 2;
            goto obj_key;
        }
        if (c == '[') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] { goto error; }
            return_stack[return_depth++] = 2;
            goto arr_val;
        }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!ws_only(value_start, ptr)) [[unlikely]] { goto error; }
            RECORD();
            goto arr_next;
        }
        if (!ws_only(value_start, ptr)) {
            goto arr_next; // angka
        }
        goto error;
    }

str_arr_val:
    {
        if (!scan_string_body(ptr, end, utf8)) {
            if (utf8 == 12) goto utf8_err;
            return types::Unexpected{core::JsonErrc::UnexpectedQuote};
        }
        RECORD();
        goto arr_next;
    }

arr_val_comma:
    {
        const char* value_start = ptr;
        const char c            = PEEK();
        if (c == '\xFF') goto utf8_err;

        if (c == ']') {
            if (ws_only(value_start, ptr)) {
                goto error; // [1,] — trailing comma
            }
            goto arr_next; // angka sebelum ']'
        }
        if (c == '"') {
            RECORD();
            goto str_arr_val;
        }
        if (c == '{') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] { goto error; }
            return_stack[return_depth++] = 2;
            goto obj_key;
        }
        if (c == '[') {
            RECORD();
            if (return_depth >= 256) [[unlikely]] { goto error; }
            return_stack[return_depth++] = 2;
            goto arr_val;
        }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!ws_only(value_start, ptr)) [[unlikely]] { goto error; }
            RECORD();
            goto arr_next;
        }
        if (!ws_only(value_start, ptr)) {
            goto arr_next; // angka
        }
        goto error;
    }

arr_next:
    {
        const char c = PEEK();
        if (c == '\xFF') goto utf8_err;
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

close:
    {
        if (return_depth == 0) [[unlikely]] { goto error; }
        const types::uint8_t ret = return_stack[--return_depth];
        if (ret == 0) { goto done; }
        if (ret == 1) { goto obj_next; }
        if (ret == 2) { goto arr_next; }
        goto error;
    }

done:
    {
        for (const char* q = ptr; q < end; ++q) {
            if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') {
                goto error;
            }
        }
        if (utf8 != 0) [[unlikely]] { goto utf8_err; }
        return static_cast<types::size_t>(cursor - dst);
    }

utf8_err:
    return types::Unexpected{core::JsonErrc::InvalidUtf8};

error:
    return types::Unexpected{core::JsonErrc::InvalidJson};

#undef RECORD
#undef PEEK
}

#elif __IS_64_BIT__ == 0
// --- scanner32 — fallback LE 32-bit (mirror scanner8 goto; validator pass) ---
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

    const char* ptr       = src;
    const char* const end = src + src_size;
    types::size_t* cursor           = dst;
    const types::size_t* end_cursor = dst + dst_size;

    ALIGNED(8) types::uint8_t return_stack[256];
    types::size_t return_depth = 0;

#define RECORD()                                                             \
    do {                                                                     \
        if (cursor >= end_cursor) [[unlikely]] {                              \
            return types::Unexpected{core::JsonErrc::InsufficientBuffer};     \
        }                                                                    \
        *cursor++ = static_cast<types::size_t>(ptr - src);                    \
        ptr++;                                                                \
    } while (0)

    auto peek = [&ptr, end]() NOEXCEPT {
        while (ptr < end &&
               lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
            ptr++;
        }
        return (ptr < end) ? *ptr : '\0';
    };

    goto doc_start;

doc_start:
    {
        const char c = peek();
        if (c == '{') { RECORD(); return_stack[return_depth++] = 0; goto obj_key; }
        if (c == '[') { RECORD(); return_stack[return_depth++] = 0; goto arr_val; }
        goto error;
    }

obj_key:
    {
        const char c = peek();
        if (c == '"') { RECORD(); goto str_key; }
        if (c == '}') { RECORD(); goto close; }
        goto error;
    }

obj_key_comma:
    {
        const char c = peek();
        if (c == '"') { RECORD(); goto str_key; }
        goto error;
    }

str_key:
    {
        while (ptr < end) {
            if (*ptr == '\\') { if (ptr + 1 >= end) goto quote_err; ptr += 2; continue; }
            if (*ptr == '"') break;
            ptr++;
        }
        if (ptr >= end) goto quote_err;
        RECORD();
        goto obj_colon;
    }

obj_colon:
    {
        if (peek() == ':') { RECORD(); goto obj_val; }
        goto error;
    }

obj_val:
    {
        const char* value_start = ptr;
        const char c = peek();
        if (c == '"') { RECORD(); goto str_obj_val; }
        if (c == '{') { RECORD(); if (return_depth >= 256) goto error; return_stack[return_depth++] = 1; goto obj_key; }
        if (c == '[') { RECORD(); if (return_depth >= 256) goto error; return_stack[return_depth++] = 1; goto arr_val; }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!ws_only(value_start, ptr)) goto error;
            RECORD(); goto obj_next;
        }
        if (!ws_only(value_start, ptr)) goto obj_next;
        goto error;
    }

str_obj_val:
    {
        while (ptr < end) {
            if (*ptr == '\\') { if (ptr + 1 >= end) goto quote_err; ptr += 2; continue; }
            if (*ptr == '"') break;
            ptr++;
        }
        if (ptr >= end) goto quote_err;
        RECORD();
        goto obj_next;
    }

obj_next:
    {
        const char c = peek();
        if (c == ',') { RECORD(); goto obj_key_comma; }
        if (c == '}') { RECORD(); goto close; }
        goto error;
    }

arr_val:
    {
        const char* value_start = ptr;
        const char c = peek();
        if (c == ']') {
            if (ws_only(value_start, ptr)) { RECORD(); goto close; }
            goto arr_next;
        }
        if (c == '"') { RECORD(); goto str_arr_val; }
        if (c == '{') { RECORD(); if (return_depth >= 256) goto error; return_stack[return_depth++] = 2; goto obj_key; }
        if (c == '[') { RECORD(); if (return_depth >= 256) goto error; return_stack[return_depth++] = 2; goto arr_val; }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!ws_only(value_start, ptr)) goto error;
            RECORD(); goto arr_next;
        }
        if (!ws_only(value_start, ptr)) goto arr_next;
        goto error;
    }

str_arr_val:
    {
        while (ptr < end) {
            if (*ptr == '\\') { if (ptr + 1 >= end) goto quote_err; ptr += 2; continue; }
            if (*ptr == '"') break;
            ptr++;
        }
        if (ptr >= end) goto quote_err;
        RECORD();
        goto arr_next;
    }

arr_val_comma:
    {
        const char* value_start = ptr;
        const char c = peek();
        if (c == ']') {
            if (ws_only(value_start, ptr)) goto error;
            goto arr_next;
        }
        if (c == '"') { RECORD(); goto str_arr_val; }
        if (c == '{') { RECORD(); if (return_depth >= 256) goto error; return_stack[return_depth++] = 2; goto obj_key; }
        if (c == '[') { RECORD(); if (return_depth >= 256) goto error; return_stack[return_depth++] = 2; goto arr_val; }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!ws_only(value_start, ptr)) goto error;
            RECORD(); goto arr_next;
        }
        if (!ws_only(value_start, ptr)) goto arr_next;
        goto error;
    }

arr_next:
    {
        const char c = peek();
        if (c == ',') { RECORD(); goto arr_val_comma; }
        if (c == ']') { RECORD(); goto close; }
        goto error;
    }

close:
    {
        if (return_depth == 0) goto error;
        const types::uint8_t ret = return_stack[--return_depth];
        if (ret == 0) goto done;
        if (ret == 1) goto obj_next;
        if (ret == 2) goto arr_next;
        goto error;
    }

done:
    {
        for (const char* q = ptr; q < end; ++q) {
            if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') goto error;
        }
        return static_cast<types::size_t>(cursor - dst);
    }

quote_err:
    return types::Unexpected{core::JsonErrc::UnexpectedQuote};

error:
    return types::Unexpected{core::JsonErrc::InvalidJson};

#undef RECORD
}
#endif // __IS_64_BIT__
#endif // __LITTLE_ENDIAN__

} // namespace zuu::scanner
