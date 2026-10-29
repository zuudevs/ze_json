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

        types::size_t count = res.value();
        tape.resize(count);
        return {true, tape};
    }

    // Helper: jalankan scanner64 dan kembalikan (ok, tape) — hanya tersedia
    // pada platform LE 64-bit; dipakai asseri kesetaraan dengan scanner8.
#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1
    std::pair<bool, std::vector<types::size_t>>
    run_scanner64(std::string_view json) {
        std::vector<types::size_t> tape(json.size() + 1);
        auto res = scanner::scanner64(
            json.data(), json.size(), tape.data(), tape.size());

        if (!res.has_value()) {
            return {false, {}};
        }

        types::size_t count = res.value();
        tape.resize(count);
        return {true, tape};
    }
#endif
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

TEST_F(ScannerTest, BracketDepthMustBalance) {
    // Kontrak kedalaman: semua kontainer wajib berpasangan — tape tidak
    // boleh dibangun dari payload terpotong.
    // 1) Kontainer tidak ditutup → ditolak.
    auto [trunc_ok, _1] = run_scanner(R"({"a":"x")");
    EXPECT_FALSE(trunc_ok);

    // 2) Kurung tutup berlebih tanpa pembuka → ditolak seketika.
    auto [premature_ok, _2] = run_scanner(R"({"a":{"b":1}}})");
    EXPECT_FALSE(premature_ok);

    // 3) Bersarang dalam-sejajar (balanced) → diterima, entry lengkap.
    auto [deep_ok, deep_tape] = run_scanner(R"({"d":{"x":[1,{"y":2}]}})");
    ASSERT_TRUE(deep_ok);
    EXPECT_GT(deep_tape.size(), 0u);
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

#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1
TEST_F(ScannerTest, Scanner64TapeMatchesScanner8) {
    // Kontrak: scanner64 wajib menghasilkan tape identik dengan scanner8
    // (offset uint64 dari src) untuk seluruh payload yang lolos, termasuk
    // tepi blok SWAR 8 byte dan string panjang.
    const std::string_view corpus[] = {
        R"({"name":"zuu","age":21,"active":true})",
        R"({"a":"back\\slash","b":"quote\"inner\"","c":"ctrl \u0001 done"})",
        R"({"message":"Hello 世界 🌍","status":"ok"})",
        R"({"d":{"x":[1,{"y":2}]}})",
        "{}",
        "[]",
        R"({"k":1e-5,"j":-0.25,"t":true,"f":false,"n":null})",
    };

    for (const auto& json : corpus) {
        auto [ok8, tape8]   = run_scanner(json);
        auto [ok64, tape64] = run_scanner64(json);

        EXPECT_EQ(ok8, ok64) << "payload: " << json;
        if (ok8 && ok64) {
            ASSERT_EQ(tape8.size(), tape64.size()) << "payload: " << json;
            for (size_t i = 0; i < tape8.size(); ++i) {
                EXPECT_EQ(tape8[i], tape64[i])
                    << "entry " << i << ", payload: " << json;
            }
        }
    }

    // Tepi blok SWAR: panjang string mengandung tanda kutip dan backslash
    // lintas batas 8 byte — semuanya wajib menghasilkan tape yang sama.
    size_t lens[] = {1, 7, 8, 9, 15, 16, 63, 64, 65, 127, 128, 255, 256, 257};
    for (auto len : lens) {
        std::string ascii(len, 'x');
        std::string plain   = "{\"k\":\"" + ascii + "\"}";
        std::string escaped = "{\"k\":\"" + ascii + " \\\"mix\\\\\"}";

        for (const std::string& json : {plain, escaped}) {
            auto [ok8, tape8]   = run_scanner(json);
            auto [ok64, tape64] = run_scanner64(json);
            EXPECT_EQ(ok8, ok64) << "len " << len << ": " << json;
            if (ok8 && ok64) {
                EXPECT_EQ(tape8, tape64) << "len " << len;
            }
        }
    }

    // Payload besar (profil jauh di atas tepi blok): item objek berulang
    // dengan nilai ber-escape — 4 ribu item tembus ukuran tape ratusan ribu.
    std::string besar = R"({"companies":[)";
    besar.reserve(220000);
    for (int i = 0; i < 4000; ++i) {
        besar += R"({"index":)";
        besar += std::to_string(i);
        besar += R"(,"name":"ZeJson","score":3.5},)";
    }
    besar.pop_back();
    besar += "]}";

    auto [big_ok8, big_tape8]   = run_scanner(besar);
    auto [big_ok64, big_tape64] = run_scanner64(besar);
    ASSERT_TRUE(big_ok8);
    ASSERT_TRUE(big_ok64);
    EXPECT_EQ(big_tape8.size(), big_tape64.size());
    EXPECT_EQ(big_tape8, big_tape64);
}

