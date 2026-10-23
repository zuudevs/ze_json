// Kelompok uji: ARRAY — traversal C ABI array_size/array_next:
// campuran tipe elemen, angka 0-entry, kasus ambigu '[1]' vs '[]',
// dan pengampunan kursor bila array tidak di-drain.
#include <gtest/gtest.h>
#include <string_view>

#include <ze_json/ze_json.hpp>

namespace zuu::testing {

class ArrayTest : public ::testing::Test {
  protected:
    types::uint8_t buffer[1024 * 1024]{}; // allocate 1 MB
    memory::Arena* arena_handle{nullptr};
    types::uint64_t tape_[256]{}; // tape untuk Session (entry struktural)

    ArrayTest() noexcept : arena_handle(create_arena(buffer, sizeof(buffer))) {}

    // Membuat Session siap pakai; gagal → nullptr (status di `err`).
    api::Session*
    make_session(
        const char* payload, types::size_t payload_size, types::int32_t& err) {
        err = static_cast<types::int32_t>(api::INVALID_JSON);
        return create_session(
            payload, payload_size, tape_, 256, arena_handle, err);
    }
};

TEST_F(ArrayTest, TraversalMixedElements) {
    // Traversal array via API publik: campuran semua tipe elemen, termasuk
    // kontainer bersarang yang di-skip dan angka negatif-pecahan (0 entry).
    alignas(8) constexpr const char payload[] =
        R"({"colors":[1,"two",true,null,[7,8],{"k":4},-8.5],"after":99})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    ASSERT_EQ(find_field(session, "colors", 6), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Array);
    EXPECT_EQ(array_size(session), 7u);

    // Elemen 0: angka (0 entry — kursor di terminatornya)
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Number);
    EXPECT_EQ(get_int(session), 1);

    // Elemen 1: string
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::String);
    const char* str_data  = nullptr;
    types::size_t str_len = 0;
    get_string(session, str_data, str_len);
    EXPECT_EQ(std::string_view(str_data, str_len), "two");

    // Elemen 2: boolean (dibaca); elemen 3: null (dikonsumsi via get_bool)
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_bool(session), 1);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)), api::ValueType::Null);
    EXPECT_EQ(get_bool(session), 0); // 'n' != 't' → false, tapi tetap konsumsi

    // Elemen 4: array bersarang — TIDAK dibaca, array_next harus skip
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Array);

    // Elemen 5: object bersarang — juga di-skip
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Object);

    // Elemen 6: angka negatif-pecahan
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Number);
    EXPECT_NEAR(get_double(session), -8.5, 1e-9);

    // Elemen habis; kursor kembali ke ']' agar object root bisa dilanjutkan
    EXPECT_EQ(array_next(session), 0);
    ASSERT_EQ(find_field(session, "after", 5), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Number);
    EXPECT_EQ(get_int(session), 99);

    arena_handle->reset();
}

TEST_F(ArrayTest, AmbiguousTapeCases) {
    // Kasus ambigu 0-entry: '[1]' vs '[]' menghasilkan tape identik,
    // ditambah elemen angka terakhir dan angka setelah string.
    alignas(8) constexpr const char payload[] =
        R"({"empty":[],"one":[1],"nums":[1,2],"mixed":["a",3],"tail":[5],"next":7})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    // empty: size 0, next 0
    ASSERT_EQ(find_field(session, "empty", 5), 1);
    EXPECT_EQ(array_size(session), 0u);
    EXPECT_EQ(array_next(session), 0);

    // one: '[1]' — angka terakhir sebelum ']'
    ASSERT_EQ(find_field(session, "one", 3), 1);
    EXPECT_EQ(array_size(session), 1u);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 1);
    EXPECT_EQ(array_next(session), 0);

    // nums: dua angka beruntun (0 entry semuanya)
    ASSERT_EQ(find_field(session, "nums", 4), 1);
    EXPECT_EQ(array_size(session), 2u);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 1);
    // Elemen kedua tidak dibaca → di-skip otomatis
    EXPECT_EQ(array_next(session), 1);
    EXPECT_NEAR(get_double(session), 2.0, 1e-12);
    EXPECT_EQ(array_next(session), 0);

    // mixed: string lalu angka terakhir (terminator = ']')
    ASSERT_EQ(find_field(session, "mixed", 5), 1);
    EXPECT_EQ(array_size(session), 2u);
    EXPECT_EQ(array_next(session), 1);
    const char* str_data  = nullptr;
    types::size_t str_len = 0;
    get_string(session, str_data, str_len);
    EXPECT_EQ(std::string_view(str_data, str_len), "a");
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 3);
    EXPECT_EQ(array_next(session), 0);

    // tail: '[5]' — elemen kedua tidak dibaca lalu habis
    ASSERT_EQ(find_field(session, "tail", 4), 1);
    EXPECT_EQ(array_size(session), 1u);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(array_next(session), 0);
    EXPECT_EQ(array_next(session), 0); // panggilan ganda tetap 0

    ASSERT_EQ(find_field(session, "next", 4), 1);
    EXPECT_EQ(get_int(session), 7);

    arena_handle->reset();
}

