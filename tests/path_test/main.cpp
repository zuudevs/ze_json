// Kelompok uji: PATH — pencarian sekaligus pembacaan dalam SATU panggilan
// C ABI (find_path / get_*_at): kedalaman origin, object dalam array,
// tipe salah, indeks luar jangkauan, overflow sintaks, dan pemakaian
// ulang Session.
#include <gtest/gtest.h>
#include <string_view>

#include <ze_json/ze_json.hpp>

namespace zuu::testing {

class PathTest : public ::testing::Test {
  protected:
    types::uint8_t buffer[1024 * 1024]{}; // allocate 1 MB
    memory::Arena* arena_handle{nullptr};
    types::uint64_t tape_[256]{}; // tape untuk Session (entry struktural)

    PathTest() noexcept
        : arena_handle(create_arena(buffer, sizeof(buffer))) {}

    api::Session*
    make_session(const char* payload, types::size_t payload_size,
                 types::int32_t& err) {
        err = static_cast<types::int32_t>(api::INVALID_JSON);
        return create_session(payload, payload_size, tape_, 256,
                              arena_handle, err);
    }
};

TEST_F(PathTest, MixedQueriesAcrossTapeOrder) {
    // SATU panggilan wasm: navigasi + cek tipe + baca.
    alignas(8) constexpr const char payload[] =
        R"({"active":true,"colors":["white","black"],"nums":[1,2,3],)"
        R"("position":{"x":24,"y":48},"deep":1})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    // Kueri berurut yang memaksa rewind lintas arah: y > x (maju),
    // lalu "deep" yang berada SEBELUM "position" di tape — bukti
    // rewinding bekerja antar-kueri.
    types::int32_t st = api::NOT_FOUND;
    EXPECT_EQ(get_int_at(session, "position.x", 10, st), 24);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    EXPECT_EQ(get_int_at(session, "position.y", 10, st), 48);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    EXPECT_EQ(get_int_at(session, "deep", 4, st), 1);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    EXPECT_EQ(get_bool_at(session, "active", 6, st), 1);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // String via array-index
    const char* data = nullptr;
    types::size_t len = 0;
    get_string_at(session, "colors[1]", 9, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "black");

    get_string_at(session, "colors[0]", 9, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "white");

    // Angka 0-entry via index (terminator ']' di akhir array)
    EXPECT_EQ(get_int_at(session, "nums[2]", 7, st), 3);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(get_int_at(session, "nums[0]", 7, st), 1);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Gagal-case: di luar jangkauan / tipe salah / sintaks rusak
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "nums[5]", 7, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "position.z", 10, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "colors[0]", 9, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::TYPE_MISMATCH);

    // String ketika nilai boolean → TYPE_MISMATCH juga
    st = api::OK;
    get_string_at(session, "active", 6, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::TYPE_MISMATCH);
    EXPECT_EQ(data, nullptr);
    EXPECT_EQ(len, 0u);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "position..x", 11, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::INVALID_PATH);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "colors[x]", 9, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::INVALID_PATH);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "", 0, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::INVALID_PATH);

    arena_handle->reset();
}

TEST_F(PathTest, ThreeLevelsDeep) {
    // Rantai key segmen 3 tingkat: a.b.c.
    alignas(8) constexpr const char payload[] =
        R"({"a":{"b":{"c":42}},"other":7})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    types::int32_t st = api::NOT_FOUND;
    EXPECT_EQ(get_int_at(session, "a.b.c", 5, st), 42);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Interupsi tengah rantai: "a.b" adalah object → tipe cek int menolak.
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "a.b", 3, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::TYPE_MISMATCH);

    arena_handle->reset();
}

TEST_F(PathTest, ObjectInsideArray) {
    // Segmen index lalu key di elemen array: items[1].n.
    alignas(8) constexpr const char payload[] =
        R"({"items":[{"n":7,"tag":"a"},{"n":8,"tag":"b"}]})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    types::int32_t st = api::NOT_FOUND;
    EXPECT_EQ(get_int_at(session, "items[1].n", 10, st), 8);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Elemen 0 juga bisa dituju.
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "items[0].n", 10, st), 7);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Index luar jangkauan: array hanya berisi 2 elemen.
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "items[2].n", 10, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    arena_handle->reset();
}

TEST_F(PathTest, NullValueIsTypeMismatch) {
    // Null bukan Number maupun Boolean → TYPE_MISMATCH pada getter.
    alignas(8) constexpr const char payload[] =
        R"({"n":null})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    types::int32_t st = api::OK;
    EXPECT_EQ(get_int_at(session, "n", 1, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::TYPE_MISMATCH);

    st = api::OK;
    EXPECT_EQ(get_bool_at(session, "n", 1, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::TYPE_MISMATCH);

    arena_handle->reset();
}

TEST_F(PathTest, IndexOverflowIsInvalidPath) {
    // Segmen digit melebihi size_t → ditolak sebagai INVALID_PATH,
    // tanpa overflow aritmetika senyap.
    alignas(8) constexpr const char payload[] =
        R"({"colors":["red"]})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    types::int32_t st = api::OK;
    EXPECT_EQ(get_int_at(
                  session, "colors[99999999999999999999]", 30, st),
              0);
    EXPECT_EQ(static_cast<api::Status>(st), api::INVALID_PATH);

    arena_handle->reset();
}

TEST_F(PathTest, RepeatedQueriesAreIndependent) {
    // Kueri yang sama diulang dua kali dengan kueri lain di antaranya —
    // tiap find_path rewind dari root, sehingga hasil selalu konsisten.
    alignas(8) constexpr const char payload[] =
        R"({"colors":["red","green"],"id":5})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    const char* data = nullptr;
    types::size_t len = 0;
    types::int32_t st = api::NOT_FOUND;

    get_string_at(session, "colors[1]", 9, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "green");

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "id", 2, st), 5);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    get_string_at(session, "colors[1]", 9, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "green");

    arena_handle->reset();
}

} // namespace zuu::testing