TEST_F(ScannerTest, BracketTypeMismatchRejectedByBothScanners) {
    // Kedalaman bracket BERTIPE via STACK-BIT (Langdale & Lemire 2019 §3.2):
    // '{' wajib ditutup '}', '[' wajib ']', DAN urutan tumpukan wajib
    // konsisten — stack-bit melacak JENIS per level, sehingga saling-silang
    // [{]} PUN tertolak (peningkatan atas dua-counter).
    const std::string_view mismatches[] = {
        R"(["mismatch"})", // '}' menutup '['
        "{]",              // ']' menutup '{'
        "[}",              // '}' menutup '['
        R"({"a":[1})",     // '}' melintasi array: '[' menggantung di akhir
        R"(["x"]})",       // ']' ekstra setelah array tertutup
        "[{]}",            // saling-silang: jumlah per jenis seimbang
        R"([{"b":1]})",    // ']' menutup object dalam array
        "{[}",             // '}' menutup '[' setelah '{'
    };

    for (const auto& json : mismatches) {
        auto [ok8, _8]   = run_scanner(json);
        auto [ok64, _64] = run_scanner64(json);
        EXPECT_FALSE(ok8) << "scanner8 malah lolos: " << json;
        EXPECT_FALSE(ok64) << "scanner64 malah lolos: " << json;
    }

    // Batas desain TERDOKUMENTASI: saling-silang seperti [{]} — jumlah
    // Nesting campuran yang sah tetap diterima kedua varian.
    const std::string_view balanced[] = {
        R"({"a":[1,{"b":[2]}]})",
        R"([[{"c":{"d":[]}}]])",
    };
    for (const auto& json : balanced) {
        auto [ok8, tape8]   = run_scanner(json);
        auto [ok64, tape64] = run_scanner64(json);
        ASSERT_TRUE(ok8) << json;
        ASSERT_TRUE(ok64) << json;
        EXPECT_EQ(tape8, tape64) << json;
    }
}

TEST_F(ScannerTest, Scanner64ErrorsMatchScanner8) {
    // Kontrak kebalikan: payload yang ditolak scanner8 wajib ditolak
    // scanner64 dengan keputusan yang sama (wajib sama-sama gagal).
    const std::string_view rejects[] = {
        R"({"a":"x")",              // Payload terpotong: kontainer buka
        R"({"a":{"b":1}}})",        // Tutup berlebihan tanpa pembuka
        R"({"key":"unclosed)",      // Tanda kutip tanpa penutup
        R"({"back":"ends with \")", // Backslash menjadikan kutip terakhir
                                    // ter-escape
        R"({"nested":{"open":[{"tight":1))", // Kontainer gagal sepihak
    };

    for (const auto& json : rejects) {
        auto [ok8, _8]   = run_scanner(json);
        auto [ok64, _64] = run_scanner64(json);
        EXPECT_FALSE(ok8) << "scanner8 malah lolos: " << json;
        EXPECT_EQ(ok8, ok64) << "payload: " << json;
    }

    // Payload UTF-8 rusak: kedua scanner wajib menolak (validator sama).
    std::string invalid_utf8 = "{\"key\":\"\xFF\xFF\"}";
    auto [u_ok8, _]          = run_scanner(invalid_utf8);
    auto [u_ok64, _2]        = run_scanner64(invalid_utf8);
    EXPECT_FALSE(u_ok8);
    EXPECT_EQ(u_ok8, u_ok64);
}

