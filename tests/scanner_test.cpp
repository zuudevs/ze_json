// Scanner differential + correctness tests (TASKS P1-3, P1-4).
//
// Rationale: every optimized scanner must reproduce the plain scalar
// reference byte-for-byte before any performance claim is admissible
// (cpp-highperf-json rule 4; AGENTS.md section 6). The corpus cases come
// from json.org JSON_checker (corpus/test/json_checker). Known legacy
// exceptions under RFC 8259: fail1.json (top-level string) and fail18.json
// (20-level nesting) are accepted by modern parsers, so the scanners must
// merely AGREE on them, not reject them.
//
// @cite 10.14778_3115404.3115416 Mison: structural index pre-scan contract.
// @cite 10.48550_arXiv.1902.08318 simdjson: staging = next-structural-index
//       stream; fast paths are correct iff byte-identical to the reference.
#include <gtest/gtest.h>

#include <ze_json/core/error.hpp>
#include <ze_json/core/lookups.hpp>
#include <ze_json/ondemand/reference.hpp>
#include <ze_json/ondemand/scanner.hpp>
#include <ze_json/utils/swar.hpp>

#include "export_sidecar.hpp" // export-helper sidecar contract (AGENTS.md section 4)

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

using zuu::core::JsonErrc;
using zuu::core::size_t;

constexpr size_t kNoIndex = static_cast<size_t>(-1);

struct ScanOutcome {
  bool ok = false;
  size_t count = 0;
  JsonErrc error = JsonErrc::None;
  std::vector<size_t> indices;
};

template <typename ScanFn>
ScanOutcome run_scan(ScanFn scan, const std::string &input, size_t capacity) {
  std::vector<size_t> dst(capacity, kNoIndex); // poisoned slots
  size_t *dst_ptr = capacity > 0 ? dst.data() : nullptr;

  auto result = scan(input.data(), input.size(), dst_ptr, capacity);

  ScanOutcome outcome;
  if (result.has_value()) {
    outcome.ok = true;
    outcome.count = result.value();
    outcome.indices.assign(
        dst.begin(), dst.begin() + static_cast<long long>(result.value()));
  } else {
    outcome.ok = false;
    outcome.error = result.error();
    for (const size_t slot : dst) { // expose the written prefix for comparison
      if (slot != kNoIndex) {
        outcome.indices.push_back(slot);
      }
    }
  }
  return outcome;
}

void expect_identical(const ScanOutcome &reference, const ScanOutcome &fast,
                      const char *context = "same input") {
  EXPECT_EQ(reference.ok, fast.ok) << context;
  if (reference.ok != fast.ok) {
    return;
  }
  if (reference.ok) {
    EXPECT_EQ(reference.count, fast.count) << context;
    EXPECT_EQ(reference.indices, fast.indices) << context;
  } else {
    EXPECT_EQ(reference.error, fast.error) << context;
    EXPECT_EQ(reference.indices, fast.indices) << context;
  }
}

std::vector<std::string> corpus_files(const std::string &prefix) {
  static const std::string corpus_root = ZE_JSON_CORPUS_DIR;
  std::vector<std::string> files;
  if (corpus_root.empty() || !std::filesystem::exists(corpus_root)) {
    return files;
  }
  for (const auto &entry : std::filesystem::directory_iterator(corpus_root)) {
    const std::string name = entry.path().filename().string();
    if (name.starts_with(prefix) && name.ends_with(".json")) {
      files.push_back(entry.path().string());
    }
  }
  return files;
}

std::string read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// ---------------------------------------------------------------- fixtures

