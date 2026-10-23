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
#include <ze_json/utils/hash_fnv1a.hpp>

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
    NOT_FOUND     = 7,
    TYPE_MISMATCH = 8,
    INVALID_PATH  = 9,
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
//  dapat ditraversi satu level — DUA pola:
//
//  (a) Counted (kontrak lama):
//    n = array_size(session)      // valid saat kursor tepat di '['
//    while (array_next(session)) {
//        switch (value_type(session)) { ... }   // nilai elemen sekarang
//    }
//
//  (b) Streaming (tanpa array_size — satu lintasan, tanpa pass ganda):
//    while (array_next(session)) {
//        switch (value_type(session)) { ... }
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
    // Mode mesin iterasi array: Counted = kontrak lama (array_size wajib,
    // counter otoritatif); Streaming = tanpa array_size (array_next satu
    // lintasan, habis dideteksi dari kursor ']' — tidak ada pass ganda).
    enum class ArrayIterMode : types::uint8_t {
        None,
        Counted,
        Streaming,
    };

    // Sisa elemen yang belum diekspos array_next(); 0 = tidak sedang
    // berada di dalam array (hanya bermakna pada mode Counted).
    types::size_t array_remaining_{0};
    ArrayIterMode array_mode_{ArrayIterMode::None};
    // True saat elemen ANGKA TERAKHIR sudah diekspos (mode streaming):
    // getter angka memindahkan kursor MELAMPAUI ']' sebelum array_next
    // berikutnya tiba, sehingga tanpa penanda ini kursor tidak bisa
    // dibedakan dari ritme object root (fantom elemen). Dipaksa hanya pada
    // cabang streaming.
    bool array_final_number_{false};
    // Elemen yang sedang dipegang kursor BELUM dibaca getter: true berarti
    // elemen sudah diekspos (array_next) tetapi belum dikonsumsi, sehingga
    // array_next berikutnya harus me-skip()-nya dulu.
    bool value_pending_{false};

    // ---- Indeks root object (lazy) ------------------------------------
    // Klaim analisis: pencarian field sekuensial member+posisi O(width) —
    // kunci terakhir pada object 1024 field menyapu semua member. Kebijakan:
    // kueri path BERAT pertama tetap sekuensial (tanpa regresi, kasus
    // pemakai sekali-baca), kueri berat kedua memicu pembangunan indeks
    // penuh (sekali), kueri berikutnya O(1) + verifikasi kunci byte-exact.
    // Lihat resolve_root_key().
    static constexpr types::size_t kIdxQuota = 24;

    struct RootFieldSlot {
        types::uint64_t hash;    // FNV-1a 64-bit teks kunci mentah
        types::uint64_t key_idx; // entry tape quote pembuka kunci
        types::uint64_t val_idx; // entry tape SAAT nilai mulai (pasca ':')
    };

    RootFieldSlot* idx_slots_{nullptr}; // null = indeks belum dibangun
    types::size_t idx_count_{0};
    types::size_t idx_mask_{0}; // kapasitas open-addressing = mask + 1
    types::uint64_t heavy_queries_{0};
    bool idx_disabled_{false};
    // Kunci ber-escape TIDAK terindeks (pencocokan sekuensial membandingkan
    // hasil unescape — teks mentah tidak ekuivalen). Bila ada kandidatnya,
    // miss indeks wajib jatuh ke sekuensial agar semantik tidak berubah.
    bool idx_has_escape_keys_{false};

    // ---- Indeks elemen array (lazy) — segmen [index] path query --------
    // Hukum yang sama dengan indeks root: position_at_index menyapu O(index)
    // elemen PER kueri (find_path me-rewind dari root). Kebijakan identik:
    // kueri berat pertama sekuensial murni, kueri berat KEDUA membangun
    // tabel posisi (dense: elemen ke-i → tape idx kursor nilainya, identik
    // dengan hasil position_at_index), kueri berikutnya lompat O(1). Tabel
    // terikat identitas array (tape idx '['); array lain memicu rebuild
    // (blok arena dipakai ulang selama kapasitas cukup).
    static constexpr types::size_t kArrIdxQuota = 24;

    types::size_t* arr_elem_idx_{nullptr}; // dense: elemen → tape idx
    types::size_t arr_elem_count_{0};      // jumlah elemen terindeks
    types::size_t arr_elem_capacity_{0};   // kapasitas blok (entri)
    types::size_t arr_elem_source_{0};     // identitas array (idx '[')
    types::uint64_t arr_heavy_queries_{0};
    bool arr_idx_present_{false};  // tabel berlaku utk arr_elem_source_
    bool arr_idx_disabled_{false};
    memory::Arena* arena_{nullptr};

  public:
    Session(
        const char* const RESTRICT src, const types::uint64_t* RESTRICT tape,
        types::size_t tape_len, memory::Arena* arena) NOEXCEPT
        : doc_(src, tape, tape_len, arena),
          root_obj_(doc_.get_root().get_object()),
          value_(doc_.get_root()),
          found_(false) {
        arena_ = arena;
    }

    Session(const Session&) = delete;
    Session&
    operator=(const Session&) = delete;

    [[nodiscard]] constexpr bool
    found() const NOEXCEPT {
        return found_;
    }

  private:
    // ------------------------------------------------------------------
    //  INDEKS ROOT OBJECT (LAZY) — kebijakan, pembangunan, dan lookup
    // ------------------------------------------------------------------
    // Kebijakan: kueri path BERAT (menyapu > kIdxQuota member root) yang
    // pertama tetap sekuensial murni, kueri berat KEDUA membangun indeks
    // penuh, kueri berikutnya O(1) + verifikasi kunci byte-exact. Kunci
    // mentah (teks tape) disimpan apa adanya — batasan "kunci beranimalan
    // escape tidak akan cocok" sama persis dengan pencarian sekuensial.
    //
    // Target: object lebar yang DIBACA BERKALI-KALI (pola path query
    // berulang). Object kecil & pemakaian sekali-baca tidak pernah
    // membayar biaya indeks sama sekali.
    [[nodiscard]] bool
    resolve_root_key(const types::StringView& key) NOEXCEPT {
        if (idx_slots_) [[likely]] {
            if (lookup_root_key(key)) {
                return true;
            }
            // Miss indeks: keputusan final O(1) HANYA bila semua kunci root
            // terindeks (bebas escape). Kandidat kunci ber-escape memaksa
            // fallback sekuensial penuh demi semantik pencocokan yang sama
            // persis dengan jalur tanpa indeks.
            if (idx_has_escape_keys_) {
                restore_root_cursor();
                types::size_t ignored = 0;
                return root_obj_.find_field_counted(key, &value_, ignored);
            }
            return false;
        }

        if (idx_disabled_) [[unlikely]] {
            // Indeks tidak memungkinkan (arena penuh): jalur murni lama
            // tanpa bookkeeping berat.
            types::size_t ignored = 0;
            doc_.get_root().get_object(); // ritual kursor = kode lama.
            return root_obj_.find_field_counted(key, &value_, ignored);
        }

        // Fallback sekuensial seperti kode lama, PLUS penghitung berat.
        doc_.get_root().get_object(); // kursor = kunci pertama root.
        types::size_t members = 0;
        const bool hit
            = root_obj_.find_field_counted(key, &value_, members);

        if (members <= kIdxQuota || ++heavy_queries_ < 2) [[likely]] {
            return hit;
        }

        // Kueri berat kedua: bangun indeks. Bila sukses, mata ulang posisi
        // kursor via lookup (semantik nilai identik dengan hit sekuensial).
        build_root_index();
        if (idx_slots_ && lookup_root_key(key)) {
            return true;
        }

        // Gagal membangun (arena penuh) / lookup tak wajar: pulihkan
        // ritme kursor lalu ulangi sekuensial MURNI.
        restore_root_cursor();
        types::size_t ignored = 0;
        return root_obj_.find_field_counted(key, &value_, ignored);
    }

    // Telusuri seluruh member teratas root object dalam SATU jalan.
    // kFill=false: hitung member saja (pass pembangun kapasitas).
    // kFill=true : pasang setiap member ke slot open-addressing.
    // Kursor: me-rewind sendiri di awal; PEMANGGIL wajib memulihkan ritme
    // kursor root setelah walk selesai.
    template <bool kFill>
    types::size_t
    walk_root_object() NOEXCEPT {
        doc_.rewind();
        ondemand::JsonIterator& it = doc_.iter();
        if (it.peek_char() != '{') [[unlikely]] {
            return 0;
        }

        it.advance(); // habiskan '{' → kursor pada kunci pertama.
        types::size_t count = 0;
        for (;;) {
            char c = it.peek_char();
            if (c == '}' || c == '\0') [[unlikely]] {
                break;
            }
            if (c == ',') {
                it.advance();
            }

            const types::size_t key_open_idx = it.index();
            ondemand::JsonValue key_val(it);
            types::StringView current_key = key_val.get_string();
            if (it.peek_char() == ':') {
                it.advance();
            }
            const types::size_t val_idx = it.index();

            if (kFill) {
                if (raw_key_has_escape(key_open_idx)) {
                    // Kunci ber-escape dilewatkan: pencocokan sekuensial
                    // membandingkan hasil unescape, teks mentah tidak
                    // setara — index mundur dan biarkan langkah fallback
                    // sekuensial memberi keputusan.
                    idx_has_escape_keys_ = true;
                } else {
                    types::uint64_t h = utils::Hash::fnv1a64(current_key);
                    if (h == 0) [[unlikely]] {
                        h = 1; // 0 = slot kosong, dipaksa jauh dari sentinel.
                    }
                    insert_slot(h, current_key, key_open_idx, val_idx);
                }
            }
            ++count;

            it.skip_child(); // lewati nilai, kursor menuju separator/'}'
        }
        return count;
    }

    void
    insert_slot(
        types::uint64_t h, const types::StringView& key,
        types::size_t key_idx, types::size_t val_idx) NOEXCEPT {
        types::size_t pos = static_cast<types::size_t>(h) & idx_mask_;
        while (idx_slots_[pos].hash != 0) {
            // Hash samberita TIDAKMUNGKIN tanpa kunci berkembar — namun
            // selalu verifikasi byte-exact: kunci kembar mempertahankan
            // kemunculan PERTAMA (semantik sama dengan pencarian sekuensial).
            if (idx_slots_[pos].hash == h
                && key_text_at(
                       static_cast<types::size_t>(idx_slots_[pos].key_idx))
                    == key) {
                return;
            }
            pos = (pos + 1) & idx_mask_;
        }
        idx_slots_[pos].hash    = h;
        idx_slots_[pos].key_idx = key_idx;
        idx_slots_[pos].val_idx = val_idx;
    }

    [[nodiscard]] bool
    lookup_root_key(const types::StringView& key) NOEXCEPT {
        ondemand::JsonIterator& it = doc_.iter();

        types::uint64_t h = utils::Hash::fnv1a64(key);
        if (h == 0) [[unlikely]] {
            h = 1;
        }

        types::size_t pos = static_cast<types::size_t>(h) & idx_mask_;
        while (idx_slots_[pos].hash != 0) {
            if (idx_slots_[pos].hash == h
                && key_text_at(
                       static_cast<types::size_t>(idx_slots_[pos].key_idx))
                    == key) {
                // Lompatan kursor ke entry awal nilai (rutenya identik
                // dengan keadaan pasca-hitung find_field sekuensial).
                it.seek(static_cast<types::size_t>(
                    idx_slots_[pos].val_idx));
                return true;
            }
            pos = (pos + 1) & idx_mask_;
        }
        restore_root_cursor();
        return false;
    }

    void
    build_root_index() NOEXCEPT {
        if (!arena_ || idx_slots_ || idx_disabled_) [[unlikely]] {
            idx_disabled_ = true;
            return;
        }

        idx_has_escape_keys_ = false;

        // Pass 1: hitung member (menyapu kursor).
        const types::size_t count = walk_root_object<false>();

        // Pass 2: kapasitas power-of-two ≥ 2× member (bantu probing).
        types::size_t capacity = 16;
        while (capacity < count * 2) {
            capacity <<= 1;
        }

        void* mem = arena_->allocate(
            capacity * sizeof(RootFieldSlot), alignof(RootFieldSlot));

        // Pulihkan kursor SEBELUM pemeriksaan apapun — state selalu bersih.
        restore_root_cursor();

        if (!mem) [[unlikely]] {
            idx_disabled_ = true; // arena penuh: fallback sekuensial saja.
            return;
        }

        auto* slots = static_cast<RootFieldSlot*>(mem);
        for (types::size_t j = 0; j < capacity; ++j) {
            slots[j].hash = 0; // 0 = slot kosong
        }
        idx_slots_ = slots;
        idx_mask_  = capacity - 1;
        idx_count_ = count;

        walk_root_object<true>();
    }

    // Teks kunci MENTAH dari pasangan entry tape quote (pembuka/penutup) —
    // tanpa pelintasan kursor sepenuhnya via tape/src.
    [[nodiscard]] types::StringView
    key_text_at(types::size_t key_idx) NOEXCEPT {
        ondemand::JsonIterator& it = doc_.iter();
        const types::uint64_t* tape   = it.tape();
        const char* src               = it.src();
        const types::uint64_t open_pos  = tape[key_idx];
        const types::uint64_t close_pos = tape[key_idx + 1];
        return types::StringView(
            src + open_pos + 1,
            static_cast<types::size_t>(close_pos - (open_pos + 1)));
    }

    // True bila teks kunci mentah memuat '\\' — kunci ber-escape TIDAK bisa
    // mengikuti keputusan indeks (urutan pencocokan sekuensial membandingkan
    // hasil unescape).
    [[nodiscard]] bool
    raw_key_has_escape(types::size_t key_idx) NOEXCEPT {
        ondemand::JsonIterator& it = doc_.iter();
        const types::uint64_t* tape   = it.tape();
        const char* src               = it.src();
        const types::uint64_t open_pos  = tape[key_idx];
        const types::uint64_t close_pos = tape[key_idx + 1];
        for (const char* p = src + open_pos + 1; p < src + close_pos; ++p) {
            if (*p == '\\') {
                return true;
            }
        }
        return false;
    }

    // Pulihkan ritme kursor persis seperti state pasca-construction
    // (find_field berikutnya mengharapkan kursor di kunci pertama).
    void
    restore_root_cursor() NOEXCEPT {
        doc_.rewind();
        doc_.get_root().get_object();
    }

    // ------------------------------------------------------------------
    //  INDEKS ELEMEN ARRAY (LAZY) — layanan segmen [index] path query
    // ------------------------------------------------------------------
    // Layan idx pada array ber-identitas arr_id: lompatan O(1) bila tabel
    // terbangun untuk array INI, sekuensial murni + penghitung berat bila
    // belum (kebijakan mirror resolve_root_key).
    [[nodiscard]] bool
    resolve_array_index(
        types::size_t arr_id, types::size_t idx) NOEXCEPT {
        if (arr_idx_present_ && arr_elem_source_ == arr_id) [[likely]] {
            if (idx >= arr_elem_count_) {
                return false;
            }
            doc_.iter().seek(arr_elem_idx_[idx]);
            return true;
        }

        if (arr_idx_disabled_) [[unlikely]] {
            // Arena penuh: jalur murni lama tanpa bookkeeping berat.
            value_.get_array(); // habiskan '[' → kursor di elemen pertama
            return position_at_index(idx);
        }

        // Fallback sekuensial seperti kode lama, PLUS penghitung berat
        // (panjang sapuan ≈ idx).
        value_.get_array(); // habiskan '[' → kursor di elemen pertama
        const bool hit = position_at_index(idx);

        if (idx <= kArrIdxQuota || ++arr_heavy_queries_ < 2) [[likely]] {
            return hit;
        }

        // Kueri berat kedua: bangun tabel untuk array ini. Bila sukses,
        // layan ulang kueri lewat lompatan (kursor identik hasil
        // sekuensial — lanjutan segmen path tak terdampak).
        build_array_index(arr_id);
        if (arr_idx_present_ && arr_elem_source_ == arr_id &&
            idx < arr_elem_count_) {
            doc_.iter().seek(arr_elem_idx_[idx]);
            return true;
        }

        // Gagal membangun (arena penuh) / anomali: pulihkan kursor ke
        // array lalu ulangi sekuensial murni.
        doc_.iter().seek(arr_id);
        value_.get_array();
        return position_at_index(idx);
    }

    void
    build_array_index(types::size_t arr_id) NOEXCEPT {
        if (!arena_ || arr_idx_disabled_) [[unlikely]] {
            arr_idx_disabled_ = true;
            return;
        }

        // Pass 1: hitung elemen (kursor dari '[').
        doc_.iter().seek(arr_id);
        value_.get_array();
        const types::size_t count = walk_array_elements<false>();

        // Alokasi / daur-pakai blok: kapasitas power-of-two ≥ jumlah
        // elemen; array lain yang lebih kecil memakai blok yang sama.
        if (!arr_elem_idx_ || arr_elem_capacity_ < count) {
            types::size_t capacity = 32;
            while (capacity < count) {
                capacity <<= 1;
            }
            void* mem = arena_->allocate(
                capacity * sizeof(types::size_t), alignof(types::size_t));
            if (!mem) [[unlikely]] {
                arr_idx_disabled_ = true; // arena penuh: sekuensial saja.
                return;
            }
            arr_elem_idx_      = static_cast<types::size_t*>(mem);
            arr_elem_capacity_ = capacity;
        }

        // Pass 2: isi posisi kursor nilai per elemen.
        doc_.iter().seek(arr_id);
        value_.get_array();
        walk_array_elements<true>();

        arr_elem_source_   = arr_id;
        arr_elem_count_     = count;
        arr_idx_present_   = true;
        arr_heavy_queries_ = 0;
    }

    // Telusuri seluruh elemen array dalam SATU jalan (kursor wajib
    // pasca-'['). kFill=false: hitung saja; kFill=true: catat tape idx
    // kursor nilai tiap elemen — POSISI PERSIS tempat position_at_index
    // mengembalikan true untuk elemen tersebut.
    template <bool kFill>
    types::size_t
    walk_array_elements() NOEXCEPT {
        ondemand::JsonIterator& it = doc_.iter();
        types::size_t i = 0;
        for (;;) {
            char c = value_.peek_char();
            if (c == '\0') [[unlikely]] {
                break;
            }
            if (c == ']') {
                // Terminator elemen ANGKA terakhir, atau array memang habis.
                if (value_.has_inline_number()) {
                    if (kFill) {
                        arr_elem_idx_[i] = it.index();
                    }
                    ++i;
                }
                break;
            }
            if (kFill) {
                arr_elem_idx_[i] = it.index();
            }
            ++i;

            if (c == ',') {
                // angka: lintas terminator/separator ke elemen berikutnya
                value_.skip();
            } else {
                value_.skip(); // habiskan elemen string/literal/kontainer
                if (value_.peek_char() == ',') {
                    value_.skip(); // lewati separator
                }
            }
        }
        return i;
    }

  public:

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
    // CATATAN: setelah find_path, counter array TIDAK diwarisi (mode mesin
    // array di-reset). Bila hasilnya array: array_size() menghitung ulang,
    // atau array_next() tanpa array_size langsung streaming satu lintasan.
    [[nodiscard]] types::int32_t
    find_path(const char* const path, types::size_t len) NOEXCEPT {
        doc_.rewind();
        value_pending_ = false;
        array_remaining_ = 0;
        array_mode_      = ArrayIterMode::None;
        array_final_number_ = false;

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
                // Identitas array (tape idx '[') ditangkap SEBELUM '['
                // dikonsumsi; layanan lewat indeks elemen lazy.
                if (!resolve_array_index(doc_.iter().index(), idx)) {
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

                // Segmen pertama = kunci root: pemecah lewat indeks lazy
                // (bila sudah terbangun) atau sekuensial berhitung; segmen
                // berjalan tetap sekuensial murni.
                const types::StringView root_seg
                    (path + start, i - start);
                if (start == 0) {
                    if (!resolve_root_key(root_seg)) {
                        return NOT_FOUND;
                    }
                } else {
                    ondemand::JsonObject obj = value_.get_object();
                    if (!obj.find_field_checked(root_seg, &value_)) {
                        return NOT_FOUND;
                    }
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
        array_final_number_ = false;
        if (value_.peek_char() != '[') [[unlikely]] {
            array_remaining_ = 0;
            array_mode_      = ArrayIterMode::None;
            return 0;
        }

        ondemand::JsonArray arr = value_.get_array();
        array_remaining_        = arr.count_elements();
        array_mode_             = ArrayIterMode::Counted;
        value_pending_          = false;
        return array_remaining_;
    }

    // Maju ke elemen berikutnya dan mendelegasikan kursor ke atasnya.
    // Mengembalikan 1 saat elemen tersedia (nilai siap dibaca lewat
    // value_type/get_*), 0 saat array habis (kursor kembali ke ritme
    // object root: ',' / '}' — find_field tetap valid). Elemen yang tidak
    // dibaca getter di-skip otomatis oleh array_next.
    //
    // Dua pola pemakaian (mesin ArrayIterMode):
    //  - Counted: array_size() dulu (kontrak lama, counter otoritatif).
    //  - Streaming: langsung array_next() saat kursor tepat '[' — satu
    //    lintasan, tidak ada penghitungan inspektif dua kali jalan.
    [[nodiscard]] types::int32_t
    array_next() NOEXCEPT {
        if (array_mode_ == ArrayIterMode::Counted && array_remaining_ == 0) {
            // Mode Counted habis: tuntun kursor ke ritme object root lalu
            // nonaktifkan mesin (array_next berikutnya dijawab None).
            value_pending_ = false;
            char c         = value_.peek_char();
            if (c == ']') {
                value_.skip();
            } else if (c != '\0') {
                value_.skip();
                if (value_.peek_char() == ']') {
                    value_.skip();
                }
            }
            array_mode_ = ArrayIterMode::None;
            return 0;
        }

        if (array_mode_ == ArrayIterMode::None) {
            // Tanpa array_size(): kursor TEPAT '[' → masuk streaming.
            // (value_pending_ dari find_path/find hanyalah "nilai siap
            // dibaca" — bukan elemen array; gerbang cukup dari kursor.)
            // Pemanggilan lain (kursor bukan '[') tetap proteksi lama: 0.
            if (value_.peek_char() != '[') {
                value_pending_ = false;
                return 0;
            }

            ondemand::JsonArray arr = value_.get_array(); // habiskan '['
            (void)arr;
            array_mode_         = ArrayIterMode::Streaming;
            array_final_number_ = false;
            value_pending_      = false;
        }

        if (array_mode_ == ArrayIterMode::Counted) {
            // --- Kontrak lama: counter otoritatif ---------------------
            array_remaining_--;

            if (value_pending_) {
                // Elemen sebelumnya diekspos tetapi tidak dibaca → habiskan.
                char c = value_.peek_char();
                if (c == ',') {
                    // Angka: kursor sudah di terminator sekaligus
                    // separator — satu langkah ke elemen berikutnya.
                    value_.skip();
                } else if (c != ']' && c != '\0') {
                    value_.skip(); // string/literal/kontainer
                    if (value_.peek_char() == ',') {
                        value_.skip(); // lewati separator
                    }
                }
            } else if (value_.peek_char() == ',') {
                // ',' pada kursor bisa berarti (a) terminator elemen ANGKA
                // yang sedang ter-expose (angka 0 entry), atau (b) separator
                // sisa konsumsi elemen non-angka — yang harus dilintasi.
                // Gap teks berawalan digit/tanda = kasus (a); gap lain =
                // kasus (b).
                if (!value_.has_inline_number()) {
                    value_.skip();
                }
            }

            value_pending_ = true;
            return 1;
        }

        // --- Streaming: habis dideteksi dari kursor, counter tidak ada ---
        if (array_final_number_) {
            // Elemen angka TERAKHIR telah diekspos panggilan sebelumnya.
            // Panggilan ini WAJIB menutup array — tanpa ekspos baru.
            if (value_pending_) {
                // Belum dibaca getter: kursor masih di terminator elemen
                // (',' atau ']'). Lintasi keduanya sampai melewati ']'.
                if (value_.peek_char() == ',') {
                    value_.skip();
                }
                if (value_.peek_char() == ']') {
                    value_.skip();
                }
            }
            // Sudah dibaca getter: kursor sudah melampaui ']' — tidak ada
            // yang perlu dilangkahi, cukup nonaktifkan mesin.
            array_final_number_ = false;
            array_mode_         = ArrayIterMode::None;
            value_pending_      = false;
            return 0;
        }

        if (value_pending_) {
            // Elemen sebelumnya diekspos tetapi tidak dibaca → habiskan
            // (logika identik dengan cabang Counted).
            char c = value_.peek_char();
            if (c == ',') {
                value_.skip();
            } else if (c != ']' && c != '\0') {
                value_.skip();
                if (value_.peek_char() == ',') {
                    value_.skip();
                }
            }
        } else if (value_.peek_char() == ',' && !value_.has_inline_number()) {
            // Separator sisa elemen non-angka yang sudah dikonsumsi getter.
            value_.skip();
        }

        char c = value_.peek_char();
        if (c == ',') {
            // Terminator elemen ANGKA (0 entry) — bila gap berawalan
            // digit/tanda, elemen angka masih tersisa untuk diekspos.
            if (value_.has_inline_number()) {
                // Terminalitas via lookahead: ',..' bisa menyatukan
                // terminator elemen angka ini dengan separator elemen
                // berikut (0 entry — tape melompat langsung ke entry
                // penerus). Entry penerus:
                //   '"'/"t"/dsb → elemen lain menyusul (bukan final).
                //   ']' + gap kosong → array tamat di elemen INI.
                //   ']' + gap angka → elemen ini non-final (ada angka
                //                   terakhir menyusul; ditangani call
                //                   berikutnya lewat cabang ']').
                ondemand::JsonIterator& it = doc_.iter();
                const types::size_t nxt = it.index() + 1;
                if (it.peek_char_at(nxt) == ']' && !it.has_inline_number_at(nxt)) {
                    array_final_number_ = true;
                }
                value_pending_ = true;
                return 1;
            }
            value_.skip(); // separator biasa — lintasi
            c = value_.peek_char();
        }

        if (c == ']') {
            if (value_.has_inline_number()) {
                // Angka TERAKHIR belum terbaca: kursor sudah di ']' namun
                // gap sebelumnya memuat angka — ekspos tanpa melangkah.
                array_final_number_ = true;
                value_pending_      = true;
                return 1;
            }
            value_.skip(); // keluar array
            array_mode_    = ArrayIterMode::None;
            value_pending_ = false;
            return 0;
        }

        if (c == '\0' || c == '}') [[unlikely]] {
            // Payload berakhir / ritme rusak: tamatkan dengan aman tanpa
            // menggeser kursor lebih jauh.
            array_mode_    = ArrayIterMode::None;
            value_pending_ = false;
            return 0;
        }

        // Elemen top-level berikutnya ('"' / 't' / 'f' / 'n' / '[' / '{'
        // atau terminator angka yang sudah dilintasi di atas) — delegasi.
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
