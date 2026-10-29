// Kelompok uji: SESSION — siklus hidup & semantik dasar API C ABI:
// pembuatan + kode status, value_type awal, find/lompat/getter, misal key.
#include <gtest/gtest.h>
#include <string_view>

#include <ze_json/ze_json.hpp>

namespace zuu::testing {

class SessionTest : public ::testing::Test {
  protected:
    types::uint8_t buffer[1024 * 1024]{}; // allocate 1 MB
    memory::Arena* arena_handle{nullptr};
    types::uint64_t tape_[256]{}; // tape untuk Session (entry struktural)

    SessionTest() noexcept
        : arena_handle(create_arena(buffer, sizeof(buffer))) {}
};

TEST_F(SessionTest, CreateSessionAndCursorDefault) {
    // Ctor Session langsung membangun JsonObject root: kursor default
    // berada DI DALAM root (entry kunci pertama), bukan di '{"'.
    // Kueri pertama tetap benar karena find mulai dari ',' / '{'.
    alignas(8) constexpr const char payload[] = R"({"a":1,"b":"x","c":true})";
    constexpr auto payload_size               = sizeof(payload) - 1;

    types::int32_t err = static_cast<types::int32_t>(api::INVALID_JSON);
    auto* session =
        create_session(payload, payload_size, tape_, 256, arena_handle, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    // Kursor default: kunci pembuka "a" → kata entry-nya String.
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::String);

    // Kueri pertama lewat find tetap sukses.
    ASSERT_EQ(find_field(session, "a", 1), 1);
    EXPECT_EQ(get_int(session), 1);

    arena_handle->reset();
}

TEST_F(SessionTest, ScannerVariantSelectionAndVersion) {
    // v0.1.5: varian scanner dapat dipilih eksplisit; AUTO = perilaku
    // create_session lama. Ketiga varian wajib menghasilkan session yang
    // setara perilakunya (tape identik lintas varian — kontrak scanner).
    alignas(8) constexpr const char payload[] =
        R"({"a":1,"b":"x","c":true,"colors":[1,2,3]})";
    constexpr auto payload_size = sizeof(payload) - 1;

    for (const auto variant : {SCANNER_AUTO, SCANNER_SCALAR, SCANNER_WIDE}) {
        types::int32_t err = static_cast<types::int32_t>(api::INVALID_JSON);
        auto* session      = create_session_with_scanner(
            payload, payload_size, tape_, 256, arena_handle, variant, err);
        ASSERT_NE(session, nullptr) << "varian " << variant << " gagal dibuat";
        ASSERT_EQ(static_cast<api::Status>(err), api::OK);

        // Kueri setara lintas varian: skalar + path query + array.
        ASSERT_EQ(find_field(session, "a", 1), 1);
        EXPECT_EQ(get_int(session), 1);

        types::int32_t st = static_cast<types::int32_t>(api::OK);
        EXPECT_EQ(get_int_at(session, "colors[2]", 9, st), 3);
        EXPECT_EQ(static_cast<api::Status>(st), api::OK);

        arena_handle->reset();
    }

    // Varian tak dikenal → diperlakukan AUTO (bukan error — kontrak
    // robust ABI).
    {
        types::int32_t err = static_cast<types::int32_t>(api::INVALID_JSON);
        auto* session      = create_session_with_scanner(
            payload, payload_size, tape_, 256, arena_handle, 77, err);
        ASSERT_NE(session, nullptr);
        ASSERT_EQ(static_cast<api::Status>(err), api::OK);
        arena_handle->reset();
    }
}

#if ZE_JSON_VERSION_NUMBER >= 2026000105
TEST_F(SessionTest, VersionNumberMatchesMacro) {
    // Gate kompatibilitas fitur: export get_version() wajib menyamai makro
    // compile-time (skema 2026_0001_05 = era 2026 + 0.1.5).
    static_assert(
        ZE_JSON_VERSION_MAJOR == 0 && ZE_JSON_VERSION_MINOR == 1 &&
            ZE_JSON_VERSION_PATCH == 6,
        "versi uji tertaut pada 0.1.6 — perbarui bersama bump versi");
    EXPECT_EQ(get_version(), ZE_JSON_VERSION_NUMBER);
    EXPECT_EQ(get_version(), 2026000106);
}
#endif

TEST_F(SessionTest, FindJumpsAndReadsAllTypes) {
    alignas(8) constexpr const char payload[] = R"({"a":1,"b":"x","c":true})";
    constexpr auto payload_size               = sizeof(payload) - 1;

    types::int32_t err = static_cast<types::int32_t>(api::INVALID_JSON);
    auto* session =
        create_session(payload, payload_size, tape_, 256, arena_handle, err);
    ASSERT_NE(session, nullptr);

    // Field pertama: angka
    ASSERT_EQ(find_field(session, "a", 1), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::Number);
    EXPECT_EQ(get_int(session), 1);

    // Field kedua: string (kursor maju melewati "a" → lompatan linear)
    ASSERT_EQ(find_field(session, "b", 1), 1);
    EXPECT_EQ(
        static_cast<api::ValueType>(value_type(session)),
        api::ValueType::String);
    const char* data  = nullptr;
    types::size_t len = 0;
    get_string(session, data, len);
    EXPECT_EQ(std::string_view(data, len), "x");

    // Field ketiga: boolean
    ASSERT_EQ(find_field(session, "c", 1), 1);
    EXPECT_EQ(get_bool(session), 1);

    arena_handle->reset();
}

TEST_F(SessionTest, MissedFindReturnsZero) {
    // Key tidak ada → 0; sesi yang sama TIDAK dijamin berlanjut
    // (kursor forward-only berhenti di akhir object).
    alignas(8) constexpr const char payload[] = R"({"a":1})";
    constexpr auto payload_size               = sizeof(payload) - 1;

    types::int32_t err = static_cast<types::int32_t>(api::INVALID_JSON);
    auto* session =
        create_session(payload, payload_size, tape_, 256, arena_handle, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    EXPECT_EQ(find_field(session, "zzz", 3), 0);
    destroy_session(session);
    arena_handle->reset();

    // Sesi kedua (arena yang dipakai ulang) kembali sehat.
    auto* second =
        create_session(payload, payload_size, tape_, 256, arena_handle, err);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);
    ASSERT_EQ(find_field(second, "a", 1), 1);
    EXPECT_EQ(get_int(second), 1);

    arena_handle->reset();
}

TEST_F(SessionTest, RejectsBadPayloads) {
    // Payload kosong & rusak ditolak: session nullptr + status != OK.
    // (Kode status spesifik diserahkan pada desain scanner.)
    types::int32_t err = static_cast<types::int32_t>(api::OK);

    auto* empty = create_session("", 0, tape_, 256, arena_handle, err);
    EXPECT_EQ(empty, nullptr);
    EXPECT_NE(static_cast<api::Status>(err), api::OK);

    auto* broken = create_session("{\"a\":", 5, tape_, 256, arena_handle, err);
    EXPECT_EQ(broken, nullptr);
    EXPECT_NE(static_cast<api::Status>(err), api::OK);

    arena_handle->reset();
}

} // namespace zuu::testing
