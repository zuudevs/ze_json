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
#include <cstdint>
#include <cstdio>
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
      "0",
      "-0",
      "1",
      "-1",
      "0.5",
      "-0.5",
      "1e5",
      "1E5",
      "1e+5",
      "1e-5",
      "1.5e2",
      "0e0",
      "0.0e-0",
      "12345678901234567890",
      "1e999999",
      "5e-324",
      "1e400",
      "2.5e-324",
      "100000000000000000000000",
      "0.0000001",
  };
  for (const char *text : valid) {
    EXPECT_TRUE(scan_number(text, std::strlen(text)).has_value()) << text;
  }
}

TEST(NumberGrammar, InvalidTokens) {
  const std::array<const char *, 17> invalid = {
      "",   "-",   "+1",  "01",   "-01", "00",   "1.",  ".5",       "-.5",
      "1e", "1e+", "1e-", "1.e5", "--1", "01.5", "-0.", "Infinity",
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
  EXPECT_TRUE(
      parses_to("9007199254740993", 9007199254740992.0)); // 2^53+1 rounds down
}

// Exact-decimal tie cases at the subnormal boundary (generated from
// Decimal(2)**-1075 — the strings are EXACT, so ties are exact).
TEST(NumberSemantics, SubnormalTiesToEven) {
  const char *half_ulp =
      "2."
      "470328229206232720882843964341106861825299013071623822127928412503377536"
      "351043759326499181808179961898982823477228588654633283551779698981993873"
      "980053909390631503565951557022639229085839244910518443593180284993653615"
      "250031937045767824921936562366986365848075700158576926990370631192827955"
      "855133292783433840935197801553124659726357957462276646527282722005637400"
      "648549997709659947045402082816622623785739345073633900796776193057750674"
      "017632467360096895134053553745851666113422376667860416215968046191446729"
      "184030053005753084904876539171138659164623952491262365388187963623937328"
      "042389101867234849766823508986338858792562830275599565752445550725518931"
      "369083625477918694866799496832404970582102851318545139621383772282614543"
      "7693412532098591327667236328125e-324";
  const char *one_and_half =
      "7."
      "410984687618698162648531893023320585475897039214871466383785237510132609"
      "053131277979497545424539885696948470431685765963899850655339096945981621"
      "940161728171894510697854671067917687257517734731555330779540854980960845"
      "750095811137303474765809687100959097544227100475730780971111893578483867"
      "565399878350301522805593404659373979179073872386829939581848166016912201"
      "945649993128979841136206248449867871357218035220901702390328579173252022"
      "052897402080290685402160661237554998340267130003581248647904138574340187"
      "552090159017259254714629617513415977493871857473787096164563890871811984"
      "127167305601704549300470526959016576377688490826798697257336652176556794"
      "107250876433756084600398490497214911746308553955635418864151316847843631"
      "3080237596295773983001708984375e-324";
  const char *two_and_half =
      "1."
      "235164114603116360441421982170553430912649506535811911063964206251688768"
      "175521879663249590904089980949491411738614294327316641775889849490996936"
      "990026954695315751782975778511319614542919622455259221796590142496826807"
      "625015968522883912460968281183493182924037850079288463495185315596413977"
      "927566646391716920467598900776562329863178978731138323263641361002818700"
      "324274998854829973522701041408311311892869672536816950398388096528875337"
      "008816233680048447567026776872925833056711188333930208107984023095723364"
      "592015026502876542452438269585569329582311976245631182694093981811968664"
      "021194550933617424883411754493169429396281415137799782876222775362759465"
      "684541812738959347433399748416202485291051425659272569810691886141307271"
      "88467062660492956638336181640625e-323";

  EXPECT_TRUE(parses_to(half_ulp, 0.0));              // exact half -> even -> 0
  EXPECT_TRUE(parses_to(one_and_half, from_bits(2))); // tie -> even -> 2 ulp
  EXPECT_TRUE(parses_to(two_and_half, from_bits(2))); // tie -> even -> 2 ulp
}

// ---------------------------------------------------------- strtod parity

TEST(NumberStrtod, CuratedBoundaries) {
  const std::array<const char *, 31> cases = {
      "0.1",
      "0.2",
      "0.3",
      "2.2250738585072011e-308",
      "2.2250738585072014e-308",
      "4.9406564584124654e-324",
      "2.2250738585072011e-308",
      "1.7976931348623157e308",
      "1.7976931348623158e308",
      "8.98846567431158e307",
      "1e23",
      "9007199254740993",
      "9007199254740995",
      "123456789012345678901234567890",
      "1e-323",
      "1e-322",
      "3.141592653589793238462643383279",
      "2.718281828459045235360287471352",
      "1.5e-10",
      "6.02214076e23",
      "1."
      "797693134862315807937289714053034150799341327100378269361737789804449682"
      "927647509466490179775872070963302864166928879109465555478519404026306574"
      "886715058206819089020007083836762738548458177115317644757302700698555713"
      "669596228429148198608349364752927190741684443655107043427115596995080930"
      "42880177904174497792e308",
      "0.000001",
      "123456.789",
      "99999999999999999999",
      "0.123456789012345678901234567890",
      "1e22",
      "1e-22",
      "18446744073709551615",
      "18446744073709551616",
      "9.999999999999999e22",
      "7.076116457632277e-10",
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
      std::string text =
          mant + "e" + (e < 0 ? "-" : "") + std::to_string(e < 0 ? -e : e);
      EXPECT_TRUE(same_as_strtod(text)) << text;
      // Fractional variant: point after the first digit (needs >= 2
      // digits; a single digit gets a ".0" tail so the token stays valid).
      std::string ftext =
          digits == 1 ? mant + ".0" : mant.substr(0, 1) + "." + mant.substr(1);
      ftext += std::string("e") + (e < 0 ? "-" : "") +
               std::to_string(e < 0 ? -e : e);
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
    text += "e" + (e < 0 ? std::string("-") : std::string()) +
            std::to_string(e < 0 ? -e : e);
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
      text.insert(text.begin() + 1 +
                      static_cast<size_t>(rng() % (text.size() - 1)),
                  '.');
    }
    if (shape <= 2 || shape == 4) {
      const int e = exp_dist(rng);
      text += "e" + (e < 0 ? std::string("-") : std::string()) +
              std::to_string(e < 0 ? -e : e);
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

TEST(NumberRegionFastPath, MatchesBitSerialReference) {
  // P4-3 C2 gate: the reciprocal fast path (kPowerOfFiveReciprocal, paper
  // Section 9.1) must agree bit-for-bit with the proven bit-serial long
  // division over the ENTIRE fast-path input contract — every (m, q) pair
  // parse_number sends to region_exact's negative branch: 1 <= m < 2^53,
  // k = -q in [1, 22]. Corner probes pin the bit-length extremes (power-of-
  // two significands, 2^53-1, all-zero tail m=1) on every k; the seeded
  // fuzz sweeps ordinary mantissas densely. A mismatch here means the
  // single-multiply quotient+correction scheme deviated from the exact
  // result and the candidate is unusable by construction.
  // @cite 10.1002_spe.2984 Section 9.1 — reciprocal division equality.
  static const std::array<core_u64, 23> kCorners{
      1,            2,           3,            5,         7,
      10,           11,          47,           100,       999,
      1000000,      123456789,   (1ull << 1),  (1ull << 10),
      (1ull << 20), (1ull << 40), (1ull << 50), (1ull << 52),
      (1ull << 52) + 1, (1ull << 52) - 1, (1ull << 53) - 1,
      0xF0F0F0F0ull,  0x800000aaaaaaull,
  };
  for (const core_u64 m : kCorners) {
    ASSERT_GE(m, 1) << "probe out of contract";
    ASSERT_LT(m, (core_u64{1} << 53)) << "probe out of contract";
    for (int k = 1; k <= 22; ++k) {
      const double fast = zuu::ondemand::detail::region_exact_neg(m, k, false);
      const double ref = zuu::ondemand::detail::region_div_ref(m, k, false);
      ASSERT_EQ(bits_of(fast), bits_of(ref))
          << "m=" << m << " k=" << k << " fast=" << fast << " ref=" << ref;
      // Sign pass-through.
      const double fast_neg =
          zuu::ondemand::detail::region_exact_neg(m, k, true);
      const double ref_neg =
          zuu::ondemand::detail::region_div_ref(m, k, true);
      ASSERT_EQ(bits_of(fast_neg), bits_of(ref_neg))
          << "m=" << m << " k=" << k << " (negative)";
    }
  }
  std::mt19937_64 rng(20261110);
  for (int trial = 0; trial < 150000; ++trial) {
    const core_u64 m = 1 + rng() % ((core_u64{1} << 53) - 1);
    const int k = 1 + static_cast<int>(rng() % 22);
    const bool negative = (rng() & 1) != 0;
    const double fast =
        zuu::ondemand::detail::region_exact_neg(m, k, negative);
    const double ref = zuu::ondemand::detail::region_div_ref(m, k, negative);
    ASSERT_EQ(bits_of(fast), bits_of(ref))
        << "m=" << m << " k=" << k << " negative=" << negative
        << " fast=" << fast << " ref=" << ref;
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
