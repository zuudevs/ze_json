// Number parser differential + correctness tests (TASKS P7-2).
//
// Rationale: the number layer must be BIT-EXACT with the comparison
// baseline (V8 JSON.parse semantics: correctly-rounded IEEE 754, ±Infinity
// on overflow, ±0 on underflow) before any performance claim is admissible
// (cpp-highperf-json rule 4; AGENTS.md section 6). The native oracle is
// std::strtod (correctly rounded per C99; identical to V8 for in-range,
// overflow and underflow values). Gates:
//   - RFC 8259 grammar cases (hard-coded verdicts);
//   - V8-parity semantics (overflow, underflow, -0, subnormal ties);
//   - bitwise parity with strtod over curated boundaries + fuzz;
//   - random double bit-pattern round-trip through "%.17g";
//   - long digit strings (slow path) vs strtod.
//
// @cite 10.1002_spe.2984 number parsing at a gigabyte per second — the
//       single-rounding requirement these tests pin down.
#include <gtest/gtest.h>

#include <ze_json/ondemand/number.hpp>

#include "export_sidecar.hpp" // export-helper sidecar contract (AGENTS.md section 4)

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

// The test only needs the u64 type; alias to keep the literals tidy.
using core_u64 = std::uint64_t;

using zuu::ondemand::parse_number;
using zuu::ondemand::scan_number;

[[nodiscard]] core_u64 bits_of(double v) noexcept {
  core_u64 bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  return bits;
}