TEST(ScannerDifferential, EmptyInput) {
  const auto ref = run_scan(zuu::ondemand::reference_scan, "", 16);
  const auto fast = run_scan(zuu::ondemand::scanner8, "", 16);
  EXPECT_FALSE(ref.ok);
  EXPECT_EQ(ref.error, JsonErrc::EmptyJson);
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, NullSource) {
  const auto ref = zuu::ondemand::reference_scan(nullptr, 1, nullptr, 0);
  const auto fast = zuu::ondemand::scanner8(nullptr, 1, nullptr, 0);
  ASSERT_TRUE(ref.has_error());
  ASSERT_TRUE(fast.has_error());
  EXPECT_EQ(ref.error(), JsonErrc::InvalidJson);
  EXPECT_EQ(fast.error(), JsonErrc::InvalidJson);
}

TEST(ScannerDifferential, NullDestination) {
  const std::string input = "{}";
  const auto ref =
      zuu::ondemand::reference_scan(input.data(), input.size(), nullptr, 4);
  const auto fast =
      zuu::ondemand::scanner8(input.data(), input.size(), nullptr, 4);
  ASSERT_TRUE(ref.has_error());
  ASSERT_TRUE(fast.has_error());
  EXPECT_EQ(ref.error(), JsonErrc::InsufficientBuffer);
  EXPECT_EQ(fast.error(), JsonErrc::InsufficientBuffer);
}

TEST(ScannerDifferential, SimpleObject) {
  const std::string input = "{\"a\":1}";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 16);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 16);
  ASSERT_TRUE(ref.ok);
  EXPECT_EQ(ref.indices, (std::vector<size_t>{0, 4, 6}));
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, WhitespaceVariants) {
  const std::string input = "\t\n\r { \"a\" : [ ] } \r\n\t";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 16);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 16);
  ASSERT_TRUE(ref.ok);
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, EscapedQuotesInsideStrings) {
  const std::string input = R"(["a\"b", "back\\slash", "end\""])";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 32);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 32);
  ASSERT_TRUE(ref.ok);
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, RawUtf8BytesOutsideAscii) {
  // "emoji <F0 9F A6 89>": multibyte UTF-8 must be treated as opaque data.
  const std::string input =
      "{\"emoji\":\"\xF0\x9F\xA6\x89\",\"na\xC3\xAFve\":1}";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 16);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 16);
  ASSERT_TRUE(ref.ok);
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, StructuralInsideStringsIgnored) {
  const std::string input = R"({"k":"a[b],{c}:d","j":2})";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 16);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 16);
  ASSERT_TRUE(ref.ok);
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, EmptyAndAdjacentStrings) {
  const std::string input = R"({"":[],"a""b":1})";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 16);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 16);
  ASSERT_TRUE(ref.ok);
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, NumbersAreOpaqueToScanner) {
  const std::string input = "[1e308,-1e-308,9007199254740993,0.1e+2]";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 16);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 16);
  ASSERT_TRUE(ref.ok);
  // [ at 0; separators at 6, 14, 31; ] at 38 — number bytes are class None.
  EXPECT_EQ(ref.indices, (std::vector<size_t>{0, 6, 14, 31, 38}));
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, UnclosedString) {
  const std::string input = "{\"a\":1,\"b";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 16);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 16);
  EXPECT_FALSE(ref.ok);
  EXPECT_EQ(ref.error, JsonErrc::InvalidJson);
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, DanglingEscape) {
  const std::string input = "\"abc\\";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 16);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 16);
  EXPECT_FALSE(ref.ok);
  EXPECT_EQ(ref.error, JsonErrc::InvalidJson);
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, CapacityExactAndOneShort) {
  const std::string input = "[1,[2],[3]]"; // 8 structural characters
  const auto exact_ref = run_scan(zuu::ondemand::reference_scan, input, 8);
  const auto exact_fast = run_scan(zuu::ondemand::scanner8, input, 8);
  ASSERT_TRUE(exact_ref.ok);
  EXPECT_EQ(exact_ref.count, 8);
  expect_identical(exact_ref, exact_fast);

  const auto short_ref = run_scan(zuu::ondemand::reference_scan, input, 7);
  const auto short_fast = run_scan(zuu::ondemand::scanner8, input, 7);
  EXPECT_FALSE(short_ref.ok);
  EXPECT_EQ(short_ref.error, JsonErrc::InsufficientBuffer);
  EXPECT_EQ(short_ref.indices.size(), 7);
  expect_identical(short_ref, short_fast);
}

