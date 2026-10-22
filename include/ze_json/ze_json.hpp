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
    // Status pencarian path (get_*_at):
    NOT_FOUND       = 7,
    TYPE_MISMATCH   = 8,
    INVALID_PATH    = 9,
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
//  1 Session = 1 root OBJECT (kursor forward-only). Nilai bertipe Array
//  dapat ditraversi satu level:
//
//    n = array_size(session)      // valid saat kursor tepat di '['
//    while (array_next(session)) {
//        switch (value_type(session)) { ... }   // nilai elemen sekarang
//    }
//
//  Elemen yang tidak dibaca getter di-skip otomatis oleh array_next.
//  PENTING (drain): array_next HARUS terus dipanggil sampai mengembalikan
//  0 — langkah drain terakhir itu yang melewatkan kursor keluar dari ']'
//  menuju separator root. find() MENGAMPUNI kursor ']' sisa bila elemen
//  terakhir sudah habis dibaca, tetapi drain tetap disarankan.
//  Session dialokasikan dari arena — setelah selesai, panggil reset_arena()
//  untuk mengembalikan seluruh memori arena (termasuk Session) ke nol.
// ============================================================================

class Session {
  private:
    ondemand::Document doc_;
    ondemand::JsonObject root_obj_;
    ondemand::JsonValue value_;
    bool found_;

    // ---- State traversal array (satu kursor forward-only) ----
    // Sisa elemen yang belum diekspos array_next(); 0 = tidak sedang
    // berada di dalam array.
    types::size_t array_remaining_{0};
    // Elemen yang sedang dipegang kursor BELUM dibaca getter: true berarti
    // elemen sudah diekspos (array_next) tetapi belum dikonsumsi, sehingga
    // array_next berikutnya harus me-skip()-nya dulu.
    bool value_pending_{false};

  public:
    Session(
        const char* const RESTRICT src, const types::uint64_t* RESTRICT tape,
        types::size_t tape_len, memory::Arena* arena) NOEXCEPT
        : doc_(src, tape, tape_len, arena),
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

    // ------------------------------------------------------------------
    //  PATH — pencarian + pembacaan dalam SATU lapis pemanggilan C ABI
    // ------------------------------------------------------------------
    // Sintaks path (subset JSONPath, byte tegas):
    //   key     : segmen sampai '.' atau '[' — pencocokan byte-exact
    //             terhadap teks kunci mentah di payload (kunci JSON yang
    //             berisi escape tidak akan cocok — batasan yang sama
    //             dengan find_field)
    //   [index] : indeks array, digit murni
    // Contoh   : "position.x", "colors[1]", "items[0].name"
    //
    // Setiap panggilan ME-REWIND kursor ke root lalu menelusuri dari awal —
    // kueri independen, biaya O(panjang path). Hasil: kursor tepat
    // di nilai terpilih dan SIAP dibaca lewat value_type/get_*.
    // CATATAN: setelah find_path, array_next/array_size TIDAK berlanjut
    // (penghitung array tidak relevan) — buat session baru atau pakai path
    // lebih spesifik untuk menjangkau elemen lain.
    [[nodiscard]] types::int32_t
    find_path(const char* const path, types::size_t len) NOEXCEPT {
        doc_.rewind();
        value_pending_   = false;
        array_remaining_ = 0;

        if (!path || len == 0) [[unlikely]] {
            return INVALID_PATH;
        }

        types::size_t i = 0;
        while (i < len) {
            if (path[i] == '[') {
                // --- segmen [index] ------------------------------------
                ++i; // masuk ke isinya
                types::size_t idx = 0;
                while (i < len && path[i] >= '0' && path[i] <= '9') {
                    const int d = path[i] - '0';
                    if (idx > (static_cast<types::size_t>(-1) - d) / 10) {
                        return INVALID_PATH;
                    }
                    idx = idx * 10 + d;
                    ++i;
                }
                if (i >= len || path[i] != ']') [[unlikely]] {
                    return INVALID_PATH;
                }
                ++i; // lintas ']'

                if (value_.peek_char() != '[') {
                    return NOT_FOUND;
                }
                value_.get_array(); // habiskan '[' → kursor di elemen pertama
                if (!position_at_index(idx)) {
                    return NOT_FOUND;
                }
            } else {
                // --- segmen key ----------------------------------------
                const types::size_t start = i;
                while (i < len && path[i] != '.' && path[i] != '[') {
                    ++i;
                }
                if (start == i) [[unlikely]] {
                    // leading '.', '..', atau trailing '.'
                    return INVALID_PATH;
                }

                if (value_.peek_char() != '{') {
                    return NOT_FOUND;
                }
                ondemand::JsonObject obj = value_.get_object();
                if (!obj.find_field_checked(
                        types::StringView(path + start, i - start),
                        &value_)) {
                    return NOT_FOUND;
                }
            }

            if (i < len && path[i] == '.') {
                ++i; // lintas '.' setelah segmen
            }
        }

        value_pending_ = true;
        return OK;
    }

