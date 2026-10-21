#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

#include <ze_json/ondemand/document.hpp>
#include <ze_json/scanner/scanner.hpp>

namespace zuu::testing {

class ParserTest : public ::testing::Test {
  protected:
    // Helper function untuk mengonversi types::StringView ke std::string_view
    // untuk asersi GTest
    std::string_view
    to_sv(const types::StringView& sv) {
        return std::string_view(sv.data, sv.length);
    }
};

TEST_F(ParserTest, DocumentTypes) {
    std::string json =
        R"({"str":"hello","num":123,"flag":true,"null_val":null,"arr":[1,2],"obj":{}})";

    std::vector<types::size_t> tape(json.size());
    auto scan_res =
        scanner::scanner8(json.data(), json.size(), tape.data(), tape.size());
    ASSERT_TRUE(scan_res.has_value());

    ondemand::Document doc(json.data(), tape.data(), scan_res.getValue());
    ondemand::JsonValue root = doc.get_root();

    EXPECT_EQ(root.type(), ondemand::JsonType::Object);
}

TEST_F(ParserTest, ObjectFieldLookup) {
    std::string json = R"({"name":"zuu","role":"parser","active":true})";

    std::vector<types::size_t> tape(json.size());
    auto scan_res =
        scanner::scanner8(json.data(), json.size(), tape.data(), tape.size());
    ASSERT_TRUE(scan_res.has_value());

    ondemand::Document doc(json.data(), tape.data(), scan_res.getValue());
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
}

TEST_F(ParserTest, LemireNumberParsing) {
    // Payload menguji Clinger Fast Path, pecahan, dan scientific notation
    std::string json =
        R"({"int_val":42,"neg_val":-2024,"double_val":3.1415926535,"exp_val":1.23e4})";

    std::vector<types::size_t> tape(json.size());
    auto scan_res =
        scanner::scanner8(json.data(), json.size(), tape.data(), tape.size());
    ASSERT_TRUE(scan_res.has_value());

    ondemand::Document doc(json.data(), tape.data(), scan_res.getValue());
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
}

TEST_F(ParserTest, SkipNestedStructures) {
    // Deeply nested JSON structure
    std::string json =
        R"({"matrix":[[1,2],[3,4]],"meta":{"deep":{"a":1}},"target":"found"})";

    std::vector<types::size_t> tape(json.size());
    auto scan_res =
        scanner::scanner8(json.data(), json.size(), tape.data(), tape.size());
    ASSERT_TRUE(scan_res.has_value());

    ondemand::Document doc(json.data(), tape.data(), scan_res.getValue());
    ondemand::JsonObject obj = doc.get_root().get_object();

    // Mencari "target" secara langsung, memaksa skip_child melompati array
    // bersarang dan object bersarang
    ondemand::JsonValue target_val =
        obj.find_field(types::StringView("target", 6));
    EXPECT_EQ(target_val.type(), ondemand::JsonType::String);
    EXPECT_EQ(to_sv(target_val.get_string()), "found");
}

} // namespace zuu::testing