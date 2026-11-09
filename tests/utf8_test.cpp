// UTF-8 validator differential + correctness tests (TASKS P7-1).
//
// Rationale: the table-driven Niagara validator must agree with the
// table-free scalar reference on EVERY input before any performance claim
// is admissible (cpp-highperf-json rule 4; AGENTS.md section 6). The tables
// themselves were verified exhaustively against a strict RFC 3629 oracle
// during P7-1 (see GenerateLookups.py provenance note); this suite pins
// that result permanently:
//   - class asserts: every error pattern of the paper's Table 8 plus every
//     valid range boundary (hard-coded truth, independent of both impls);
//   - exhaustive sweeps: full 1-byte and 2-byte spaces, full 3-byte space,
//     structured 4-byte sweep — both impls must agree with each other;
//   - truncation: every proper prefix of a valid multi-byte char is invalid;
//   - json_checker pass files (valid UTF-8 documents) must validate.
//
// @cite 10.1002_spe.2920 Niagara two-table validation + Table 8 patterns.
#include <gtest/gtest.h>

#include <ze_json/utils/utf8_validator.hpp>

#include "export_sidecar.hpp" // export-helper sidecar contract (AGENTS.md section 4)

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using zuu::utils::utf8_validate;
using zuu::utils::utf8_validate_scalar;

struct Case {
  std::string_view bytes;
  bool valid;
};

// Every Table 8 error pattern + every valid boundary (hard-coded truth).
constexpr std::array<Case, 34> kCases = {{
  {"a", true},                                   // ASCII
  {"\x7F", true},                                // U+007F boundary
  {"\xC2\x80", true},                            // U+0080 (min 2-byte)
  {"\xDF\xBF", true},                            // U+07FF (max 2-byte)
  {"\xE0\xA0\x80", true},                        // U+0800 (min 3-byte)
  {"\xE1\x80\x80", true},                        // E1 plain lead
  {"\xEC\xBF\xBF", true},                        // U+CFFF
  {"\xED\x80\x80", true},                        // U+D000 (pre-surrogate)
  {"\xED\x9F\xBF", true},                        // U+D7FF (last pre-surrogate)
  {"\xEE\x80\x80", true},                        // U+E000 (post-surrogate)
  {"\xEF\xBF\xBF", true},                        // U+FFFF (max 3-byte)
  {"\xF0\x90\x80\x80", true},                    // U+10000 (min 4-byte)
  {"\xF1\x80\x80\x80", true},                    // F1 plain lead
  {"\xF3\xBF\xBF\xBF", true},                    // U+3FFFF
  {"\xF4\x80\x80\x80", true},                    // U+100000
  {"\xF4\x8F\xBF\xBF", true},                    // U+10FFFF (max)
  {"\xC0\x80", false},                           // overlong 2-byte (NUL)
  {"\xC1\xBF", false},                           // overlong 2-byte
  {"\xC2\x41", false},                           // too short (ASCII 2nd)
  {"\xC2\xC3", false},                           // too short (lead 2nd)
  {"\x41\x80", false},                           // too long (ASCII + cont)
  {"\x80\x41", false},                           // stray continuation
  {"\xE0\x80\x80", false},                       // overlong 3-byte
  {"\xE0\x9F\xBF", false},                       // overlong 3-byte
  {"\xED\xA0\x80", false},                       // surrogate U+D800
  {"\xED\xBF\xBF", false},                       // surrogate U+DFFF
  {"\xEE\x41\x80", false},                       // too short inside 3-byte
  {"\xF0\x80\x80\x80", false},                   // overlong 4-byte
  {"\xF0\x8F\xBF\xBF", false},                   // overlong 4-byte
  {"\xF4\x90\x80\x80", false},                   // > U+10FFFF
  {"\xF5\x80\x80\x80", false},                   // F5 lead (too large)
  {"\xFF", false},                               // 0xFF lead
  {"\xC2", false},                               // truncated 2-byte
  {"\xE0\xA0", false},                           // truncated 3-byte
}};

// Valid multi-byte chars for the truncation sweep (every proper prefix of
// these must be rejected by BOTH implementations).
constexpr std::array<std::string_view, 6> kValidChars = {{
  "\xC2\x80", "\xE0\xA0\x80", "\xED\x9F\xBF", "\xF0\x90\x80\x80",
  "\xF4\x8F\xBF\xBF", "\xE6\x96\x87",
}};

void expect_agreement(std::string_view bytes, bool expected, const char *ctx) {
  const bool scalar = utf8_validate_scalar(bytes.data(), bytes.size());
  const bool fast = utf8_validate(bytes.data(), bytes.size());
  EXPECT_EQ(scalar, expected) << ctx << " (scalar)";
  EXPECT_EQ(fast, expected) << ctx << " (niagara)";
  EXPECT_EQ(scalar, fast) << ctx << " (differential)";
}

TEST(Utf8Validator, Table8PatternsAndBoundaries) {
  for (const auto &c : kCases) {
    expect_agreement(c.bytes, c.valid, "table8 case");
  }
}