  private:
    // Posisikan kursor di elemen ke-`index` array (kursor saat ini harus
    // sudah pasca-'['). Kembali true bila indeks valid; elemen 0..index-1
    // di-skip utuh (angka 0 entry dilintasi lewat kursor di terminatornya).
    [[nodiscard]] bool
    position_at_index(types::size_t index) NOEXCEPT {
        types::size_t i = 0;
        for (;;) {
            char c = value_.peek_char();
            if (c == '\0') [[unlikely]] {
                return false;
            }
            if (c == ']') {
                // ']': habis, atau terminator elemen ANGKA terakhir.
                return i == index ? value_.has_inline_number() : false;
            }
            if (i == index) {
                return true; // elemen sedang dipegang kursor
            }
            ++i;

            if (c == ',') {
                // angka: lintas terminator/separator-nya ke elemen berikut
                value_.skip();
            } else {
                value_.skip(); // habiskan elemen string/literal/kontainer
                if (value_.peek_char() == ',') {
                    value_.skip();
                }
            }
        }
    }

  public:
    // Cari field; menggeser kursor internal (forward-only).
    void
    find(const types::StringView& key) NOEXCEPT {
        // Pengampunan kursor: kursor ']'-sisa muncul bila array sebelumnya
        // habis dibaca getter pada elemen terakhir tanpa drain array_next
        // (yang normalnya melewatkannya). Cukup satu langkah keluar.
        if (value_.peek_char() == ']') [[unlikely]] {
            value_.skip();
        }

        found_         = root_obj_.find_field_checked(key, &value_);
        value_pending_ = false;
    }

    // ------------------------------------------------------------------
    //  Traversal array (satu level, kursor forward-only)
    // ------------------------------------------------------------------

    // Valid SAAT kursor tepat menunjuk '[' (hasil find_field bertipe
    // Array). Menghabiskan '[' dan menghitung jumlah elemen top-level;
    // penghitungan inspektif (snapshot & restore) sehingga kursor kembali
    // tepat ke posisi elemen pertama.
    [[nodiscard]] types::size_t
    array_size() NOEXCEPT {
        if (value_.peek_char() != '[') [[unlikely]] {
            array_remaining_ = 0;
            return 0;
        }

        ondemand::JsonArray arr = value_.get_array();
        array_remaining_        = arr.count_elements();        value_pending_                = false;
        return array_remaining_;
    }

    // Maju ke elemen berikutnya dan mendelegasikan kursor ke atasnya.
    // Mengembalikan 1 saat elemen tersedia (nilai siap dibaca lewat
    // value_type/get_*), 0 saat array habis (kursor kembali ke ritme
    // object root: ',' / '}' — find_field tetap valid). Elemen yang tidak
    // dibaca getter di-skip otomatis oleh array_next.
    [[nodiscard]] types::int32_t
    array_next() NOEXCEPT {
        if (array_remaining_ == 0) {
            value_pending_ = false;

            // Menuntun kursor kembali ke ritme object root:
            //   ']'   → array kosong ATAU elemen terakhir sudah dibaca
            //           hingga terminatornya → satu langkah keluar.
            //   lain  → elemen terakhir belum dibaca → habiskan elemen,
            //           lalu keluar dari ']' bila berhenti di sana.
            //   '\0'  → kursor sudah melampaui ']' — tidak perlu apa-apa.
            char c = value_.peek_char();
            if (c == ']') {
                value_.skip();
            } else if (c != '\0') {
                value_.skip();
                if (value_.peek_char() == ']') {
                    value_.skip();
                }
            }
            return 0;
        }

        array_remaining_--;

        if (value_pending_) {
            // Elemen sebelumnya diekspos tetapi tidak dibaca → habiskan.
            char c = value_.peek_char();
            if (c == ',') {
                // Angka: kursor sudah di terminator sekaligus separator —
                // satu langkah tepat ke posisi elemen berikutnya.
                value_.skip();
            } else if (c != ']' && c != '\0') {
                value_.skip(); // string/literal/kontainer
                if (value_.peek_char() == ',') {
                    value_.skip(); // lewati separator
                }
            }
        } else if (value_.peek_char() == ',') {
            // ',' pada kursor bisa berarti (a) terminator elemen ANGKA yang
            // sedang ter-expose (angka 0 entry), atau (b) separator sisa
            // konsumsi elemen non-angka — yang harus dilintasi. Gap teks
            // berawalan digit/tanda = kasus (a); gap "rue"/"" = kasus (b).
            if (!value_.has_inline_number()) {
                value_.skip();
            }
        }

        value_pending_ = true;
        return 1;
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
        const auto result = value_.get_int();
        value_pending_    = false;
        return result;
    }