TEST(ScannerDifferential, ZeroStructuralOutput) {
  const std::string input = "\"just a string\"";
  const auto ref = run_scan(zuu::ondemand::reference_scan, input, 4);
  const auto fast = run_scan(zuu::ondemand::scanner8, input, 4);
  ASSERT_TRUE(ref.ok);
  EXPECT_EQ(ref.count, 0);
  expect_identical(ref, fast);
}

TEST(ScannerDifferential, DeterministicFuzz) {
  // Deterministic PRNG (fixed seed): reproducible adversarial byte soup.
  std::mt19937 rng(20261108);
  const char alphabet[] = "{}[]\",: \\ \t\nabc123e.-+";
  std::uniform_int_distribution<int> pick(0, sizeof(alphabet) - 2);
  std::uniform_int_distribution<int> length(0, 4096);

  for (int iteration = 0; iteration < 500; ++iteration) {
    std::string input(static_cast<size_t>(length(rng)), '\0');
    for (char &c : input) {
      c = alphabet[pick(rng)];
    }
    const auto ref = run_scan(zuu::ondemand::reference_scan, input, 4096);
    const auto fast = run_scan(zuu::ondemand::scanner8, input, 4096);
    expect_identical(ref, fast);
    if (HasNonfatalFailure()) {
      ADD_FAILURE() << "divergence at fuzz iteration " << iteration;
      return;
    }
  }
}

// SWAR utility masks must match a serial per-byte reference exactly.
// Carries the P4-2 correctness fix: the textbook `(v - 0x01..) & ~v & 0x80..`
// form leaks borrows across lanes when used as a per-byte mask (30/200k
// random failures); the shipped carry-free form and swar_eq must never.
// @cite 10.1002_spe.2984 SWAR byte-parallel arithmetic idioms.
TEST(SwarUtil, MasksMatchSerialReference) {
  using zuu::core::uint64_t;
  using zuu::core::uint8_t;
  auto serial_zero = [](uint64_t v) {
    uint64_t mask = 0;
    for (int lane = 0; lane < 8; ++lane) {
      const auto byte = static_cast<uint8_t>(v >> (lane * 8));
      mask |= (byte == 0 ? uint64_t{0x80} : uint64_t{0}) << (lane * 8);
    }
    return mask;
  };
  auto serial_eq = [](uint64_t x, uint8_t c) {
    uint64_t mask = 0;
    for (int lane = 0; lane < 8; ++lane) {
      const auto byte = static_cast<uint8_t>(x >> (lane * 8));
      mask |= (byte == c ? uint64_t{0x80} : uint64_t{0}) << (lane * 8);
    }
    return mask;
  };

  // Adversarial patterns: zero-adjacent-to-0x01 (the textbook borrow case),
  // full lanes, single-lane hits.
  const std::vector<uint64_t> edges = {
      0x0000000000000000ULL, 0x0000000000000100ULL, 0x0100000000000000ULL,
      0x0000000001000000ULL, 0x0000000000000001ULL, 0x0101010101010101ULL,
      0xFFFFFFFFFFFFFFFFULL, 0x8000000000000080ULL, 0x8080808080808080ULL,
      0x0000000000000080ULL, 0x2222222222222222ULL, 0x5C5C5C5C5C5C5C5CULL,
  };
  for (const uint64_t x : edges) {
    EXPECT_EQ(zuu::utils::swar_has_zero_byte(x), serial_zero(x))
        << std::hex << x;
    EXPECT_EQ(zuu::utils::swar_eq(x, 0x22), serial_eq(x, 0x22))
        << std::hex << x;
    EXPECT_EQ(zuu::utils::swar_eq(x, 0x5C), serial_eq(x, 0x5C))
        << std::hex << x;
    EXPECT_EQ(zuu::utils::swar_eq(x, 0x00), serial_eq(x, 0x00))
        << std::hex << x;
  }

  // Deterministic random sweep (project fuzz seed, P1-3/P4 convention).
  std::mt19937_64 rng(20261108);
  const std::array<uint8_t, 6> byte_pool = {
      0x00, 0x01, 0x22, 0x5C, 0x7F, 0xFF,
  };
  std::uniform_int_distribution<size_t> pool_pick(0, byte_pool.size() - 1);
  for (int iteration = 0; iteration < 200000; ++iteration) {
    uint64_t x = rng();
    if (iteration % 3 == 0) {
      // Structured stress: borrow-prone lanes built from the pool.
      for (int lane = 0; lane < 8; ++lane) {
        const int shift = lane * 8;
        x = (x & ~(uint64_t{0xFF} << shift)) |
            (uint64_t{byte_pool[pool_pick(rng)]} << shift);
      }
    }
    const auto c = static_cast<uint8_t>(rng());
    EXPECT_EQ(zuu::utils::swar_has_zero_byte(x), serial_zero(x));
    EXPECT_EQ(zuu::utils::swar_eq(x, c), serial_eq(x, c));
  }
}