TEST_F(ScannerTest, MergedUtf8ValidationBlockBoundaries) {
    // Validasi UTF-8 kini menumpang jalan scanner (bukan pass terpisah).
    // Sekuens multibyte wajib tetap benar meski TERBELAH di batas blok
    // SWAR 8 byte: DFA state menyeberangi blok via jalur per-byte.
    // Body string mulai pada offset payload 6 (setelah {"k":"), sehingga
    // 7×'a' + sekuens menempatkan byte pertama sekuens di ujung blok 0.

    // (a) 2-byte é terbelah: C3 di body[7] (blok 0), A9 di body[8] (blok 1)
    std::string straddle2 = "{\"k\":\"";
    straddle2.append(7, 'a');
    straddle2 += "\xC3\xA9";
    straddle2 += "\",\"t\":\"x\"}";

    // (b) 3-byte 中 terbelah: E4 B8 di blok 0, AD di blok 1
    std::string straddle3 = "{\"k\":\"";
    straddle3.append(7, 'a');
    straddle3 += "\xE4\xB8\xAD";
    straddle3 += "\",\"t\":\"x\"}";

    // (c) 4-byte 🌍 terbelah: F0 di blok 0, 9F 8C 8D di blok 1
    std::string straddle4 = "{\"k\":\"";
    straddle4.append(7, 'a');
    straddle4 += "\xF0\x9F\x8C\x8D";
    straddle4 += "\",\"t\":\"x\"}";

    for (const auto& json : {straddle2, straddle3, straddle4}) {
        auto [ok8, tape8]   = run_scanner(json);
        auto [ok64, tape64] = run_scanner64(json);
        ASSERT_TRUE(ok8) << "scanner8 menolak: " << json;
        ASSERT_TRUE(ok64) << "scanner64 menolak: " << json;
        EXPECT_EQ(tape8, tape64) << "payload: " << json;
    }

    // (d) Multibyte di GAP antar struktural: valid UTF-8 → tetap lolos
    //     (scanner memang tidak memvalidasi sintaks nilai — kontrak lama),
    //     kedua scanner sepakat.
    std::string gap_mb = "{\"a\":\xC3\xA9}";
    {
        auto [ok8, tape8]   = run_scanner(gap_mb);
        auto [ok64, tape64] = run_scanner64(gap_mb);
        ASSERT_TRUE(ok8);
        ASSERT_TRUE(ok64);
        EXPECT_EQ(tape8, tape64);
    }

    // (e) Escape disusul multibyte: pasangan escape tetap masuk DFA.
    std::string esc_mb = "{\"k\":\"a\\\xC3\xA9ok\"}";
    {
        auto [ok8, tape8]   = run_scanner(esc_mb);
        auto [ok64, tape64] = run_scanner64(esc_mb);
        ASSERT_TRUE(ok8);
        ASSERT_TRUE(ok64);
        EXPECT_EQ(tape8, tape64);
    }
}

TEST_F(ScannerTest, MergedUtf8ValidationRejectsAcrossBoundaries) {
    // Penolakan wajib identik scanner8 vs scanner64 untuk sekuens rusak
    // pada posisi strategis — semua DITOLAK dua-duanya.
    const std::string rejects[] = {
        // Kutip penutup di tengah sekuens: C3 lalu '"' → invalid.
        std::string("{\"k\":\"") + std::string(7, 'a') + "\xC3\"}",
        // Backslash di tengah sekuens: C3 lalu '\\' → invalid.
        std::string("{\"k\":\"") + std::string(7, 'a') + "\xC3\\n\"}",
        // Byte escape kedua ber-high-bit: '\\' + C3 → invalid.
        std::string("{\"k\":\"a\\\xC3\"}"),
        // Overlong C0 AF di dalam string, menempel batas blok.
        std::string("{\"k\":\"") + std::string(6, 'a') + "\xC0\xAF\"}",
        // Surrogate ED A0 80 (3-byte terlarang).
        std::string("{\"k\":\"") + std::string(5, 'a') + "\xED\xA0\x80\"}",
        // Sekuens terpotong di gap antar struktural (bracket tetap seimbang
        // agar penolakan murni berasal dari DFA, bukan kedalaman).
        std::string("{\"a\":1 \xC3}"),
    };

    for (const auto& json : rejects) {
        auto [ok8, _8]   = run_scanner(json);
        auto [ok64, _64] = run_scanner64(json);
        EXPECT_FALSE(ok8) << "scanner8 malah lolos: " << json;
        EXPECT_FALSE(ok64) << "scanner64 malah lolos: " << json;
    }
}
#endif // __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1

} // namespace zuu::testing