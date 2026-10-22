#pragma once

// ============================================================================
//  ze_json.hpp — SURFACE API PUBLIK (kontrak ABI untuk export ke JS)
// ============================================================================
//  Mekanisme export: raw WebAssembly (tanpa glue Emscripten). Semua fungsi
//  di bawah adalah fungsi C ABI murni dan ditandai EXPORT_NAME sehingga
//  muncul di export section file .wasm hasil build preset wasm32/wasm64.
//
//  Layout linear memory yang diharapkan dari sisi JS (memory64):
//
//    [ payload JSON ][ tape ][ arena buffer ][ key ]
//      const char*    size_t×  bebas           size_t×
//
//    - payload : byte UTF-8 JSON mentah yang ditulis JS ke memory.
//    - tape    : array posisi struktural, kapasitas >= payload_size ENTRI
//                (1 entri = sizeof(types::size_t) byte).
//    - arena   : blok bebas untuk arena allocator (jar/unescape slow-path),
//                ukuran sesuai kebutuhan alokasi per-parsing.
//    - key     : nama field yang dituliskan JS sebelum memanggil find_field.
//
//  Catatan address space: pada target wasm64 semua pointer/sizes dari JS
//  berupa BigInt; pada wasm32 berupa Number.
// ============================================================================

#include <ze_json/core/error.hpp>
#include <ze_json/core/macros.hpp>
#include <ze_json/memory/arena.hpp>
#include <ze_json/ondemand/document.hpp>
#include <ze_json/ondemand/json_object.hpp>
#include <ze_json/types/string_view.hpp>

// ============================================================================
//  KONTRAK STATUS (ABI-stabil)
// ============================================================================

namespace zuu::api {

enum Status : types::int32_t {
    OK                  = 0,
    EMPTY_JSON          = 1,
    INVALID_JSON        = 2,
    UNEXPECTED_QUOTE    = 3,
    INVALID_QUOTE       = 4,
    INSUFFICIENT_BUFFER = 5,
    INVALID_UTF8        = 6,
};

// Pastikan pemetaan Status <-> JsonErrc tidak pernah salah katupan
static_assert(
    static_cast<types::int32_t>(core::JsonErrc::EmptyJson) == EMPTY_JSON &&
        static_cast<types::int32_t>(core::JsonErrc::InvalidJson) ==
            INVALID_JSON &&
        static_cast<types::int32_t>(core::JsonErrc::UnexpectedQuote) ==
            UNEXPECTED_QUOTE &&
        static_cast<types::int32_t>(core::JsonErrc::InvalidQuote) ==
            INVALID_QUOTE &&
        static_cast<types::int32_t>(core::JsonErrc::InsufficientBuffer) ==
            INSUFFICIENT_BUFFER &&
        static_cast<types::int32_t>(core::JsonErrc::InvalidUtf8) ==
            INVALID_UTF8,
    "ABI Status harus sinkron dengan core::JsonErrc");

// ============================================================================
//  KONTRAK TIPE NILAI (ABI-stabil)
// ============================================================================

enum class ValueType : types::int32_t {
    Array   = 0,
    Object  = 1,
    String  = 2,
    Number  = 3,
    Boolean = 4,
    Null    = 5,
};

// ============================================================================
//  SESSION — state machine on-demand satu dokumen JSON
// ============================================================================
//  Kontrak pemakaian dari JS:
//
//    session = create_session(payload, len, tape, cap, arena, &err)
//    if (!find_field(session, key_ptr, key_len)) { ... tidak ada ... }
//    switch (value_type(session)) {
//        case ValueType::Number :  i64 v = get_int(session); break; ...
//    }
//
//  1 Session = 1 root OBJECT (API minimal: traversal object top-level saja;
//    traversal array/iterator nested belum dijanjikan di versi ini).
//  Session dialokasikan dari arena — setelah selesai, panggil reset_arena()
//  untuk mengembalikan seluruh memori arena (termasuk Session) ke nol.
// ============================================================================

class Session {
  private:
    memory::Arena* arena_;
    ondemand::Document doc_;
    ondemand::JsonObject root_obj_;
    ondemand::JsonValue value_;
    bool found_;

  public:
    Session(
        const char* const RESTRICT src, const types::uint64_t* RESTRICT tape,
        types::size_t tape_len, memory::Arena* arena) NOEXCEPT
        : arena_(arena),
          doc_(src, tape, tape_len, arena),
          root_obj_(doc_.get_root().get_object()),
          value_(doc_.get_root()),
          found_(false) {}

    Session(const Session&) = delete;
    Session&
    operator=(const Session&) = delete;

    [[nodiscard]] constexpr bool
    found() const NOEXCEPT {
        return found_;
    }

    // Cari field; menggeser kursor internal (forward-only).
    void
    find(const types::StringView& key) NOEXCEPT {
        found_ = root_obj_.find_field_checked(key, &value_);
    }

    // Tipe nilai yang SEDANG dipegang kursor. Nilai angka tidak direkam tape,
    // sehingga deteksinya lewat karakter terminator (',' '}' ']' '\0');
    // karakter jenis lain langsung dipetakan lewat tabel struktural.
    [[nodiscard]] ValueType
    type() const NOEXCEPT {
        switch (value_.peek_char()) {
        case '{':
            return ValueType::Object;
        case '[':
            return ValueType::Array;
        case '\"':
            return ValueType::String;
        case 't':
        case 'f':
            return ValueType::Boolean;
        case 'n':
            return ValueType::Null;
        default:
            return ValueType::Number;
        }
    }