#if defined(__LITTLE_ENDIAN__) && __LITTLE_ENDIAN__ == 1 &&                    \
    defined(__IS_64_BIT__) && __IS_64_BIT__ == 1

TEST(ScannerVariants, AllVariantsMatchReference) {
  // scanner64 (SWAR), scanner128 (SSE2) and the static dispatch must
  // reproduce the reference exactly; structural_scan additionally validates
  // the once-resolved dispatch wiring (P4-2).
  using ScanFn = zuu::core::Result<zuu::core::size_t, zuu::core::JsonErrc> (*)(
      const char *const, zuu::core::size_t, zuu::core::size_t *,
      zuu::core::size_t);
  const std::vector<ScanFn> variants = {
      &zuu::ondemand::scanner8,
      &zuu::ondemand::scanner64,
      &zuu::ondemand::structural_scan,
#if defined(__SSE2__)
      &zuu::ondemand::scanner128,
#endif
  };

  const std::vector<std::string> cases = {
      "{\"a\":1}",
      "[1,[2],[3]]",
      "\"just a string\"",
      "{\"k\":\"a[b],{c}\"}",
      R"(["a\"b", "back\\slash", "end\""])",
      "{\"emoji\":\"\xF0\x9F\xA6\x89\",\"na\xC3\xAFve\":1}",
      "{\"\":\"[]\",\"a\\\\\":1}",
      "{\"unclosed",
      "\"dangling\\",
      "",
      "[1e308,-1e-308,9007199254740993,0.1e+2]",
      "\"\\\\\"",   // escaped backslash closes the string
      "\"\\\\\\\"", // escaped backslash then escaped quote
  };

  for (const auto &variant : variants) {
    for (const auto &input : cases) {
      const auto reference = run_scan(zuu::ondemand::reference_scan, input, 64);
      const auto fast = run_scan(variant, input, 64);
      expect_identical(reference, fast, input.c_str());
    }
  }
}

TEST(ScannerVariants, FuzzAgreesAcrossVariants) {
  std::mt19937 rng(424242);
  const char alphabet[] = "{}[]\",: \\ \t\nabc123e.-+";
  std::uniform_int_distribution<int> pick(0, sizeof(alphabet) - 2);
  std::uniform_int_distribution<int> length(0, 4096);

  using ScanFn = zuu::core::Result<zuu::core::size_t, zuu::core::JsonErrc> (*)(
      const char *const, zuu::core::size_t, zuu::core::size_t *,
      zuu::core::size_t);
  const std::vector<ScanFn> variants = {
      &zuu::ondemand::scanner8,
      &zuu::ondemand::scanner64,
      &zuu::ondemand::structural_scan,
#if defined(__SSE2__)
      &zuu::ondemand::scanner128,
#endif
  };

  for (int iteration = 0; iteration < 300; ++iteration) {
    std::string input(static_cast<size_t>(length(rng)), '\0');
    for (char &c : input) {
      c = alphabet[pick(rng)];
    }
    const auto reference = run_scan(zuu::ondemand::reference_scan, input, 4096);
    for (const auto &variant : variants) {
      const auto fast = run_scan(variant, input, 4096);
      expect_identical(reference, fast);
      if (HasNonfatalFailure()) {
        ADD_FAILURE() << "divergence at fuzz iteration " << iteration;
        return;
      }
    }
  }
}

