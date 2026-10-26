// Kelompok uji: JSON_checker — pembanding REFERENCE resmi json.org
// (implementasi C Douglas Crockford) terhadap scanner ze_json pada
// korus pas*.json / fail*.json klasik.
//
// Sumber data (sekali unduh ke folder ini, ter-commit):
//   scripts/invoke_JSON_checker/main.py  →  tests/JSON_checker_test/corpus/
//   (+ JSON_checker.c/.h, utf8_decode.c/.h, utf8_to_utf16.c/.h)
//
// METODOLOGI JUJUR — ranah kedua validator BERBEDA:
//   - JSON_checker = state machine GRAMATIKAL penuh (kutip kunci, sintaks
//     nilai, escape legal, kontrol dalam string, kedalaman, tipe akar).
//   - scanner ze_json = validator STRUKTURAL (keseimbangan bracket BERTIPE
//     sejak v0.1.5 — objek '{' wajib ditutup '}', array '[' wajib ']',
//     per Langdale & Lemire 2019 §3.2; kutip string utuh + pasangan
//     escape; UTF-8 terintegrasi) — sintaks nilai sengaja BUKAN ranahnya
//     (dibaca on-demand; lihat docs/architecture).
// Konsekuensi: file fail yang invalid secara gramatikal DIHARAPKAN lolos
// scanner; hanya subset struktural (kStructuralFails) yang wajib ditolak.
// Batas desain ini DIKUNCI oleh asersi — bila validasi gramatikal ditambah
// kelak, pindahkan file dari divergensi ke kStructuralFails.
//
// SEMUA varian scanner platform ini diperiksa: scanner8 selalu; scanner64
// pada LE 64-bit (scanner32 fallback 32-bit tak dikompilasi di sini —
// kesetaraan format tape lintas varian dijaga scanner_test).
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ze_json/scanner/scanner.hpp>
#include <ze_json/ze_json.hpp>

extern "C" {
#include "JSON_checker.h"
}

namespace zuu::testing {
namespace fs = std::filesystem;

class JsonCheckerTest : public ::testing::Test {
  protected:
    // Arena 1 MB untuk tape per-file (dialokasi tiap iterasi lalu
    // di-reset — korpus terbesar ~1,4 KiB, jauh di bawah kapasitas).
    types::uint8_t buffer[1024 * 1024]{};
    memory::Arena* arena_handle{nullptr};
    std::vector<fs::path> corpus;

    JsonCheckerTest() noexcept
        : arena_handle(create_arena(buffer, sizeof(buffer))),
          corpus(globCorpus()) {}

