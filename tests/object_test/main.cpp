// Kelompok uji: OBJECT — semantik JsonObject di lapisan Document:
// pencarian field, pelompatan kontainer bersarang, tipe root, string escape.
#include <gtest/gtest.h>
#include <string_view>

#include <ze_json/ondemand/document.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/ze_json.hpp>

namespace zuu::testing {

// Helper: mengonversi types::StringView ke std::string_view untuk asersi
// GTest. StringView zero-copy TIDAK null-terminated (berujung langsung di
// payload mentah), sehingga EXPECT_STREQ tidak bisa dipakai.
static std::string_view
to_sv(const types::StringView& sv) {
    return std::string_view(sv.data, sv.length);
}

class ObjectTest : public ::testing::Test {
  protected:
    // Bangun Document dari payload: scanner → tape di arena → ctor.
    // Mengembalikan jumlah entry tape (0 bila scan gagal).
    types::size_t
    build_document(
        const char* payload, types::size_t payload_size,
        ondemand::Document** out_doc) {
        auto tape_handle = arena_handle->allocate(
            payload_size * sizeof(types::size_t), sizeof(types::size_t));
        auto* tape       = static_cast<types::size_t*>(tape_handle);
        auto* const_tape = static_cast<const types::size_t*>(tape_handle);

        auto scan_res =
            scanner::scanner8(payload, payload_size, tape, payload_size);
        if (!scan_res.has_value()) {
            return 0;
        }

        *out_doc = new (arena_handle->allocate(
            sizeof(ondemand::Document), alignof(ondemand::Document)))
            ondemand::Document(
                payload, const_tape, scan_res.value(), arena_handle);
        return scan_res.value();
    }

    types::uint8_t buffer[1024 * 1024]{}; // allocate 1 MB
    memory::Arena* arena_handle{nullptr};

    ObjectTest() noexcept
        : arena_handle(create_arena(buffer, sizeof(buffer))) {}
};

TEST_F(ObjectTest, RootTypes) {
    // Tipe root diambil dari karakter struktural awal tape.
    alignas(8) constexpr const char payload[] =
        R"({"str":"hello","num":123,"flag":true,"null_val":null,"arr":[1,2],"obj":{}})";
    constexpr auto payload_size = sizeof(payload) - 1;

    ondemand::Document* doc = nullptr;
    ASSERT_NE(build_document(payload, payload_size, &doc), 0u);

    ondemand::JsonValue root = doc->get_root();
    EXPECT_EQ(root.type(), ondemand::JsonType::Object);

    arena_handle->reset();
}

TEST_F(ObjectTest, FieldLookupAndLiteralValues) {
    alignas(8) constexpr const char payload[] =
        R"({"name":"zuu","role":"parser","active":true})";
    constexpr auto payload_size = sizeof(payload) - 1;

    ondemand::Document* doc = nullptr;
    ASSERT_NE(build_document(payload, payload_size, &doc), 0u);

    ondemand::JsonObject obj = doc->get_root().get_object();

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

TEST_F(ObjectTest, MissedFindIsReported) {
    // Varian checked: key yang tidak ada → false; kontrak kursor:
    // pencarian berhenti di akhir object. Untuk mencari ulang, Document
    // harus di-rewind (kursor forward-only, tidak bisa mundur sendiri).
    alignas(8) constexpr const char payload[] =
        R"({"name":"zuu","active":true})";
    constexpr auto payload_size = sizeof(payload) - 1;

    ondemand::Document* doc = nullptr;
    ASSERT_NE(build_document(payload, payload_size, &doc), 0u);

    ondemand::JsonObject obj = doc->get_root().get_object();
    ondemand::JsonValue out(doc->get_root());

    EXPECT_FALSE(obj.find_field_checked(types::StringView("missing", 7), &out));

    // Rewind lalu cari ulang pada object yang fresh.
    doc->rewind();
    ondemand::JsonObject fresh = doc->get_root().get_object();
    EXPECT_TRUE(fresh.find_field_checked(types::StringView("active", 6), &out));
    EXPECT_EQ(out.type(), ondemand::JsonType::Boolean);

    arena_handle->reset();
}

TEST_F(ObjectTest, SkipNestedStructures) {
    // Mencari "target" memaksa find_field melompati array bersarang
    // dan object bersarang (skip_child satu kurung-penuh).
    alignas(8) constexpr const char payload[] =
        R"({"matrix":[[1,2],[3,4]],"meta":{"deep":{"a":1}},"target":"found"})";
    constexpr auto payload_size = sizeof(payload) - 1;

    ondemand::Document* doc = nullptr;
    ASSERT_NE(build_document(payload, payload_size, &doc), 0u);

    ondemand::JsonObject obj = doc->get_root().get_object();

    ondemand::JsonValue target_val =
        obj.find_field(types::StringView("target", 6));
    EXPECT_EQ(target_val.type(), ondemand::JsonType::String);
    EXPECT_EQ(to_sv(target_val.get_string()), "found");

    arena_handle->reset();
}

TEST_F(ObjectTest, EscapedStringsAreUnescaped) {
    // get_string meng-unescape \n, \" dan \u pada aliran arena (bukan
    // teks mentah payload).
    alignas(8) constexpr const char payload[] =
        R"({"newline":"a\nb","quote":"s\"x","uni":"A\u00eb"})";
    constexpr auto payload_size = sizeof(payload) - 1;

    ondemand::Document* doc = nullptr;
    ASSERT_NE(build_document(payload, payload_size, &doc), 0u);

    ondemand::JsonObject obj = doc->get_root().get_object();

    ondemand::JsonValue newline_val =
        obj.find_field(types::StringView("newline", 7));
    EXPECT_EQ(to_sv(newline_val.get_string()), std::string_view("a\nb", 3));

    ondemand::JsonValue quote_val =
        obj.find_field(types::StringView("quote", 5));
    EXPECT_EQ(to_sv(quote_val.get_string()), std::string_view("s\"x", 3));

    // "A\u00eb" → 'A' + UTF-8 "ë" (0xC3 0xAB)
    ondemand::JsonValue uni_val = obj.find_field(types::StringView("uni", 3));
    EXPECT_EQ(to_sv(uni_val.get_string()), std::string_view("A\xC3\xAB", 3));

    arena_handle->reset();
}

} // namespace zuu::testing