#endif // little-endian 64-bit

TEST(JsonCharClassTable, MatchesReferenceCharacterSet) {
  // The codegen table itself is under test: its classes must equal the
  // reference character set (a table bug would corrupt every fast path).
  for (int byte = 0; byte < 256; ++byte) {
    const char c = static_cast<char>(static_cast<unsigned char>(byte));
    zuu::core::uint8_t expected = 0x00_u8;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      expected = expected | 0x01_u8;
    }
    if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',' || c == ':') {
      expected = expected | 0x02_u8;
    }
    if (c == '"') {
      expected = expected | 0x04_u8;
    }
    if (c == '\\') {
      expected = expected | 0x08_u8;
    }
    EXPECT_EQ(zuu::core::kJsonCharClass[byte], expected)
        << "kJsonCharClass mismatch at byte 0x" << std::hex << byte;
  }
}

TEST(JsonCheckerCorpus, PassCasesProduceIdenticalStructuralIndexes) {
  const auto files = corpus_files("pass");
  if (files.empty()) {
    GTEST_SKIP() << "json_checker corpus not available (ZE_JSON_CORPUS_DIR)";
  }
  for (const auto &path : files) {
    const std::string input = read_file(path);
    const auto ref = run_scan(zuu::ondemand::reference_scan, input, 65536);
    const auto fast = run_scan(zuu::ondemand::scanner8, input, 65536);
    ASSERT_TRUE(ref.ok) << path;
    ASSERT_GT(ref.count, size_t{0}) << path;
    expect_identical(ref, fast, path.c_str());
  }
}

TEST(JsonCheckerCorpus, FailCasesAgreeWithLegacyExceptions) {
  const auto files = corpus_files("fail");
  if (files.empty()) {
    GTEST_SKIP() << "json_checker corpus not available (ZE_JSON_CORPUS_DIR)";
  }
  for (const auto &path : files) {
    const std::string name = std::filesystem::path(path).filename().string();
    const std::string input = read_file(path);
    const auto ref = run_scan(zuu::ondemand::reference_scan, input, 65536);
    const auto fast = run_scan(zuu::ondemand::scanner8, input, 65536);
    expect_identical(ref, fast, path.c_str());

    const bool legacy_rfc8259_valid =
        (name == "fail1.json" || name == "fail18.json");
    if (legacy_rfc8259_valid) {
      // RFC 8259 allows top-level values (fail1) and does not limit nesting
      // depth (fail18): scanners must accept, the parser validates grammar.
      EXPECT_TRUE(ref.ok) << name;
    } else {
      // Layering note: json_checker fail cases test the FULL parser. Grammar
      // errors (missing colons, trailing commas, control characters, ...)
      // are invisible to the structural scanner by design — it only rejects
      // unterminated strings and dangling escapes. Whatever the verdict,
      // both scanners must agree on it (checked above).
      if (!ref.ok) {
        EXPECT_EQ(ref.error, JsonErrc::InvalidJson) << name;
      }
    }
  }
}

} // namespace

// Custom main: persist the export-helper sidecar (ZE_METADATA_OUT) when the
// run is instrumented, then execute the suite. Plain local runs are no-ops.
int main(int argc, char **argv) {
  zuu::testing::write_export_sidecar("ZE_METADATA_OUT", "ZE_RUN_ID",
                                     "ZE_GIT_COMMIT", "ze_json_scanner_test");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