TEST(Utf8Validator, ExhaustiveByteSpaces) {
  // 1-byte: full space.
  for (int a = 0; a < 256; ++a) {
    const char ch = static_cast<char>(static_cast<unsigned char>(a));
    const bool expected = a < 0x80;
    expect_agreement(std::string_view(&ch, 1), expected, "1-byte sweep");
  }
  // 2-byte: full 65,536 space (truth = strict full-string semantics:
  // two ASCII chars is a VALID string; a continuation cannot lead; a
  // 2-byte char is exactly C2-DF + 80-BF; anything else is truncated or
  // malformed).
  for (int a = 0; a < 256; ++a) {
    for (int b = 0; b < 256; ++b) {
      const char s[2] = {static_cast<char>(static_cast<unsigned char>(a)),
                         static_cast<char>(static_cast<unsigned char>(b))};
      const bool expected = (a < 0x80 && b < 0x80)
                            || (a >= 0xC2 && a <= 0xDF && b >= 0x80 && b <= 0xBF);
      expect_agreement(std::string_view(s, 2), expected, "2-byte sweep");
    }
  }
  // 3-byte: FULL space (16,777,216) — cheap in C++, and exactly the space
  // where the paper's marker cross-check earns its keep.
  for (int a = 0; a < 256; ++a) {
    for (int b = 0; b < 256; ++b) {
      for (int c = 0; c < 256; ++c) {
        const char s[3] = {static_cast<char>(static_cast<unsigned char>(a)),
                           static_cast<char>(static_cast<unsigned char>(b)),
                           static_cast<char>(static_cast<unsigned char>(c))};
        const bool expected = utf8_validate_scalar(s, 3);
        ASSERT_EQ(utf8_validate(s, 3), expected) << "3-byte sweep";
      }
    }
  }
  // 4-byte: structured sweep — every lead through every probe pair.
  const std::array<unsigned char, 8> probes = {
    0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0xBF, 0xC0,
  };
  for (int a = 0xC0; a < 256; ++a) {
    for (const unsigned char b : probes) {
      for (const unsigned char c : probes) {
        for (const unsigned char d : probes) {
          const char s[4] = {static_cast<char>(static_cast<unsigned char>(a)),
                             static_cast<char>(b), static_cast<char>(c),
                             static_cast<char>(d)};
          const bool expected = utf8_validate_scalar(s, 4);
          ASSERT_EQ(utf8_validate(s, 4), expected) << "4-byte sweep";
        }
      }
    }
  }
}

TEST(Utf8Validator, TruncatedPrefixesAreInvalid) {
  for (const std::string_view full : kValidChars) {
    for (size_t cut = 0; cut <= full.size(); ++cut) {
      // Empty and complete = valid; every proper prefix = invalid.
      expect_agreement(full.substr(0, cut),
                       cut == 0 || cut == full.size(), "truncation sweep");
    }
  }
}

TEST(Utf8Validator, EmbeddedInText) {
  // Valid multibyte embedded in ASCII text (the JSON-string shape the
  // parser will hand the validator).
  const std::string ok = "name\": \"\xE6\x96\x87\xF0\x9F\xA6\x89\", na\xC3\xAFve";
  expect_agreement(ok, true, "embedded valid");
  const std::string bad = "name\": \"\xE6\x96\x41\xF0\x9F\xA6\x89\"";
  expect_agreement(bad, false, "embedded broken");
}

TEST(Utf8Validator, JsonCheckerPassFilesAreValidUtf8) {
  namespace fs = std::filesystem;
  const fs::path dir(ZE_JSON_CORPUS_DIR);
  ASSERT_TRUE(fs::exists(dir)) << dir.string();
  for (const auto &entry : fs::directory_iterator(dir)) {
    const std::string name = entry.path().filename().string();
    if (!name.starts_with("pass") || !name.ends_with(".json")) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    EXPECT_TRUE(utf8_validate_scalar(content.data(), content.size())) << name;
    EXPECT_TRUE(utf8_validate(content.data(), content.size())) << name;
  }
}

TEST(Utf8Validator, FuzzAgreement) {
  // Deterministic fuzz (project seed convention): both implementations must
  // agree everywhere; truth is pinned by the curated + exhaustive cases.
  std::mt19937_64 rng(20261108);
  std::uniform_int_distribution<int> byte_dist(0, 255);
  std::uniform_int_distribution<size_t> len_dist(0, 64);
  std::vector<char> buf;
  for (int iteration = 0; iteration < 200000; ++iteration) {
    buf.resize(len_dist(rng));
    for (auto &byte : buf) {
      byte = static_cast<char>(static_cast<unsigned char>(byte_dist(rng)));
    }
    // Bias 1-in-4 buffers toward multi-byte shapes (leads + continuations).
    if (iteration % 4 == 0 && !buf.empty()) {
      for (size_t k = 0; k < buf.size(); k += 2) {
        buf[k] = static_cast<char>(static_cast<unsigned char>(0xC0 | (byte_dist(rng) & 0x3F)));
      }
    }
    const bool scalar = utf8_validate_scalar(buf.data(), buf.size());
    const bool fast = utf8_validate(buf.data(), buf.size());
    ASSERT_EQ(scalar, fast) << "fuzz divergence at iteration " << iteration;
  }
}

} // namespace

// Custom main: persist the export-helper sidecar (ZE_METADATA_OUT) when the
// run is instrumented, then execute the suite. Plain local runs are no-ops.
int main(int argc, char **argv) {
  zuu::testing::write_export_sidecar("ZE_METADATA_OUT", "ZE_RUN_ID",
                                     "ZE_GIT_COMMIT", "ze_json_utf8_test");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