TEST_F(ArrayTest, DrainContractAndForgiveness) {
    // Tiga bagian kontrak drain (tiap bagian pakai Session baru):
    //  (a) lupa drain DI TENGAH array → kueri object berikutnya gagal
    //      (kursor cannot cross elemen yang belum dibaca — forward-only);
    //  (b) drain penuh (array_next sampai 0) → find berikutnya sukses;
    //  (c) elemen terakhir selesai dibaca, TANPA memanggil array_next
    //      terakhir → pengampunan kursor ']'-sisa tetap membuat find sukses.
    alignas(8) constexpr const char payload[] = R"({"nums":[1,2,3],"after":9})";
    constexpr auto payload_size               = sizeof(payload) - 1;

    types::int32_t err = api::OK;

    // --- (a) lupa drain di tengah array ---------------------------------
    {
        auto* session = make_session(payload, payload_size, err);
        ASSERT_NE(session, nullptr);
        ASSERT_EQ(static_cast<api::Status>(err), api::OK);

        ASSERT_EQ(find_field(session, "nums", 4), 1);
        EXPECT_EQ(array_size(session), 3u);

        // Baca elemen 0 saja, lalu LANGSUNG tinggalkan array.
        EXPECT_EQ(array_next(session), 1);
        EXPECT_EQ(get_int(session), 1);

        // Kursor masih di dalam array → find gagal dengan rapi (0),
        // tanpa perilaku tidak terdefinisi.
        EXPECT_EQ(find_field(session, "after", 5), 0);
        arena_handle->reset();
    }

    // --- (b) drain penuh ------------------------------------------------
    {
        auto* session = make_session(payload, payload_size, err);
        ASSERT_NE(session, nullptr);
        ASSERT_EQ(static_cast<api::Status>(err), api::OK);

        ASSERT_EQ(find_field(session, "nums", 4), 1);
        EXPECT_EQ(array_size(session), 3u);
        EXPECT_EQ(array_next(session), 1);
        EXPECT_EQ(get_int(session), 1);

        // Habiskan seluruh elemen (baca sisanya / biarkan di-skip).
        EXPECT_EQ(array_next(session), 1);
        EXPECT_EQ(array_next(session), 1);
        EXPECT_EQ(array_next(session), 0);

        ASSERT_EQ(find_field(session, "after", 5), 1);
        EXPECT_EQ(
            static_cast<api::ValueType>(value_type(session)),
            api::ValueType::Number);
        EXPECT_EQ(get_int(session), 9);
        arena_handle->reset();
    }

    // --- (c) elemen terakhir dibaca, drain terakhir dilupakan -----------
    {
        auto* session = make_session(payload, payload_size, err);
        ASSERT_NE(session, nullptr);
        ASSERT_EQ(static_cast<api::Status>(err), api::OK);

        ASSERT_EQ(find_field(session, "nums", 4), 1);
        EXPECT_EQ(array_size(session), 3u);
        EXPECT_EQ(array_next(session), 1);
        EXPECT_EQ(get_int(session), 1);
        EXPECT_EQ(array_next(session), 1);
        EXPECT_EQ(get_int(session), 2);
        EXPECT_EQ(array_next(session), 1);
        EXPECT_EQ(get_int(session), 3);

        // TIDAK memanggil array_next terakhir: kursor berhenti di ']'
        // sisa. Pengampunan kursor membiarkan find berikutnya sukses.
        ASSERT_EQ(find_field(session, "after", 5), 1);
        EXPECT_EQ(get_int(session), 9);
        arena_handle->reset();
    }
}

