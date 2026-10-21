#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

#include <ze_json/scanner/scanner.hpp>
#include <ze_json/utils/utf8_validator.hpp>

namespace zuu::testing {

class ScannerTest : public ::testing::Test {
  protected:
    // Helper function untuk menjalankan scanner8 dan mengembalikan tape result
    std::pair<bool, std::vector<types::size_t>>
    run_scanner(std::string_view json) {
        std::vector<types::size_t> tape(json.size() + 1);
        auto res = scanner::scanner8(
            json.data(), json.size(), tape.data(), tape.size());

        if (!res.has_value()) {
            return {false, {}};
        }

        types::size_t count = res.getValue();
        tape.resize(count);
        return {true, tape};
    }
};

TEST_F(ScannerTest, StandardJsonPayload) {
    std::string json     = R"({"name":"zuu","age":21,"active":true})";
    auto [success, tape] = run_scanner(json);

    ASSERT_TRUE(success);
    // Karakter struktural: {, ", ", :, ", ", ,, ", ", :, 21, ,, ", ", :, true,
    // }
    EXPECT_GT(tape.size(), 0u);

    // Verifikasi titik awal dan akhir
    EXPECT_EQ(json[tape.front()], '{');
    EXPECT_EQ(json[tape.back()], '}');
}

TEST_F(ScannerTest, EmptyContainers) {
    auto [obj_ok, obj_tape] = run_scanner("{}");
    ASSERT_TRUE(obj_ok);
    EXPECT_EQ(obj_tape.size(), 2u);
    EXPECT_EQ(obj_tape[0], 0u);
    EXPECT_EQ(obj_tape[1], 1u);

    auto [arr_ok, arr_tape] = run_scanner("[]");
    ASSERT_TRUE(arr_ok);
    EXPECT_EQ(arr_tape.size(), 2u);
}

TEST_F(ScannerTest, ValidUtf8Multibyte) {
    // String dengan karakter UTF-8 valid: 2-byte (Cina/Jepang), 4-byte (Emoji)
    std::string json = R"({"message":"Hello 世界 🌍","status":"ok"})";

    // Pastikan UTF-8 Validator meloloskannya
    bool utf8_valid = utils::Utf8Validator(json.data(), json.size());
    EXPECT_TRUE(utf8_valid);

    auto [ok, tape] = run_scanner(json);
    ASSERT_TRUE(ok);
}

TEST_F(ScannerTest, RejectsInvalidUtf8) {
    // 1. Invalid continuation byte (0xFF)
    std::string invalid_byte = "{\"key\":\"\xFF\xFF\"}";
    EXPECT_FALSE(
        utils::Utf8Validator(invalid_byte.data(), invalid_byte.size()));
    auto [ok1, tape1] = run_scanner(invalid_byte);
    EXPECT_FALSE(ok1);

    // 2. Overlong Encoding karakter ASCII '/' (0xC0 0xAF)
    std::string overlong = "{\"path\":\"\xC0\xAF\"}";
    EXPECT_FALSE(utils::Utf8Validator(overlong.data(), overlong.size()));
    auto [ok2, tape2] = run_scanner(overlong);
    EXPECT_FALSE(ok2);

    // 3. Unmatched surrogate pair byte
    std::string surrogate = "{\"code\":\"\xED\xA0\x80\"}";
    EXPECT_FALSE(utils::Utf8Validator(surrogate.data(), surrogate.size()));
    auto [ok3, tape3] = run_scanner(surrogate);
    EXPECT_FALSE(ok3);
}

TEST_F(ScannerTest, EdgeCasesAndBuffers) {
    // Null ptr / empty size handling
    std::vector<types::size_t> tape(10);
    auto null_res = scanner::scanner8(nullptr, 0, tape.data(), tape.size());
    EXPECT_FALSE(null_res.has_value());

    // Buffer destination terlalu kecil
    std::string json = R"({"a": 1, "b": 2, "c": 3})";
    std::vector<types::size_t> tiny_tape(
        2); // Terlalu kecil untuk menampung tape
    auto overflow_res = scanner::scanner8(
        json.data(), json.size(), tiny_tape.data(), tiny_tape.size());
    EXPECT_FALSE(overflow_res.has_value());
}

} // namespace zuu::testing