[[nodiscard]] double from_bits(core_u64 bits) noexcept {
  double v = 0.0;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

[[nodiscard]] bool parses_to(const char *text, double expected) noexcept {
  const auto r = parse_number(text, std::strlen(text));
  return r.has_value() && bits_of(r.value()) == bits_of(expected);
}

[[nodiscard]] bool same_as_strtod(const std::string &text) noexcept {
  const auto r = parse_number(text.data(), text.size());
  if (!r.has_value()) {
    return false;
  }
  const double oracle = std::strtod(text.c_str(), nullptr);
  return bits_of(r.value()) == bits_of(oracle);
}

// ------------------------------------------------------------------- grammar

TEST(NumberGrammar, ValidTokens) {
  const std::array<const char *, 20> valid = {
    "0",        "-0",        "1",        "-1",         "0.5",
    "-0.5",     "1e5",       "1E5",      "1e+5",       "1e-5",
    "1.5e2",    "0e0",       "0.0e-0",   "12345678901234567890",
    "1e999999", "5e-324",    "1e400",    "2.5e-324",   "100000000000000000000000",
    "0.0000001",
  };
  for (const char *text : valid) {
    EXPECT_TRUE(scan_number(text, std::strlen(text)).has_value()) << text;
  }
}

TEST(NumberGrammar, InvalidTokens) {
  const std::array<const char *, 17> invalid = {
    "",     "-",   "+1",  "01",  "-01", "00",  "1.",  ".5",
    "-.5",  "1e",  "1e+", "1e-", "1.e5", "--1", "01.5", "-0.",
    "Infinity",
  };
  for (const char *text : invalid) {
    EXPECT_TRUE(scan_number(text, std::strlen(text)).has_error()) << text;
  }
}

TEST(NumberGrammar, PrefixConsumption) {
  // The on-demand parser calls scan at a position inside a document: the
  // token is a PREFIX of the buffer — a following garbage byte is the
  // CALLER's delimiter check, not the number layer's.
  const auto r = parse_number("1.5xyz", 6);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(bits_of(r.value()), bits_of(1.5));
  EXPECT_TRUE(parses_to("0x1", 0.0));   // consumes "0"
  EXPECT_TRUE(parses_to("1e5.5", 1e5)); // consumes "1e5"
  EXPECT_TRUE(parses_to("1e+0x", 1.0)); // consumes "1e+0"
  const auto s = scan_number("[123", 4);
  EXPECT_TRUE(s.has_error()); // '[' is not a number start
}

// -------------------------------------------------------------- V8 parity

TEST(NumberSemantics, V8ParityEdges) {
  const double inf = from_bits(0x7FF0000000000000ull);
  const double neg_inf = from_bits(0xFFF0000000000000ull);
  const double neg_zero = from_bits(0x8000000000000000ull);
  const double min_sub = from_bits(1); // 5e-324

  EXPECT_TRUE(parses_to("1e400", inf));
  EXPECT_TRUE(parses_to("-1e400", neg_inf));
  EXPECT_TRUE(parses_to("1e999999", inf));
  EXPECT_TRUE(parses_to("1e-400", 0.0));
  EXPECT_TRUE(parses_to("-1e-400", neg_zero));
  EXPECT_TRUE(parses_to("-0", neg_zero));
  EXPECT_TRUE(parses_to("-0.0e0", neg_zero));
  EXPECT_TRUE(parses_to("5e-324", min_sub));
  EXPECT_TRUE(parses_to("9007199254740993", 9007199254740992.0)); // 2^53+1 rounds down
}

// Exact-decimal tie cases at the subnormal boundary (generated from
// Decimal(2)**-1075 — the strings are EXACT, so ties are exact).
TEST(NumberSemantics, SubnormalTiesToEven) {
  const char *half_ulp =
      "2.4703282292062327208828439643411068618252990130716238221279284125033775363510437593264991818081799618989828234772285886546332835517796989819938739800539093906315035659515570226392290858392449105184435931802849936536152500319370457678249219365623669863658480757001585769269903706311928279558551332927834338409351978015531246597263579574622766465272827220056374006485499977096599470454020828166226237857393450736339007967761930577506740176324673600968951340535537458516661134223766678604162159680461914467291840300530057530849048765391711386591646239524912623653881879636239373280423891018672348497668235089863388587925628302755995657524455507255189313690836254779186948667994968324049705821028513185451396213837722826145437693412532098591327667236328125e-324";
  const char *one_and_half =
      "7.4109846876186981626485318930233205854758970392148714663837852375101326090531312779794975454245398856969484704316857659638998506553390969459816219401617281718945106978546710679176872575177347315553307795408549809608457500958111373034747658096871009590975442271004757307809711118935784838675653998783503015228055934046593739791790738723868299395818481660169122019456499931289798411362062484498678713572180352209017023903285791732520220528974020802906854021606612375549983402671300035812486479041385743401875520901590172592547146296175134159774938718574737870961645638908718119841271673056017045493004705269590165763776884908267986972573366521765567941072508764337560846003984904972149117463085539556354188641513168478436313080237596295773983001708984375e-324";
  const char *two_and_half =
      "1.23516411460311636044142198217055343091264950653581191106396420625168876817552187966324959090408998094949141173861429432731664177588984949099693699002695469531575178297577851131961454291962245525922179659014249682680762501596852288391246096828118349318292403785007928846349518531559641397792756664639171692046759890077656232986317897873113832326364136100281870032427499885482997352270104140831131189286967253681695039838809652887533700881623368004844756702677687292583305671118833393020810798402309572336459201502650287654245243826958556932958231197624563118269409398181196866402119455093361742488341175449316942939628141513779978287622277536275946568454181273895934743339974841620248529105142565927256981069188614130727188467062660492956638336181640625e-323";

  EXPECT_TRUE(parses_to(half_ulp, 0.0));              // exact half -> even -> 0
  EXPECT_TRUE(parses_to(one_and_half, from_bits(2))); // tie -> even -> 2 ulp
  EXPECT_TRUE(parses_to(two_and_half, from_bits(2))); // tie -> even -> 2 ulp
}

// ---------------------------------------------------------- strtod parity

TEST(NumberStrtod, CuratedBoundaries) {
  const std::array<const char *, 31> cases = {
    "0.1",                      "0.2",
    "0.3",                      "2.2250738585072011e-308",
    "2.2250738585072014e-308",  "4.9406564584124654e-324",
    "2.2250738585072011e-308",  "1.7976931348623157e308",
    "1.7976931348623158e308",   "8.98846567431158e307",
    "1e23",                     "9007199254740993",
    "9007199254740995",         "123456789012345678901234567890",
    "1e-323",                   "1e-322",
    "3.141592653589793238462643383279", "2.718281828459045235360287471352",
    "1.5e-10",                  "6.02214076e23",
    "1.79769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497792e308",
    "0.000001",                 "123456.789",
    "99999999999999999999",     "0.123456789012345678901234567890",
    "1e22",                     "1e-22",
    "18446744073709551615",     "18446744073709551616",
    "9.999999999999999e22",     "7.076116457632277e-10",
  };
  for (const char *text : cases) {
    EXPECT_TRUE(same_as_strtod(text)) << text;
  }
}

TEST(NumberStrtod, MantissaAndExponentSweep) {
  // Every 1..19-digit repdigit-ish mantissa family across the exact-region
  // boundary (|exp| <= 22 vs beyond), all compared bitwise with strtod.
  for (int digits = 1; digits <= 19; ++digits) {
    std::string mant(static_cast<size_t>(digits), '7');
    for (int e = -30; e <= 30; ++e) {
      std::string text = mant + "e" + (e < 0 ? "-" : "") + std::to_string(e < 0 ? -e : e);
      EXPECT_TRUE(same_as_strtod(text)) << text;
      // Fractional variant: point after the first digit (needs >= 2
      // digits; a single digit gets a ".0" tail so the token stays valid).
      std::string ftext = digits == 1
          ? mant + ".0"
          : mant.substr(0, 1) + "." + mant.substr(1);
      ftext += std::string("e") + (e < 0 ? "-" : "")
               + std::to_string(e < 0 ? -e : e);
      EXPECT_TRUE(same_as_strtod(ftext)) << ftext;
    }
  }
}

TEST(NumberStrtod, LongDigitStrings) {
  // Slow-path exercise: 20..800 significant digits with varied exponents.
  std::mt19937_64 rng(20261108);
  std::uniform_int_distribution<int> digit_dist(0, 9);
  for (int trial = 0; trial < 2000; ++trial) {
    const size_t len = 20 + static_cast<size_t>(rng() % 781);
    std::string text;
    text.reserve(len + 8);
    text += static_cast<char>('1' + rng() % 9);
    for (size_t i = 1; i < len; ++i) {
      text += static_cast<char>('0' + digit_dist(rng));
    }
    text.insert(text.begin() + 1 + static_cast<size_t>(rng() % (len - 1)), '.');
    const int e = static_cast<int>(rng() % 700) - 350;
    text += "e" + (e < 0 ? std::string("-") : std::string()) + std::to_string(e < 0 ? -e : e);
    EXPECT_TRUE(same_as_strtod(text)) << text;
  }
}

TEST(NumberStrtod, FuzzValidDecimals) {
  std::mt19937_64 rng(20261108);
  std::uniform_int_distribution<int> digit_dist(0, 9);
  std::uniform_int_distribution<int> shape_dist(0, 5);
  std::uniform_int_distribution<int> exp_dist(-40, 40);
  for (int trial = 0; trial < 300000; ++trial) {
    const size_t len = 1 + rng() % 25;
    std::string text;
    text += static_cast<char>('1' + rng() % 9);
    for (size_t i = 1; i < len; ++i) {
      text += static_cast<char>('0' + digit_dist(rng));
    }
    const int shape = shape_dist(rng);
    if ((shape == 0 || shape == 3) && text.size() >= 2) {
      text.insert(text.begin() + 1 + static_cast<size_t>(rng() % (text.size() - 1)), '.');
    }
    if (shape <= 2 || shape == 4) {
      const int e = exp_dist(rng);
      text += "e" + (e < 0 ? std::string("-") : std::string())
              + std::to_string(e < 0 ? -e : e);
    }
    if (trial % 3 == 0) {
      text = "-" + text;
    }
    EXPECT_TRUE(same_as_strtod(text)) << text << " (trial " << trial << ")";
  }
}

TEST(NumberRoundTrip, RandomBitPatterns) {
  // Every finite double, printed with %.17g (always round-trips), must be
  // reparsed bit-exactly by the number layer.
  std::mt19937_64 rng(20261109);
  char buf[64];
  for (int trial = 0; trial < 200000; ++trial) {
    core_u64 bits = rng();
    if (trial % 8 == 0) {
      bits = rng() % 0x100000000ull; // bias toward subnormals / small values
    }
    const double original = from_bits(bits);
    if (!std::isfinite(original)) {
      continue; // inf/NaN have no JSON number spelling
    }
    std::snprintf(buf, sizeof(buf), "%.17g", original);
    const auto r = parse_number(buf, std::strlen(buf));
    ASSERT_TRUE(r.has_value()) << buf;
    ASSERT_EQ(bits_of(r.value()), bits) << buf;
  }
}

} // namespace

// Custom main: persist the export-helper sidecar (ZE_METADATA_OUT) when the
// run is instrumented, then execute the suite. Plain local runs are no-ops.
int main(int argc, char **argv) {
  zuu::testing::write_export_sidecar("ZE_METADATA_OUT", "ZE_RUN_ID",
                                     "ZE_GIT_COMMIT", "ze_json_number_test");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
