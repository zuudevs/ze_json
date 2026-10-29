// Kelompok uji: PATH — pencarian sekaligus pembacaan dalam SATU panggilan
// C ABI (find_path / get_*_at): kedalaman origin, object dalam array,
// tipe salah, indeks luar jangkauan, overflow sintaks, dan pemakaian
// ulang Session.
#include <cstdio>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

#include <ze_json/ze_json.hpp>

namespace zuu::testing {

class PathTest : public ::testing::Test {
  protected:
    types::uint8_t buffer[1024 * 1024]{}; // allocate 1 MB
    memory::Arena* arena_handle{nullptr};
    types::uint64_t tape_[256]{}; // tape untuk Session (entry struktural)

    PathTest() noexcept : arena_handle(create_arena(buffer, sizeof(buffer))) {}

    api::Session*
    make_session(
        const char* payload, types::size_t payload_size, types::int32_t& err) {
        err = static_cast<types::int32_t>(api::INVALID_JSON);
        return create_session(
            payload, payload_size, tape_, 256, arena_handle, err);
    }
};

TEST_F(PathTest, MixedQueriesAcrossTapeOrder) {
    // SATU panggilan wasm: navigasi + cek tipe + baca.
    alignas(8) constexpr const char payload[] =
        R"({"active":true,"colors":["white","black"],"nums":[1,2,3],)"
        R"("position":{"x":24,"y":48},"deep":1})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
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
    const char* data  = nullptr;
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
    auto* session      = make_session(payload, payload_size, err);
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
    auto* session      = make_session(payload, payload_size, err);
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
    alignas(8) constexpr const char payload[] = R"({"n":null})";
    constexpr auto payload_size               = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
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
    alignas(8) constexpr const char payload[] = R"({"colors":["red"]})";
    constexpr auto payload_size               = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    types::int32_t st = api::OK;
    EXPECT_EQ(get_int_at(session, "colors[99999999999999999999]", 30, st), 0);
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
    auto* session      = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    const char* data  = nullptr;
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

TEST_F(PathTest, RootWideIndexPathQueries) {
    // Indeks root lazy: objek LEBAR (128 member) + kueri path berulang.
    // Kebijakan: kueri berat pertama murni sekuensial, kedua membangun
    // indeks, berikutnya lompatan langsung — seluruhnya transparen bagi
    // pemanggil (hasil & status identik).
    std::string json = "{";
    for (int n = 0; n < 128; ++n) {
        if (n != 0)
            json += ",";
        char label[8];
        std::snprintf(label, sizeof(label), "k%04d", n);
        json += std::string("\"") + label + "\":\"" + std::to_string(1000 + n) +
                "\"";
    }
    json += "}";

    alignas(8) std::vector<types::uint8_t> buffer(1024 * 1024);
    memory::Arena* arena = create_arena(buffer.data(), buffer.size());
    ASSERT_NE(arena, nullptr);
    std::vector<types::uint64_t> wide_tape(json.size());

    types::int32_t err = api::OK;
    auto* session      = create_session(
        json.data(), json.size(), wide_tape.data(), wide_tape.size(), arena,
        err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    const char* data  = nullptr;
    types::size_t len = 0;
    types::int32_t st = api::OK;

    // Kueri berat-1 (belum ada indeks): kunci tengah tetap ketemu.
    st = api::OK;
    get_string_at(session, "k0064", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "1064");

    // Kueri berat-2: pembangunan indeks terjadi di sini; hasil identik.
    st = api::OK;
    get_string_at(session, "k0065", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "1065");

    // Terpenting: SEMUA arah kini lompatan lewat indeks — depan, tengah,
    // belakang memberikan hasil yang sama.
    st = api::OK;
    get_string_at(session, "k0000", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "1000");

    st = api::OK;
    get_string_at(session, "k0064", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "1064");

    st = api::OK;
    get_string_at(session, "k0127", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "1127");

    // Kunci yang TIDAK ADA: indeks menjawab NOT_FOUND; sesi tetap hidup.
    st = api::OK;
    get_string_at(session, "k9999", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    st = api::OK;
    get_string_at(session, "k0021", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "1021");

    // Perjalanan bauran find() sekuensial TETAP valid (indeks hanya
    // menyentuh jalur find_path segmen pertama; find() tidak diubah).
    // Kursor bersifat forward-only: pasca kueri k0064/k0065, hanya member
    // DI ATAS k0065 yang masih jangkauan find() — k0100.
    EXPECT_EQ(find_field(session, "k0100", 5), 1);
    get_string(session, data, len);
    EXPECT_EQ(std::string_view(data, len), "1100");

    // Path setelah find() sekuensial: tetap rewind + indeks.
    st = api::OK;
    get_string_at(session, "k0021", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "1021");

    destroy_arena(arena);
}

TEST_F(PathTest, RootWideIndexDuplicateKeyKeepsFirst) {
    // Semantik dup key: kemunculan PERTAMA, baik sekuensial maupun setelah
    // indeks dibangun.
    std::string json = "{";
    for (int n = 0; n < 48; ++n) {
        if (n != 0)
            json += ",";
        json += "\"dup\":\"v" + std::to_string(n) + "\"";
    }
    json += "}";

    alignas(8) std::vector<types::uint8_t> buffer(1024 * 1024);
    memory::Arena* arena = create_arena(buffer.data(), buffer.size());
    std::vector<types::uint64_t> wide_tape(json.size());

    types::int32_t err = api::OK;
    auto* session      = create_session(
        json.data(), json.size(), wide_tape.data(), wide_tape.size(), arena,
        err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    const char* data  = nullptr;
    types::size_t len = 0;
    types::int32_t st = api::OK;

    // Dua kueri berat membangun indeks; hasil wajib masih "v0".
    get_string_at(session, "dup", 3, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0");

    st = api::OK;
    get_string_at(session, "dup", 3, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0");

    // Berikutnya (indeks aktif): PERTAMA — bukan kemunculan lain.
    st = api::OK;
    get_string_at(session, "dup", 3, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0");

    destroy_arena(arena);
}

TEST_F(PathTest, RootWideIndexNestedObjects) {
    // Nilai bersarang: segmen pertama di-resolve lewat indeks, lanjutan
    // dipegang sekuensial — path "kXXXX.v" tetap benar.
    std::string json = "{";
    for (int n = 0; n < 64; ++n) {
        if (n != 0)
            json += ",";
        char label[8];
        std::snprintf(label, sizeof(label), "k%04d", n);
        json +=
            std::string("\"") + label + "\":{\"v\":" + std::to_string(n) + "}";
    }
    json += "}";

    alignas(8) std::vector<types::uint8_t> buffer(1024 * 1024);
    memory::Arena* arena = create_arena(buffer.data(), buffer.size());
    std::vector<types::uint64_t> wide_tape(json.size());

    types::int32_t err = api::OK;
    auto* session      = create_session(
        json.data(), json.size(), wide_tape.data(), wide_tape.size(), arena,
        err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    types::int32_t st = api::OK;
    // Berat-1: sekuensial murni.
    EXPECT_EQ(get_int_at(session, "k0015.v", 7, st), 15);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Berat-2: pembangunan indeks; nested setelahnya lewat lompatan kursor.
    EXPECT_EQ(get_int_at(session, "k0016.v", 7, st), 16);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    EXPECT_EQ(get_int_at(session, "k0063.v", 7, st), 63);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    EXPECT_EQ(get_int_at(session, "k0000.v", 7, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Kontainer pada lompatan: kursor wajib tepat pada '{' nilai, bukan
    // pada ':' ataupun member lain.
    EXPECT_EQ(get_int_at(session, "k0032.v", 7, st), 32);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    destroy_arena(arena);
}

TEST_F(PathTest, RootWideIndexEscapeKeyUnchanged) {
    // Kunci mengandung escape: kontrak lama — pencarian sekuensial
    // membandingkan hasil unescape, sehingga path teks pintas TIDAK
    // pernah cocok (baik tanpa maupun dengan indeks aktif). Kunci
    // ber-escape tidak dimasukkan ke indeks; miss wajib tetap dijawab
    // fallback sekuensial supaya hasil identik di semua mode.
    std::string json = "{";
    for (int n = 0; n < 32; ++n) {
        if (n != 0)
            json += ",";
        json += "\"f" + std::to_string(n) + "\":\"" + std::to_string(n) + "\"";
    }
    json += R"(,"na\ty":"on-time"})";

    alignas(8) std::vector<types::uint8_t> buffer(1024 * 1024);
    memory::Arena* arena = create_arena(buffer.data(), buffer.size());
    std::vector<types::uint64_t> wide_tape(json.size());

    types::int32_t err = api::OK;
    auto* session      = create_session(
        json.data(), json.size(), wide_tape.data(), wide_tape.size(), arena,
        err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    const char* data  = nullptr;
    types::size_t len = 0;
    types::int32_t st = api::OK;

    // Kueri berat-1 (sekuensial murni): kunci escape tak terjangkau.
    get_string_at(session, "na\\ty", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    // Kueri berat-2: indeks dibangun (kunci escape dikecualikan).
    st = api::OK;
    get_string_at(session, "f10", 3, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "10");

    // Pasca-indeks: kueri kunci escape murni lewat fallback — keputusan
    // wajib serupa dengan mode sekuensial (NOT_FOUND).
    st = api::OK;
    get_string_at(session, "na\\ty", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    // Kunci murni lain tetap lompatan O(1).
    st = api::OK;
    get_string_at(session, "f28", 3, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "28");

    destroy_arena(arena);
}

TEST_F(PathTest, ArrayIndexPathQueries) {
    // Indeks elemen array lazy: array LEBAR (128 elemen string) + kueri
    // [index] berulang. Kebijakan mirror indeks root: kueri berat pertama
    // sekuensial murni, kedua membangun tabel posisi, berikutnya O(1).
    std::string json = "{\"items\":[";
    for (int n = 0; n < 128; ++n) {
        if (n != 0)
            json += ",";
        char label[8];
        std::snprintf(label, sizeof(label), "v%04d", n);
        json += std::string("\"") + label + "\"";
    }
    json += "],\"after\":9}";

    alignas(8) std::vector<types::uint8_t> buffer(1024 * 1024);
    memory::Arena* arena = create_arena(buffer.data(), buffer.size());
    ASSERT_NE(arena, nullptr);
    std::vector<types::uint64_t> wide_tape(json.size());

    types::int32_t err = api::OK;
    auto* session      = create_session(
        json.data(), json.size(), wide_tape.data(), wide_tape.size(), arena,
        err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    const char* data  = nullptr;
    types::size_t len = 0;
    types::int32_t st = api::OK;

    // Berat-1 (belum ada tabel): kunci tengah tetap ketemu.
    st = api::OK;
    get_string_at(session, "items[64]", 9, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0064");

    // Berat-2: pembangunan tabel terjadi di sini.
    st = api::OK;
    get_string_at(session, "items[65]", 9, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0065");

    // Semua arah kini lompatan: depan, tengah, belakang.
    st = api::OK;
    get_string_at(session, "items[0]", 8, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0000");

    st = api::OK;
    get_string_at(session, "items[64]", 9, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0064");

    st = api::OK;
    get_string_at(session, "items[127]", 10, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0127");

    // Indeks luar jangkauan: NOT_FOUND; sesi tetap hidup.
    st = api::OK;
    get_string_at(session, "items[128]", 10, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    st = api::OK;
    get_string_at(session, "items[9999]", 11, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    st = api::OK;
    get_string_at(session, "items[21]", 9, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0021");

    // Kueri root lain pasca-indeks: rewind + indeks root tetap benar.
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "after", 5, st), 9);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    destroy_arena(arena);
}

TEST_F(PathTest, ArrayIndexMixedTypesAndNumbers) {
    // Array campuran tipe > kuota: lompatan per elemen wajib meninggalkan
    // kursor PERSIS seperti hasil sekuensial — termasuk angka 0-entry
    // (kursor di terminator ',') dan angka TERAKHIR (kursor di ']' +
    // gap angka). Pola 7 elemen diulang 8× (indeks 0..55).
    std::string json = "{\"mix\":[";
    for (int b = 0; b < 8; ++b) {
        if (b != 0)
            json += ",";
        json += R"(1,"two",true,null,[7,8],{"k":4},-8.5)";
    }
    json += "],\"after\":5}";

    alignas(8) std::vector<types::uint8_t> buffer(1024 * 1024);
    memory::Arena* arena = create_arena(buffer.data(), buffer.size());
    std::vector<types::uint64_t> wide_tape(json.size());

    types::int32_t err = api::OK;
    auto* session      = create_session(
        json.data(), json.size(), wide_tape.data(), wide_tape.size(), arena,
        err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    const char* data  = nullptr;
    types::size_t len = 0;
    types::int32_t st = api::OK;

    // Berat-1: angka (pattern index 0) di posisi 49.
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "mix[49]", 7, st), 1);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Berat-2 (tabel dibangun): string di posisi 50 (pattern index 1).
    st = api::OK;
    get_string_at(session, "mix[50]", 7, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "two");

    // Boolean (pattern 2) & null (pattern 3).
    st = api::OK;
    EXPECT_EQ(get_bool_at(session, "mix[51]", 7, st), 1);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "mix[52]", 7, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::TYPE_MISMATCH); // null

    // Array bersarang (pattern 4): segmen [index] LANJUTAN pasca-lompatan.
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "mix[53][1]", 10, st), 8);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Object bersarang (pattern 5): segmen key LANJUTAN pasca-lompatan.
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "mix[54].k", 9, st), 4);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Angka TERAKHIR (pattern 6, index 55): kursor ']' + gap angka.
    st = api::OK;
    EXPECT_EQ(get_double_at(session, "mix[55]", 7, st), -8.5);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Angka di tengah (pattern 0, index 28): kursor terminator ','.
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "mix[28]", 7, st), 1);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    // Out-of-range pasca-tabel.
    st = api::OK;
    EXPECT_EQ(get_int_at(session, "mix[56]", 7, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    destroy_arena(arena);
}

TEST_F(PathTest, ArrayIndexSwitchArrays) {
    // Tabel terikat identitas array: kueri lintas array memicu rebuild
    // (blok arena dipakai ulang) — hasil tidak pernah nyasar antar array.
    std::string json = "{\"a\":[";
    for (int n = 0; n < 40; ++n) {
        if (n != 0)
            json += ",";
        char label[8];
        std::snprintf(label, sizeof(label), "v%04d", n);
        json += std::string("\"") + label + "\"";
    }
    json += "],\"b\":[";
    for (int n = 0; n < 40; ++n) {
        if (n != 0)
            json += ",";
        char label[8];
        std::snprintf(label, sizeof(label), "w%04d", n);
        json += std::string("\"") + label + "\"";
    }
    json += "],\"after\":7}";

    alignas(8) std::vector<types::uint8_t> buffer(1024 * 1024);
    memory::Arena* arena = create_arena(buffer.data(), buffer.size());
    std::vector<types::uint64_t> wide_tape(json.size());

    types::int32_t err = api::OK;
    auto* session      = create_session(
        json.data(), json.size(), wide_tape.data(), wide_tape.size(), arena,
        err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    const char* data  = nullptr;
    types::size_t len = 0;
    types::int32_t st = api::OK;

    // Bangun tabel untuk array "a" (dua kueri berat).
    get_string_at(session, "a[30]", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0030");

    st = api::OK;
    get_string_at(session, "a[31]", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0031");

    // Pindah ke array "b": tabel a tidak boleh bocor.
    st = api::OK;
    get_string_at(session, "b[30]", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "w0030");

    st = api::OK;
    get_string_at(session, "b[39]", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "w0039");

    // Kembali ke "a": rebuild — hasil tetap benar.
    st = api::OK;
    get_string_at(session, "a[39]", 5, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "v0039");

    st = api::OK;
    get_string_at(session, "b[0]", 4, data, len, st);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(std::string_view(data, len), "w0000");

    destroy_arena(arena);
}

TEST_F(PathTest, ArrayIndexNumberOnlyAndAmbiguity) {
    // Array full angka + kasus ambigu '[7]' vs '[]' pada tape serupa.
    // Array kecil (≤ kuota) TIDAK membangun tabel — kontrak sekuensial
    // lama utuh; array besar angka mengetes seek terminator 0-entry
    // termasuk elemen TERAKHIR (']' + gap angka).
    alignas(8) constexpr const char payload[] =
        R"({"nums":[1,2,3],"one":[7],"empty":[],"after":5})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::int32_t err = api::OK;
    auto* session      = make_session(payload, payload_size, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    types::int32_t st = api::OK;
    EXPECT_EQ(get_int_at(session, "nums[0]", 7, st), 1);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(get_int_at(session, "nums[2]", 7, st), 3);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(get_int_at(session, "nums[3]", 7, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "one[0]", 6, st), 7);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);
    EXPECT_EQ(get_int_at(session, "one[1]", 6, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "empty[0]", 8, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);
    arena_handle->reset();

    // Array besar full angka: dua kueri berat membangun tabel; seluruh
    // arah + elemen terakhir (']' + gap angka) wajib benar.
    std::string json = "{\"big\":[";
    for (int n = 0; n < 100; ++n) {
        if (n != 0)
            json += ",";
        json += std::to_string(n);
    }
    json += "],\"after\":5}";

    alignas(8) std::vector<types::uint8_t> buffer(1024 * 1024);
    memory::Arena* arena = create_arena(buffer.data(), buffer.size());
    std::vector<types::uint64_t> wide_tape(json.size());

    err     = api::OK;
    session = create_session(
        json.data(), json.size(), wide_tape.data(), wide_tape.size(), arena,
        err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "big[90]", 7, st), 90);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "big[91]", 7, st), 91);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "big[99]", 7, st), 99);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "big[0]", 6, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::OK);

    st = api::OK;
    EXPECT_EQ(get_int_at(session, "big[100]", 8, st), 0);
    EXPECT_EQ(static_cast<api::Status>(st), api::NOT_FOUND);

    destroy_arena(arena);
}

} // namespace zuu::testing