TEST_F(ArrayTest, StreamingEqualsCountedMixedElements) {
    // Pola streaming: array_next TANPA array_size pada campuran tipe yang
    // sama dengan TraversalMixedElements (kontrak lama) — hasil wajib
    // identik: sequence tipe per elemen dan drain ke ritme object root.
    alignas(8) constexpr const char payload[] =
        R"({"colors":[1,"two",true,null,[7,8],{"k":4},-8.5],"after":99})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    ASSERT_EQ(find_field(session, "colors", 6), 1);

    // Elemen 0: angka (0 entry)
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Number);
    EXPECT_EQ(get_int(session), 1);

    // Elemen 1: string
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::String);
    const char* str_data  = nullptr;
    types::size_t str_len = 0;
    get_string(session, str_data, str_len);
    EXPECT_EQ(std::string_view(str_data, str_len), "two");

    // Elemen 2 & 3: boolean + null (bebas di-skip tanpa getter)
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Null);

    // Elemen 4: array bersarang — tidak dibaca; array_next berikutnya
    // melewati SELURUH kontainer.
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Array);

    // Elemen 5: object bersarang — di-skip demikian pula.
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Object);

    // Elemen 6: angka pecahan negatif (0 entry di terminator ',')
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Number);
    EXPECT_EQ(get_double(session), -8.5);

    // Empat array_next lagi: elemen 5 & 6 dilalui, lalu TAMAT (drain
    // streaming mengembalikan kursor ke ritme object root).
    EXPECT_EQ(array_next(session), 0);

    // Drain selesai → find() ke field lanjutan tetap valid.
    ASSERT_EQ(find_field(session, "after", 5), 1);
    EXPECT_EQ(get_int(session), 99);
    arena_handle->reset();
}

TEST_F(ArrayTest, StreamingNumberOnlyAndAmbiguity) {
    // Kasus paling licik: array FULL angka (semua 0 entry) — kursor selalu
    // di terminator (',' / ']'); habis HARUS terdeteksi lewat gap
    // inline-number, bukan counter. Termasuk kasus ambigu '[1]' vs '[]'
    // pada tape yang identik.
    alignas(8) constexpr const char payload[] =
        R"({"nums":[1,2,3],"e":[],"one":[7],"after":5})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    ASSERT_EQ(find_field(session, "nums", 4), 1);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 1);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 2);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 3);
    // Angka terakhir: kursor ']' + gap angka → masih 1, lalu tamat.
    EXPECT_EQ(array_next(session), 0);
    ASSERT_EQ(find_field(session, "after", 5), 1);
    EXPECT_EQ(get_int(session), 5);
    arena_handle->reset();

    // Array kosong: langsung tamat tanpa ekspos.
    session = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(find_field(session, "e", 1), 1);
    EXPECT_EQ(array_next(session), 0);
    ASSERT_EQ(find_field(session, "after", 5), 1);
    EXPECT_EQ(get_int(session), 5);
    arena_handle->reset();

    // Ambigu '[7]' (tape '[]' serupa): tepat satu elemen angka.
    session = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(find_field(session, "one", 3), 1);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 7);
    EXPECT_EQ(array_next(session), 0);
    arena_handle->reset();
}

TEST_F(ArrayTest, StreamingViaFindPath) {
    // find_path pada nilai array lalu streaming langsung — kontrak baru:
    // tidak perlu array_size (dan tanpa pass penghitungan dua kali jalan).
    alignas(8) constexpr const char payload[] =
        R"({"tags":["a","bb","ccc"]})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    ASSERT_EQ(static_cast<api::Status>(find_path(session, "tags", 4)),
        api::OK);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Array);

    const char* data  = nullptr;
    types::size_t len = 0;
    EXPECT_EQ(array_next(session), 1);
    get_string(session, data, len);
    EXPECT_EQ(std::string_view(data, len), "a");

    EXPECT_EQ(array_next(session), 1);
    get_string(session, data, len);
    EXPECT_EQ(std::string_view(data, len), "bb");

    EXPECT_EQ(array_next(session), 1);
    get_string(session, data, len);
    EXPECT_EQ(std::string_view(data, len), "ccc");

    EXPECT_EQ(array_next(session), 0);
    EXPECT_EQ(array_next(session), 0); // over-drain aman: tetap 0
    arena_handle->reset();
}

TEST_F(ArrayTest, StreamingMixedThenCountedSeparate) {
    // Bauran pola pada SATU session: streaming dulu (drain penuh), lalu
    // field lain dengan kontrak counted — kedua mesin tidak saling bocor.
    alignas(8) constexpr const char payload[] =
        R"({"a":[1,2,3],"x":9,"b":["p","q"],"after":7})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    // Streaming penuh pada "a".
    ASSERT_EQ(find_field(session, "a", 1), 1);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 1);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 2);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_int(session), 3);
    EXPECT_EQ(array_next(session), 0);

    // Kontrak counted pada "b".
    ASSERT_EQ(find_field(session, "b", 1), 1);
    EXPECT_EQ(array_size(session), 2u);
    EXPECT_EQ(array_next(session), 1);
    const char* data  = nullptr;
    types::size_t len = 0;
    get_string(session, data, len);
    EXPECT_EQ(std::string_view(data, len), "p");
    EXPECT_EQ(array_next(session), 1);
    get_string(session, data, len);
    EXPECT_EQ(std::string_view(data, len), "q");
    EXPECT_EQ(array_next(session), 0);

    ASSERT_EQ(find_field(session, "after", 5), 1);
    EXPECT_EQ(get_int(session), 7);
    arena_handle->reset();
}

} // namespace zuu::testing
