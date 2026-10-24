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
//   - scanner ze_json = validator STRUKTURAL (keseimbangan bracket, kutip
//     string utuh + pasangan escape, UTF-8 terintegrasi) — sintaks nilai
//     sengaja BUKAN ranahnya (dibaca on-demand; lihat docs/architecture).
// Konsekuensi: file fail yang invalid secara gramatikal DIHARAPKAN lolos
// scanner; hanya subset struktural (kStructuralFails) yang wajib ditolak.
// Batas desain ini DIKUNCI oleh asersi — bila validasi gramatikal ditambah
// kelak, pindahkan file dari divergensi ke kStructuralFails.
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

    // Verdict scanner ze_json — struktural: keseimbangan bracket, kutip
    // string, escape berpasangan, UTF-8. Tape dialokasikan longgar
    // (1 entry per byte payload = batas atas aman) lalu arena di-reset.
    bool
    scannerAccepts(const std::string& content) {
        auto* tape = static_cast<types::size_t*>(arena_handle->allocate(
            content.length() * sizeof(types::size_t), sizeof(types::size_t)));
        if (!tape) [[unlikely]] {
            return false;
        }
        const auto result = scanner::scanner8(
            content.data(), content.length(), tape, content.length());
        arena_handle->reset();
        return result.has_value();
    }

    // File fail yang invalid secara STRUKTURAL — scanner WAJIB menolak.
    // Terukur dari korpus: bracket tanpa pasangan (buka-tanpa-tutup /
    // tutup-tanpa-buka). 30 file sisanya invalid GRAMATIKAL.
    static const std::set<std::string>&
    structuralFails() {
        static const std::set<std::string> fails = {
            "fail2.json",  // ["Unclosed array" — '[' tanpa ']'
            "fail8.json",  // ["Extra close"]] — ']' tanpa pembuka
            "fail32.json", // {"Comma instead if closing brace": true, — '{' tanpa '}'
        };
        return fails;
    }
};

TEST_F(JsonCheckerTest, PassCorpusAcceptedByBoth) {
    // Kontrak korpus: pass*.json wajib diterima REFERENCE dan scanner.
    const auto samples = getCorpusByPrefix("pass");
    ASSERT_FALSE(samples.empty())
        << "Korpus pass*.json tidak ditemukan — jalankan "
           "scripts/invoke_JSON_checker/main.py";

    for (const auto& sample : samples) {
        const std::string content = readFile(sample);
        EXPECT_TRUE(isValidJson(content))
            << "REFERENCE malah menolak: " << sample;
        EXPECT_TRUE(scannerAccepts(content))
            << "Scanner menolak file VALID: " << sample;
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
    // Klasifikasi terkunci (bukti empiris, bukan tebak): scanner hanya
    // menolak subset struktural. Divergensi lain — kunci tak berkutip,
    // koma ekstra, angka ilegal (013/0x14/0e), escape liar (\x15/\017),
    // kontrol mentah dalam string, kedalaman >20, tipe akar bukan
    // object/array, bahkan tipe bracket tak cocok dengan jumlah seimbang
    // ("fail33": ["mismatch"} — kandidat perbaikan depth-counter bertipe)
    // — semuanya di luar ranah scanner struktural BY DESIGN.
    for (const auto& sample : getCorpusByPrefix("fail")) {
        const auto name = sample.filename().string();
        const bool accepted = scannerAccepts(readFile(sample));
        if (structuralFails().contains(name)) {
            EXPECT_FALSE(accepted) << "Invalid struktural wajib ditolak: " << name;
        } else {
            EXPECT_TRUE(accepted)
                << "Divergensi baru — periksa apakah validasi scanner "
                   "diperluas (pindahkan ke structuralFails bila memang "
                   "perbaikan): "
                << name;
        }
    }
}

} // namespace zuu::testing