    [[nodiscard]] double
    get_double() NOEXCEPT {
        const auto result = value_.get_double();
        value_pending_    = false;
        return result;
    }

    [[nodiscard]] types::StringView
    get_string() NOEXCEPT {
        const auto result = value_.get_string();
        value_pending_    = false;
        return result;
    }

    [[nodiscard]] bool
    get_bool() NOEXCEPT {
        const auto result = value_.get_bool();
        value_pending_    = false;
        return result;
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

// Hitung jumlah elemen top-level array pada nilai kursor. Valid HANYA
// saat value_type == Array (kursor tepat di '[') dan belum pernah
// array_size dipanggil untuk array ini. Kursor kemudian berada di posisi
// elemen pertama — array_size tidak bisa dipanggil ulang.
EXPORT_NAME(array_size)
[[nodiscard]] zuu::types::size_t
array_size(zuu::api::Session* session) NOEXCEPT;

// Maju ke elemen array berikutnya: 1 = elemen tersedia (kursor di
// posisi elemen, nilai bisa dibaca lewat value_type/get_*); 0 = habis
// (kursor di ']') — find_field berikutnya tetap valid. Elemen yang tidak
// dibaca getter di-skip otomatis.
EXPORT_NAME(array_next)
[[nodiscard]] zuu::types::int32_t
array_next(zuu::api::Session* session) NOEXCEPT;

// ============================================================================
//  C ABI — path query (SATU panggilan wasm: navigasi penuh + baca nilai)
// ============================================================================
//  Sintaks path: "position.x", "colors[1]", "items[0].name".
//  Setiap kueri me-rewind kursor ke root dan menelusuri ulang di C++
//  (O(panjang path), skala nanosekon) — JS tidak perlu menjejaki kursor
//  per-langkah lagi. Kode status dari zuu::api::Status dituliskan ke
//  out-param `error`: OK / NOT_FOUND / TYPE_MISMATCH / INVALID_PATH.
//
//  find_path hanya men-POSISIKAN kursor; nilainya dibaca sesudahnya lewat
//  value_type/get_*. Varian get_*_at melakukan navigasi + cek tipe + baca
//  dalam SATU panggilan yang sama.
// ============================================================================

// Navigasi path. Mengembalikan zuu::api::OK bila kursor berada tepat di
// nilai terpilih (harga read sesudahnya), atau NOT_FOUND / INVALID_PATH.
EXPORT_NAME(find_path)
[[nodiscard]] zuu::types::int32_t
find_path(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len) NOEXCEPT;

// Hanya valid bila nilai path bertipe Number (integer). Mengembalikan 0
// bila gagal (lihat `error_out`).
EXPORT_NAME(get_int_at)
[[nodiscard]] zuu::types::int64_t
get_int_at(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len, zuu::types::int32_t& error_out) NOEXCEPT;

// Hanya valid bila nilai path bertipe Number (integer atau pecahan).
EXPORT_NAME(get_double_at)
[[nodiscard]] double
get_double_at(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len, zuu::types::int32_t& error_out) NOEXCEPT;

// Hanya valid bila nilai path bertipe Boolean. Mengembalikan 0/1.
EXPORT_NAME(get_bool_at)
[[nodiscard]] zuu::types::int32_t
get_bool_at(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len, zuu::types::int32_t& error_out) NOEXCEPT;

// Zero-copy: out_data menunjuk LANGSUNG ke memory payload / arena.
// Hanya valid bila nilai path bertipe String.
EXPORT_NAME(get_string_at)
void
get_string_at(
    zuu::api::Session* session, const char* const RESTRICT path,
    zuu::types::size_t path_len, const char*& out_data,
    zuu::types::size_t& out_len, zuu::types::int32_t& error_out) NOEXCEPT;

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
