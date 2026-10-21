#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// Include library ze_json Anda (Sesuaikan path jika diperlukan)
#include <ze_json/ondemand/document.hpp>
#include <ze_json/scanner/scanner.hpp>

using namespace zuu;

// Helper function untuk GTest: Konversi zuu::ondemand::StringView
// ke std::string_view agar mudah dibandingkan menggunakan EXPECT_EQ
inline std::string_view
to_std_view(const types::StringView& sv) {
    return std::string_view(sv.data, sv.length);
}

// =========================================================================
// TEST SUITE 1: Menguji Scanner (Pembuat Tape Index)
// =========================================================================
TEST(ZeJsonScannerTest, ScanValidPayload) {
    std::string payload =
        R"({"name":"zuu","age":21,"active":true,"status":null,"colors":["white","black"],"pos":{"x":24,"y":-24}})";

    // Alokasi memori dinamis di Heap menggunakan std::vector (Hanya untuk
    // pengujian) Kapasitas buffer dst kita buat sama dengan ukuran payload
    // untuk berjaga-jaga
    std::vector<types::size_t> tape(payload.size());

    // 1. Eksekusi Scanner
    auto result = scanner::scanner8(
        payload.data(), payload.size(), tape.data(), tape.size());

    // 2. Asersi Keberhasilan Scanner
    // Asumsi Result memiliki method .has_value() atau serupa ala std::expected
    ASSERT_TRUE(result.has_value()) << "Scanner gagal, error code didapatkan.";

    types::size_t tape_length = result.getValue();

    // Berdasarkan hasil JS Anda sebelumnya, panjang tape struktural harus 45
    EXPECT_EQ(tape_length, 45);

    // 3. Asersi Nilai Tape (Mengecek posisi karakter tertentu)
    // Index 0 haruslah karakter '{' di offset 0
    EXPECT_EQ(payload[tape[0]], '{');
    EXPECT_EQ(tape[0], 0);

    // Index 1 haruslah karakter '"' di offset 1 (awal dari "name")
    EXPECT_EQ(payload[tape[1]], '"');
    EXPECT_EQ(tape[1], 1);

    // Index 3 haruslah karakter ':' di offset 7
    EXPECT_EQ(payload[tape[3]], ':');
    EXPECT_EQ(tape[3], 7);
}

// =========================================================================
// TEST SUITE 2: Menguji On-Demand Parser (Navigasi JSON)
// =========================================================================
TEST(ZeJsonOnDemandTest, ParseAndFindFields) {
    std::string payload =
        R"({"name":"zuu","age":21,"active":true,"status":null})";

    // Persiapan Scanner Tape
    std::vector<types::size_t> tape(payload.size());
    auto scan_res = scanner::scanner8(
        payload.data(), payload.size(), tape.data(), tape.size());
    ASSERT_TRUE(scan_res.has_value());
    types::size_t tape_length = scan_res.getValue();

    // 1. Inisialisasi Document
    // GTest meniru perilaku Node.js: Memberikan source teks dan tape index ke
    // Document
    ondemand::Document doc(payload.data(), tape.data(), tape_length);

    // 2. Ambil Root Node (Harus Object)
    ondemand::JsonValue root = doc.get_root();
    EXPECT_EQ(root.type(), ondemand::JsonType::Object);

    // 3. Masuk ke dalam Object
    ondemand::JsonObject root_obj = root.get_object();

    // 4. Cari field "name"
    ondemand::JsonValue name_val =
        root_obj.find_field(types::StringView("name", 4));

    // Pastikan ketemu dan tipenya String
    ASSERT_EQ(name_val.type(), ondemand::JsonType::String);
    EXPECT_EQ(to_std_view(name_val.get_string()), "zuu");

    // 5. Uji pencarian properti boolean ("active": true)
    // Karena forward-only linear search, kita harus mencari property SETELAH
    // "name" Pada implementasi penuh, ondemand biasanya mereset kursor iterasi
    // ke awal object jika user meminta pencarian kunci yang out-of-order.
    ondemand::JsonValue active_val =
        root_obj.find_field(types::StringView("active", 6));
    ASSERT_EQ(active_val.type(), ondemand::JsonType::Boolean);
    EXPECT_TRUE(active_val.get_bool());
}