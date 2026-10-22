#include <gtest/gtest.h>
#include <string_view>

#include <ze_json/ondemand/document.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/ze_json.hpp>

namespace zuu::testing {

class ParserTest : public ::testing::Test {
  protected:
    // Helper: mengonversi types::StringView ke std::string_view untuk asersi
    // GTest. StringView zero-copy TIDAK null-terminated (berujung langsung di
    // payload mentah), sehingga EXPECT_STREQ tidak bisa dipakai.
    std::string_view
    to_sv(const types::StringView& sv) {
        return std::string_view(sv.data, sv.length);
    }

    types::uint8_t buffer[1024 * 1024]{}; // allocate 1 MB
    memory::Arena* arena_handle{nullptr};

    ParserTest() noexcept
        : arena_handle(create_arena(buffer, sizeof(buffer))) {}
};

TEST_F(ParserTest, DocumentTypes) {
    alignas(8) constexpr const char payload[] =
        R"({"str":"hello","num":123,"flag":true,"null_val":null,"arr":[1,2],"obj":{}})";
    constexpr auto payload_size = sizeof(payload) - 1;

    ASSERT_NE(arena_handle, nullptr);

    constexpr types::size_t tape_capacity = payload_size;
    auto* tape = static_cast<types::size_t*>(arena_handle->allocate(
        tape_capacity * sizeof(types::size_t), alignof(types::size_t)));

    ASSERT_NE(tape, nullptr);

    auto res = scanner::scanner8(payload, payload_size, tape, tape_capacity);

    ASSERT_TRUE(res.has_value());

    const types::size_t tape_count = res.value();

    ASSERT_LE(tape_count, tape_capacity);

    const types::size_t* const_tape = tape;

    ondemand::Document doc(payload, const_tape, tape_count, arena_handle);

    ondemand::JsonValue root = doc.get_root();

    EXPECT_EQ(root.type(), ondemand::JsonType::Object);

    arena_handle->reset();
}

TEST_F(ParserTest, ObjectFieldLookup) {
    alignas(8) constexpr const char payload[] =
        R"({"name":"zuu","role":"parser","active":true})";
    constexpr auto payload_size = sizeof(payload) - 1;

    auto tape_handle = arena_handle->allocate(
        payload_size * sizeof(types::size_t), sizeof(types::size_t));
    auto tape_size =
        static_cast<types::size_t>(payload_size); // satuan entry, bukan byte
    auto tape       = reinterpret_cast<types::size_t*>(tape_handle);
    auto const_tape = reinterpret_cast<const types::size_t*>(tape_handle);

    auto scan_res = scanner::scanner8(payload, payload_size, tape, tape_size);
    ASSERT_TRUE(scan_res.has_value());

    ondemand::Document doc(payload, const_tape, scan_res.value(), arena_handle);
    ondemand::JsonObject obj = doc.get_root().get_object();

    // 1. Cari "name"
    ondemand::JsonValue name_val = obj.find_field(types::StringView("name", 4));
    EXPECT_EQ(name_val.type(), ondemand::JsonType::String);
    EXPECT_EQ(to_sv(name_val.get_string()), "zuu");

    // 2. Cari "active" (melompati field "role" secara otomatis)
    ondemand::JsonValue active_val =
        obj.find_field(types::StringView("active", 6));
    EXPECT_EQ(active_val.type(), ondemand::JsonType::Boolean);
    EXPECT_TRUE(active_val.get_bool());

    arena_handle->reset();
}

TEST_F(ParserTest, LemireNumberParsing) {
    alignas(8) constexpr const char payload[] =
        R"({"int_val":42,"neg_val":-2024,"double_val":3.1415926535,"exp_val":1.23e4})";
    constexpr auto payload_size = sizeof(payload) - 1;

    auto tape_handle = arena_handle->allocate(
        payload_size * sizeof(types::size_t), sizeof(types::size_t));
    auto tape_size =
        static_cast<types::size_t>(payload_size); // satuan entry, bukan byte
    auto tape       = reinterpret_cast<types::size_t*>(tape_handle);
    auto const_tape = reinterpret_cast<const types::size_t*>(tape_handle);

    auto scan_res = scanner::scanner8(payload, payload_size, tape, tape_size);
    ASSERT_TRUE(scan_res.has_value());

    ondemand::Document doc(payload, const_tape, scan_res.value(), arena_handle);
    ondemand::JsonObject obj = doc.get_root().get_object();

    // Integer parsing
    ondemand::JsonValue int_node =
        obj.find_field(types::StringView("int_val", 7));
    EXPECT_EQ(int_node.get_int(), 42);

    ondemand::JsonValue neg_node =
        obj.find_field(types::StringView("neg_val", 7));
    EXPECT_EQ(neg_node.get_int(), -2024);

    // Double parsing
    ondemand::JsonValue double_node =
        obj.find_field(types::StringView("double_val", 10));
    EXPECT_NEAR(double_node.get_double(), 3.1415926535, 1e-9);

    ondemand::JsonValue exp_node =
        obj.find_field(types::StringView("exp_val", 7));
    EXPECT_NEAR(exp_node.get_double(), 12300.0, 1e-5);

    arena_handle->reset();
}

TEST_F(ParserTest, SkipNestedStructures) {
    // Deeply nested JSON structure
    alignas(8) constexpr const char payload[] =
        R"({"matrix":[[1,2],[3,4]],"meta":{"deep":{"a":1}},"target":"found"})";
    constexpr auto payload_size = sizeof(payload) - 1;

    auto tape_handle = arena_handle->allocate(
        payload_size * sizeof(types::size_t), sizeof(types::size_t));
    auto tape_size =
        static_cast<types::size_t>(payload_size); // satuan entry, bukan byte
    auto tape       = reinterpret_cast<types::size_t*>(tape_handle);
    auto const_tape = reinterpret_cast<const types::size_t*>(tape_handle);

    auto scan_res = scanner::scanner8(payload, payload_size, tape, tape_size);
    ASSERT_TRUE(scan_res.has_value());

    ondemand::Document doc(payload, const_tape, scan_res.value(), arena_handle);
    ondemand::JsonObject obj = doc.get_root().get_object();

    // Mencari "target" secara langsung, memaksa skip_child melompati array
    // bersarang dan object bersarang
    ondemand::JsonValue target_val =
        obj.find_field(types::StringView("target", 6));
    EXPECT_EQ(target_val.type(), ondemand::JsonType::String);
    EXPECT_EQ(to_sv(target_val.get_string()), "found");

    arena_handle->reset();
}

TEST_F(ParserTest, SessionArrayTraversal) {
    // Traversal array via API publik: campuran semua tipe elemen, termasuk
    // kontainer bersarang yang di-skip dan angka negatif-pecahan (0 entry).
    alignas(8) constexpr const char payload[] =
        R"({"colors":[1,"two",true,null,[7,8],{"k":4},-8.5],"after":99})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::uint64_t tape[256]{};
    types::int32_t err = static_cast<types::int32_t>(api::INVALID_JSON);
    auto* session =
        create_session(payload, payload_size, tape, 256, arena_handle, err);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(static_cast<api::Status>(err), api::OK);

    ASSERT_EQ(find_field(session, "colors", 6), 1);
    EXPECT_EQ(static_cast<api::ValueType>(value_type(session)),
              api::ValueType::Array);
    EXPECT_EQ(array_size(session), 7u);

    // Elemen 0: angka (0 entry — kursor di terminatornya)
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(static_cast<api::ValueType>(value_type(session)),
              api::ValueType::Number);
    EXPECT_EQ(get_int(session), 1);

    // Elemen 1: string
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(static_cast<api::ValueType>(value_type(session)),
              api::ValueType::String);
    const char* str_data = nullptr;
    types::size_t str_len = 0;
    get_string(session, str_data, str_len);
    EXPECT_EQ(std::string_view(str_data, str_len), "two");

    // Elemen 2: boolean (dibaca); elemen 3: null (dikonsumsi via get_bool)
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(get_bool(session), 1);
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(static_cast<api::ValueType>(value_type(session)),
              api::ValueType::Null);
    EXPECT_EQ(get_bool(session), 0); // 'n' != 't' → false, tapi tetap konsumsi

    // Elemen 4: array bersarang — TIDAK dibaca, array_next harus skip
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(static_cast<api::ValueType>(value_type(session)),
              api::ValueType::Array);

    // Elemen 5: object bersarang — juga di-skip
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(static_cast<api::ValueType>(value_type(session)),
              api::ValueType::Object);

    // Elemen 6: angka negatif-pecahan
    EXPECT_EQ(array_next(session), 1);
    EXPECT_EQ(static_cast<api::ValueType>(value_type(session)),
              api::ValueType::Number);
    EXPECT_NEAR(get_double(session), -8.5, 1e-9);

    // Elemen habis; kursor kembali ke ']' agar object root bisa dilanjutkan
    EXPECT_EQ(array_next(session), 0);
    ASSERT_EQ(find_field(session, "after", 5), 1);
    EXPECT_EQ(static_cast<api::ValueType>(value_type(session)),
              api::ValueType::Number);
    EXPECT_EQ(get_int(session), 99);

    arena_handle->reset();
}

TEST_F(ParserTest, SessionArrayEdgeCases) {
    // Kasus ambigu 0-entry: '[1]' vs '[]' menghasilkan tape identik,
    // ditambah elemen angka terakhir dan angka setelah string.
    alignas(8) constexpr const char payload[] =
        R"({"empty":[],"one":[1],"nums":[1,2],"mixed":["a",3],"tail":[5],"next":7})";
    constexpr auto payload_size = sizeof(payload) - 1;

    types::uint64_t tape[256]{};
    types::int32_t err = static_cast<types::int32_t>(api::INVALID_JSON);
    auto* session =
        create_session(payload, payload_size, tape, 256, arena_handle, err);
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
    const char* str_data = nullptr;
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

} // namespace zuu::testing