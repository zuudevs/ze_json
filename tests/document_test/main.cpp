#include <gtest/gtest.h>
#include <string_view>

#include <ze_json/ondemand/document.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/ze_json.hpp>

using namespace zuu;

// Helper: mengonversi types::StringView ke std::string_view untuk asersi
// GTest. StringView zero-copy TIDAK null-terminated (berujung langsung di
// payload mentah), sehingga EXPECT_STREQ tidak bisa dipakai.
static std::string_view
to_sv(const types::StringView& sv) {
    return std::string_view(sv.data, sv.length);
}

class Memory : public ::testing::Test {
  protected:
    types::uint8_t buffer[1024 * 1024]{}; // allocate 1 MB
    memory::Arena* arena_handle{nullptr};

    Memory() noexcept : arena_handle(create_arena(buffer, sizeof(buffer))) {}
};

TEST_F(Memory, ScanValidPayload) {
    alignas(8) constexpr const char payload[] =
        R"({"name":"zuu","age":21,"active":true,"status":null,"colors":["white","black"],"pos":{"x":24,"y":-24}})";
    constexpr auto payload_size = sizeof(payload) - 1;

    auto tape_handle = arena_handle->allocate(
        payload_size * sizeof(types::size_t), sizeof(types::size_t));
    auto tape_size  = payload_size; // satuan entry, bukan byte
    auto tape       = reinterpret_cast<types::size_t*>(tape_handle);
    auto const_tape = reinterpret_cast<const types::size_t*>(tape_handle);

    // 1. Eksekusi Scanner
    auto result = scanner::scanner8(payload, payload_size, tape, tape_size);

    ASSERT_TRUE(result.has_value()) << "Scanner gagal, error code didapatkan.";

    types::size_t tape_length = result.value();

    EXPECT_EQ(tape_length, 45);
    EXPECT_EQ(payload[const_tape[0]], '{');
    EXPECT_EQ(const_tape[0], 0);

    // Index 1 haruslah karakter '"' di offset 1 (awal dari "name")
    EXPECT_EQ(payload[const_tape[1]], '"');
    EXPECT_EQ(const_tape[1], 1);

    // Index 3 haruslah karakter ':' di offset 7
    EXPECT_EQ(payload[const_tape[3]], ':');
    EXPECT_EQ(const_tape[3], 7);

    arena_handle->reset();
}

TEST_F(Memory, ParseAndFindFields) {
    alignas(8) constexpr const char payload[] =
        R"({"name":"zuu","age":21,"active":true,"status":null})";
    constexpr auto payload_size = sizeof(payload) - 1;

    auto tape_handle = arena_handle->allocate(
        payload_size * sizeof(types::size_t), sizeof(types::size_t));
    auto tape_size  = payload_size; // satuan entry, bukan byte
    auto tape       = reinterpret_cast<types::size_t*>(tape_handle);
    auto const_tape = reinterpret_cast<const types::size_t*>(tape_handle);

    auto result = scanner::scanner8(payload, payload_size, tape, tape_size);
    ASSERT_TRUE(result.has_value());
    types::size_t tape_length = result.value();

    // 1. Inisialisasi Document
    ondemand::Document doc(
        payload, const_tape, result.value(), tape + result.value());

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
    EXPECT_EQ(to_sv(name_val.get_string()), "zuu");

    // 5. Uji pencarian properti boolean ("active": true)
    // Karena forward-only linear search, kita harus mencari property SETELAH
    // "name" Pada implementasi penuh, ondemand biasanya mereset kursor iterasi
    // ke awal object jika user meminta pencarian kunci yang out-of-order.
    ondemand::JsonValue active_val =
        root_obj.find_field(types::StringView("active", 6));
    ASSERT_EQ(active_val.type(), ondemand::JsonType::Boolean);
    EXPECT_TRUE(active_val.get_bool());

    arena_handle->reset();
}