    [[nodiscard]] types::int64_t
    get_int() NOEXCEPT {
        return value_.get_int();
    }

    [[nodiscard]] double
    get_double() NOEXCEPT {
        return value_.get_double();
    }

    [[nodiscard]] types::StringView
    get_string() NOEXCEPT {
        return value_.get_string();
    }

    [[nodiscard]] bool
    get_bool() NOEXCEPT {
        return value_.get_bool();
    }
};

} // namespace zuu::api

// ============================================================================
//  C ABI — fitur arena (dikelola manual dari JS)
// ============================================================================

EXTERN_C_BEGIN

EXPORT_NAME(create_arena)
[[nodiscard]] zuu::memory::Arena*
create_arena(void* memory_ptr, zuu::types::size_t total_capacity) NOEXCEPT;

EXPORT_NAME(reset_arena)
void
reset_arena(zuu::memory::Arena* arena) NOEXCEPT;

EXPORT_NAME(destroy_arena)
void
destroy_arena(zuu::memory::Arena* arena) NOEXCEPT;

EXPORT_NAME(get_arena_used_space)
[[nodiscard]] zuu::types::size_t
get_arena_used_space(zuu::memory::Arena* arena) NOEXCEPT;

// ============================================================================
//  C ABI — scanner (langkah 0: validasi + rekam posisi struktural ke tape)
// ============================================================================

EXPORT_NAME(scanner8)
void
scanner8(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size,
    zuu::types::size_t& result, zuu::types::int32_t& error) NOEXCEPT;

#if __LITTLE_ENDIAN__ == 1
#if __IS_64_BIT__ == 1
EXPORT_NAME(scanner64)
void
scanner64(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size,
    zuu::types::size_t& result, zuu::types::int32_t& error) NOEXCEPT;
#elif __IS_64_BIT__ == 0
#else
EXPORT_NAME(scanner32)
void
scanner32(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::size_t* RESTRICT dst, zuu::types::size_t dst_size,
    zuu::types::size_t& result, zuu::types::int32_t& error) NOEXCEPT;
#endif // __IS_64_BIT
#endif // __LITTLE_ENDIAN__ == 1

// ============================================================================
//  C ABI — session API
// ============================================================================

// Scan sekaligus bangun Session. Mengembalikan nullptr jika gagal; kode
// error dituliskan ke `error` (lihat zuu::api::Status).
//
// Catatan: `tape` HANYA untuk hasil scanner; arena adalah blok memori
// terpisah. Jangan bi tape berada DI DALAM buffer arena (reset_arena akan
// menimpanya).
EXPORT_NAME(create_session)
[[nodiscard]] zuu::api::Session*
create_session(
    const char* const RESTRICT src, zuu::types::size_t src_size,
    zuu::types::uint64_t* RESTRICT tape, zuu::types::size_t tape_entries,
    zuu::memory::Arena* arena, zuu::types::int32_t& error) NOEXCEPT;

// Cari field pada object root. Mengembalikan 1 jika ketemu, 0 jika tidak
// (sesudah 0, kursor berada di akhir object — session ini habis dipakai,
// buat session baru untuk mencari ulang atau reset arena dan mulai lagi).
EXPORT_NAME(find_field)
[[nodiscard]] zuu::types::int32_t
find_field(
    zuu::api::Session* session, const char* const RESTRICT key,
    zuu::types::size_t key_len) NOEXCEPT;

// Tipe nilai pada kursor saat ini (hasil zuu::api::ValueType).
EXPORT_NAME(value_type)
[[nodiscard]] zuu::types::int32_t
value_type(zuu::api::Session* session) NOEXCEPT;

// Hanya valid ketika value_type == Number.
EXPORT_NAME(get_int)
[[nodiscard]] zuu::types::int64_t
get_int(zuu::api::Session* session) NOEXCEPT;

// Hanya valid ketika value_type == Number. Lemire exact fallback.
EXPORT_NAME(get_double)
[[nodiscard]] double
get_double(zuu::api::Session* session) NOEXCEPT;

// Hanya valid ketika value_type == Boolean. Mengembalikan 0/1.
EXPORT_NAME(get_bool)
[[nodiscard]] zuu::types::int32_t
get_bool(zuu::api::Session* session) NOEXCEPT;

// Zero-copy string: out_data menunjuk LANGSUNG ke memory payload; JS wajib
// menyalin (mis. TextDecoder dengan batas out_len) sebelum arena reset atau
// payload diganti. Hanya valid ketika value_type == String.
EXPORT_NAME(get_string)
void
get_string(
    zuu::api::Session* session, const char*& out_data,
    zuu::types::size_t& out_len) NOEXCEPT;

// Tidak melakukan apa-apa: lifecycle Session terkelola arena
// (reset_arena). Ada murni untuk simetri API dan kemudahan GC sisi JS.
EXPORT_NAME(destroy_session)
void
destroy_session(zuu::api::Session* session) NOEXCEPT;

EXTERN_C_END