    // Kumpulkan korpus (non-rekursif, terurut — asersi prefix mengandalkan
    // urutan ini).
    static std::vector<fs::path>
    globCorpus() {
        const auto corpus_dir = fs::path(__FILE__).parent_path() / "corpus";
        std::vector<fs::path> paths;
        if (!fs::exists(corpus_dir)) {
            return paths;
        }
        for (const auto& entry : fs::directory_iterator(corpus_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                paths.push_back(entry.path());
            }
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

    static std::string
    readFile(const fs::path& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            return {};
        }
        const auto size = fs::file_size(filepath);
        std::string content(size, '\0');
        file.read(content.data(), static_cast<std::streamsize>(size));
        return content;
    }

    // Rentang berkas ber-prefix tertentu dari korpus terurut.
    std::span<const fs::path>
    getCorpusByPrefix(std::string_view prefix) const noexcept {
        const auto first = std::find_if(
            corpus.begin(), corpus.end(), [prefix](const fs::path& p) {
                return p.filename().string().starts_with(prefix);
            });
        if (first == corpus.end()) {
            return {};
        }
        const auto last = std::find_if_not(
            first, corpus.end(), [prefix](const fs::path& p) {
                return p.filename().string().starts_with(prefix);
            });
        return std::span<const fs::path>(
            &(*first), static_cast<std::size_t>(std::distance(first, last)));
    }

    // Verdict REFERENCE json.org — gramatikal penuh, kedalaman maks `depth`.
    static bool
    isValidJson(std::string_view input, int depth = 20) {
        JSON_checker jc = new_JSON_checker(depth);
        if (!jc) {
            return false;
        }
        bool valid = true;
        for (const char ch : input) {
            if (!JSON_checker_char(jc, ch)) {
                valid = false;
                break;
            }
        }
        if (valid) {
            valid = JSON_checker_done(jc) != 0;
        }
        return valid;
    }

    // Varian scanner yang tersedia di platform ini.
    static std::vector<std::string_view>
    scannerVariants() {
        std::vector<std::string_view> variants = {"scanner8"};
#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1
        variants.push_back("scanner64");
#endif
        return variants;
    }

    // Verdict satu varian scanner — struktural: keseimbangan bracket
    // BERTIPE, kutip string, escape berpasangan, UTF-8. Tape dialokasikan
    // longgar (1 entry per byte payload = batas atas aman) lalu arena
    // di-reset.
    bool
    scannerAccepts(std::string_view variant, const std::string& content) {
        auto* tape = static_cast<types::size_t*>(arena_handle->allocate(
            content.length() * sizeof(types::size_t), sizeof(types::size_t)));
        if (!tape) [[unlikely]] {
            return false;
        }
        bool accepted = false;
        if (variant == "scanner8") {
            accepted = scanner::scanner8(
                           content.data(), content.length(), tape,
                           content.length())
                           .has_value();
        }
#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1
        else if (variant == "scanner64") {
            accepted = scanner::scanner64(
                           content.data(), content.length(), tape,
                           content.length())
                           .has_value();
        }
#endif
        arena_handle->reset();
        return accepted;
    }

    // File fail yang DITOLAK oleh goto-automaton scanner8 (gramatikal).
    // scanner64 menyusul — kontrak kesetaraan varian menuntut behavior sama.
    // 13 file sisanya = batas ranah scanner (sintaks angka, escape string,
    // kontrol mentah dalam string, kedalaman > 256).
    static const std::set<std::string>&
    structuralFails() {
        static const std::set<std::string> fails = {
            // Bracket: tanpa pasangan / tipe tak cocok
            "fail2.json",  // ["Unclosed array"
            "fail8.json",   // ["Extra close"]]
            "fail32.json", // {"...": true,
            "fail33.json", // ["mismatch"}
            // Root type (goto: doc_start hanya menerima '{' atau '[')
            "fail1.json",  // "A JSON payload should be..."
            // Kunci object (goto: kunci WAJIB berkutip)
            "fail3.json",  // {unquoted_key: ...}
            // Koma ekstra / trailing comma (goto: state machine)
            "fail4.json",  // ["extra comma",]
            "fail5.json",  // ["double extra comma",,]
            "fail6.json",  // [, "<-- missing value"]
            "fail7.json",  // ["Comma after the close"],
            "fail9.json",  // {"Extra comma": true,}
            // Trailing garbage setelah root close (goto: done state check)
            "fail10.json", // {"Extra value..."} "misplaced..."
            // Pseudo-structural: garbage sebelum literal (goto: ws_only check)
            "fail12.json", // {"Illegal invocation": alert()}
            "fail16.json", // [\naked]
            "fail23.json", // ["Bad value", truth]
            "fail24.json", // ['single quote']
            // Kunci/colon sequencing (goto: obj_colon state)
            "fail19.json", // {"Missing colon" null}
            "fail20.json", // {"Double colon":: null}
            "fail21.json", // {"Comma instead of colon", null}
            "fail22.json", // ["Colon instead of comma": false]
        };
        return fails;
    }
};

TEST_F(JsonCheckerTest, PassCorpusAcceptedByBoth) {
    // Kontrak korpus: pass*.json wajib diterima REFERENCE dan SETIAP
    // varian scanner.
    const auto samples = getCorpusByPrefix("pass");
    ASSERT_FALSE(samples.empty())
        << "Korpus pass*.json tidak ditemukan — jalankan "
           "scripts/invoke_JSON_checker/main.py";

    for (const auto& sample : samples) {
        const std::string content = readFile(sample);
        EXPECT_TRUE(isValidJson(content))
            << "REFERENCE malah menolak: " << sample;
        for (const auto variant : scannerVariants()) {
            SCOPED_TRACE(variant);
            EXPECT_TRUE(scannerAccepts(variant, content))
                << "Scanner menolak file VALID: " << sample;
        }
    }
}

TEST_F(JsonCheckerTest, FailCorpusRejectedByReference) {
    // Sanity harness: reference menolak SELURUH korpus fail.
    const auto samples = getCorpusByPrefix("fail");
    ASSERT_FALSE(samples.empty())
        << "Korpus fail*.json tidak ditemukan — jalankan "
           "scripts/invoke_JSON_checker/main.py";

    for (const auto& sample : samples) {
        EXPECT_FALSE(isValidJson(readFile(sample)))
            << "REFERENCE malah menerima: " << sample;
    }
}

TEST_F(JsonCheckerTest, FailCorpusScannerRejectsStructuralOnly) {
    // Klasifikasi terkunci (bukti empiris, bukan tebak): SETIAP varian
    // scanner hanya menolak subset struktural. Divergensi lain — kunci
    // tak berkutip, koma ekstra, angka ilegal (013/0x14/0e), escape liar
    // (\x15/\017), kontrol mentah dalam string, kedalaman >20, tipe akar
    // bukan object/array — semuanya di luar ranah scanner struktural
    // BY DESIGN (saling-silang [{]} juga lolos: dua counter tak melacak
    // URUTAN — ranah stack scope tahap-2, per Langdale & Lemire 2019
    // §3.2).
    for (const auto variant : scannerVariants()) {
        SCOPED_TRACE(variant);
        for (const auto& sample : getCorpusByPrefix("fail")) {
            const auto name = sample.filename().string();
            const bool accepted = scannerAccepts(variant, readFile(sample));
            if (structuralFails().contains(name)) {
                EXPECT_FALSE(accepted)
                    << "Invalid struktural wajib ditolak: " << name;
            } else {
                EXPECT_TRUE(accepted)
                    << "Divergensi baru — periksa apakah validasi scanner "
                       "diperluas (pindahkan ke structuralFails bila memang "
                       "perbaikan): "
                    << name;
            }
        }
    }
}

} // namespace zuu::testing
