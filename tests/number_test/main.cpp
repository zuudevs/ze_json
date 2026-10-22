// Kelompok uji: NUMBER — parsing angka ( Lemire) di lapisan Document:
// integer/pecahan/eksponen, batas int64, dan bentuk batas eksponen.
#include <gtest/gtest.h>
#include <string_view>

#include <ze_json/ondemand/document.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/ze_json.hpp>

namespace zuu::testing {

class NumberTest : public ::testing::Test {
  protected:
    ondemand::Document* build_document(const char* payload,
                                       types::size_t payload_size) {
        auto tape_handle = arena_handle->allocate(
            payload_size * sizeof(types::size_t), sizeof(types::size_t));
        auto* tape       = static_cast<types::size_t*>(tape_handle);
        auto* const_tape = static_cast<const types::size_t*>(tape_handle);

        auto scan_res = scanner::scanner8(payload, payload_size, tape,
                                          payload_size);
        if (!scan_res.has_value()) {
            return nullptr;
        }

        return new (arena_handle->allocate(sizeof(ondemand::Document),
                                           alignof(ondemand::Document)))
            ondemand::Document(payload, const_tape, scan_res.value(),
                               arena_handle);
    }

    types::uint8_t buffer[1024 * 1024]{}; // allocate 1 MB
    memory::Arena* arena_handle{nullptr};

    NumberTest() noexcept
        : arena_handle(create_arena(buffer, sizeof(buffer))) {}
};

TEST_F(NumberTest, LemireBasicForms) {
    // Integer negatif, pecahan, dan eksponen.
    alignas(8) constexpr const char payload[] =
        R"({"int_val":42,"neg_val":-2024,"double_val":3.1415926535,"exp_val":1.23e4})";
    constexpr auto payload_size = sizeof(payload) - 1;

    ondemand::Document* doc = build_document(payload, payload_size);
    ASSERT_NE(doc, nullptr);

    ondemand::JsonObject obj = doc->get_root().get_object();

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

TEST_F(NumberTest, IntegerBeyondDoublePrecision) {
    // Lebih besar dari 2^53 — tepat sebagai int64, tetapi tak bisa
    // diwakili presisi penuh sebagai double (bukti jalur int64 terpisah).
    alignas(8) constexpr const char payload[] =
        R"({"big":9007199254740993,"small":-9007199254740993,"zero":0,"minus_zero":-0})";
    constexpr auto payload_size = sizeof(payload) - 1;

    ondemand::Document* doc = build_document(payload, payload_size);
    ASSERT_NE(doc, nullptr);

    ondemand::JsonObject obj = doc->get_root().get_object();

    ondemand::JsonValue big_node = obj.find_field(types::StringView("big", 3));
    EXPECT_EQ(big_node.get_int(), 9007199254740993);

    ondemand::JsonValue small_node =
        obj.find_field(types::StringView("small", 5));
    EXPECT_EQ(small_node.get_int(), -9007199254740993);

    ondemand::JsonValue zero_node =
        obj.find_field(types::StringView("zero", 4));
    EXPECT_EQ(zero_node.get_int(), 0);

    ondemand::JsonValue minus_zero_node =
        obj.find_field(types::StringView("minus_zero", 10));
    EXPECT_DOUBLE_EQ(minus_zero_node.get_double(), 0.0);

    arena_handle->reset();
}

TEST_F(NumberTest, ExponentBoundaries) {
    // 1e300 / 1e-300: besar-kecil ekstrem namun masih dalam rentang double.
    alignas(8) constexpr const char payload[] =
        R"({"huge":1e300,"tiny":1e-300,"neg_tiny":-2.5e-8})";
    constexpr auto payload_size = sizeof(payload) - 1;

    ondemand::Document* doc = build_document(payload, payload_size);
    ASSERT_NE(doc, nullptr);

    ondemand::JsonObject obj = doc->get_root().get_object();

    ondemand::JsonValue huge_node =
        obj.find_field(types::StringView("huge", 4));
    EXPECT_NEAR(huge_node.get_double(), 1e300, 1e288);

    ondemand::JsonValue tiny_node =
        obj.find_field(types::StringView("tiny", 4));
    EXPECT_NEAR(tiny_node.get_double(), 1e-300, 1e-306);

    ondemand::JsonValue neg_tiny_node =
        obj.find_field(types::StringView("neg_tiny", 8));
    EXPECT_NEAR(neg_tiny_node.get_double(), -2.5e-8, 1e-16);

    arena_handle->reset();
}

} // namespace zuu::testing
