// Reference parser differential + verdict tests (TASKS P7-6).
//
// Rationale: the on-demand parser's verdicts must be reproduced by the
// plain scalar byte walker (reference_parser.hpp) at FULL traversal —
// every skip latitude undone, every framing/grammar layer run — before
// any parser performance claim is admissible (cpp-highperf-json rule 4;
// AGENTS.md section 6). Gates:
//   - json_checker: fail2..fail33 MUST reject on BOTH sides EXCEPT the
//     two legacy RFC-8259 exceptions (fail1 = top-level string, fail18 =
//     20-level nesting — grammar-valid, V8 JSON.parse accepts both; see
//     tests/CMakeLists.txt and reference_parser.hpp), which MUST accept;
//     pass1..pass3 MUST accept;
//   - benchmark corpora: verdict equality on every corpus/benchmarks
//     document (all must accept);
//   - adversarial framing fixtures: trailing commas, cross-type closers,
//     missing delimiters, stray structural bytes, scalar-root tails,
//     object-key grammar ({"a"} / {1:2} reject), brackets inside
//     strings, number/string corners — both sides agree AND match the
//     pinned outcome;
//   - event invariants of the on-demand walk (ts monotonic non-
//     decreasing, root first, VALUES only — keys are not events).
//
// Deep-nesting stays the lazy layer's own gate (value_test walks it
// iteratively); the reference recurses by design and only runs the
// test corpus.
//
// @cite 10.1002_spe.3313 on-demand JSON — full-walk verdict anchor
// @cite 10.14778_3115404.3115416 Mison — the index is not required for
//       correctness, only for speed
#include <gtest/gtest.h>

#include <ze_json/core/error.hpp>
#include <ze_json/core/integral.hpp>
#include <ze_json/ondemand/document.hpp>
#include <ze_json/ondemand/reference_parser.hpp>
#include <ze_json/ondemand/value.hpp>
#include <ze_json/utils/move.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "export_sidecar.hpp"

namespace {

using zuu::core::JsonErrc;
using zuu::core::Result;
using zuu::ondemand::NodeType;

struct Event {
  zuu::core::uint64_t ts;
  NodeType kind;
};

enum class Verdict : zuu::core::uint8_t { Accept = 0, Reject = 1 };

std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  // Braces: the paren form is the classic most-vexing-parse declaration.
  return std::string{std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>()};
}

// The on-demand FULL walk: every child visited, every scalar promoted
// through its validation accessor (get_double / get_string_span /
// get_bool / is_null). Any framing or grammar violation rejects.
Result<zuu::core::size_t, JsonErrc>
walk_events(const zuu::ondemand::Value &value, std::vector<Event> &out)
    NOEXCEPT {
  const NodeType kind = value.type();
  out.push_back(Event{value.token_start(), kind});
  switch (kind) {
  case NodeType::Object: {
    const auto begin = value.begin_object();
    if (!begin) {
      return Result<zuu::core::size_t, JsonErrc>(begin.error());
    }
    auto iter = begin.value();
    while (!iter.done_) {
      if (iter.failed_ != JsonErrc::None) {
        return Result<zuu::core::size_t, JsonErrc>(iter.failed_);
      }
      const auto field = *iter;
      if (!field) {
        return Result<zuu::core::size_t, JsonErrc>(field.error());
      }
      auto child = walk_events(field.value().value, out);
      if (!child) {
        return zuu::utils::move(child);
      }      ++iter;
    }
    if (iter.failed_ != JsonErrc::None) {
      return Result<zuu::core::size_t, JsonErrc>(iter.failed_);
    }
    return Result<zuu::core::size_t, JsonErrc>(out.size());
  }
  case NodeType::Array: {
    const auto begin = value.begin_array();
    if (!begin) {
      return Result<zuu::core::size_t, JsonErrc>(begin.error());
    }
    auto iter = begin.value();
    while (!iter.done_) {
      if (iter.failed_ != JsonErrc::None) {
        return Result<zuu::core::size_t, JsonErrc>(iter.failed_);
      }
      const auto element = *iter;
      if (!element) {
        return Result<zuu::core::size_t, JsonErrc>(element.error());
      }
      auto child = walk_events(element.value(), out);
      if (!child) {
        return zuu::utils::move(child);
      }
      ++iter;
    }
    if (iter.failed_ != JsonErrc::None) {
      return Result<zuu::core::size_t, JsonErrc>(iter.failed_);
    }
    return Result<zuu::core::size_t, JsonErrc>(out.size());
  }
  case NodeType::String: {
    const auto span = value.get_string_span();
    if (!span) {
      return Result<zuu::core::size_t, JsonErrc>(span.error());
    }
    return Result<zuu::core::size_t, JsonErrc>(out.size());
  }
  case NodeType::Number: {
    const auto number = value.get_double();
    if (!number) {
      return Result<zuu::core::size_t, JsonErrc>(number.error());
    }
    return Result<zuu::core::size_t, JsonErrc>(out.size());
  }
  case NodeType::BoolTrue:
  case NodeType::BoolFalse: {
    const auto literal = value.get_bool();
    if (!literal) {
      return Result<zuu::core::size_t, JsonErrc>(literal.error());
    }
    return Result<zuu::core::size_t, JsonErrc>(out.size());
  }
  case NodeType::Null:
    return value.is_null()
               ? Result<zuu::core::size_t, JsonErrc>(out.size())
               : Result<zuu::core::size_t, JsonErrc>(JsonErrc::InvalidJson);
  case NodeType::Unknown:
  default:
    return Result<zuu::core::size_t, JsonErrc>(JsonErrc::InvalidJson);
  }
}

//! Full on-demand document verdict + events. The Document must outlive
//! the walk (Value holds a raw document pointer), so it lives in a
//! local moved out of the open result.
Verdict on_demand_verdict(const std::string &data, std::vector<Event> &events) {
  // Arena footprint upper bound (document.hpp): three blocks of at most
  // src_size * sizeof(size_t) entries (index, match, stack) + alignment.
  std::vector<zuu::core::size_t> arena(data.size() * 3 / sizeof(
                                           zuu::core::size_t) *
                                           sizeof(zuu::core::size_t) +
                                       64);
  zuu::ondemand::Arena backing(arena.data(),
                               arena.size() * sizeof(zuu::core::size_t));
  auto opened = zuu::ondemand::Document::open(
      backing, data.data(), static_cast<zuu::core::size_t>(data.size()));
  if (!opened) {
    events.clear();
    // EmptyJson / InvalidJson / InsufficientBuffer are all "no verdict
    // value root" — the verdict gate counts every non-accept as Reject.
    return Verdict::Reject;
  }
  auto document = zuu::utils::move(opened.value());
  const auto root = document.get_root();
  if (!root) {
    events.clear();
    return Verdict::Reject;
  }
  const auto walked = walk_events(root.value(), events);
  if (!walked) {
    events.clear();
    return Verdict::Reject;
  }
  return Verdict::Accept;
}

//! Scalar reference verdict (the anchor of this gate).
Verdict reference_verdict(const std::string &data) {
  const auto consumed = zuu::ondemand::ref_parse_document(
      data.data(), static_cast<zuu::core::size_t>(data.size()));
  return consumed.has_value() ? Verdict::Accept : Verdict::Reject;
}

struct FixtureCase {
  const char *text;
  Verdict expected;
};

//! Both walks must agree with each other AND with the pinned expectation.
void compare_verdicts(const FixtureCase &item, std::size_t index) {
  const std::string text(item.text);
  std::vector<Event> events;
  const Verdict od = on_demand_verdict(text, events);
  const Verdict ref = reference_verdict(text);
  EXPECT_EQ(od, item.expected)
      << "index " << index << " on-demand walk: \"" << item.text << "\"";
  EXPECT_EQ(ref, item.expected)
      << "index " << index << " reference walk: \"" << item.text << "\"";
}

//! Events of a full walk must stay prefix-ordered: ts non-decreasing
//! over the source, root first, not empty for a valid document.
void assert_events_in_order(const std::vector<Event> &events,
                            const char *text) {
  EXPECT_GT(events.size(), std::size_t{0}) << text;
  for (std::size_t i = 1; i < events.size(); ++i) {
    EXPECT_GE(events[i].ts, events[i - 1].ts) << text;
  }
}

//! Runs one corpus directory through both verdict paths; collects the
//! name + sides of every document that disagrees with `expected`.
std::vector<std::string>
corpus_disagreements(const std::filesystem::path &dir, Verdict expected) {
  std::vector<std::string> failures;
  if (!std::filesystem::exists(dir)) {
    failures.push_back("missing corpus dir: " + dir.string());
    return failures;
  }
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    const auto name = entry.path().filename().string();
    if (entry.path().extension() == ".json" && name != "MANIFEST.json") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  for (const auto &path : files) {
    const std::string data = read_file(path);
    const Verdict ref = reference_verdict(data);
    std::vector<Event> events;
    const Verdict od = on_demand_verdict(data, events);
    if (ref != expected || od != expected) {
      failures.push_back(
          path.filename().string() + ": reference=" +
          (ref == Verdict::Accept ? "accept" : "reject") + " on-demand=" +
          (od == Verdict::Accept ? "accept" : "reject") + " expected=" +
          (expected == Verdict::Accept ? "accept" : "reject"));
      continue;
    }
    if (expected == Verdict::Accept) {
      EXPECT_NO_FATAL_FAILURE(assert_events_in_order(events,
                                                     path.filename().string()
                                                         .c_str()));
    }
  }
  return failures;
}

} // namespace

// The corpus gate: json_checker's malformed documents reject on BOTH
// the scalar reference and the on-demand walk — with the two legacy
// exceptions (fail1 = top-level string, fail18 = 20-level nesting,
// grammar-valid, V8 JSON.parse accepts both) accepting on both sides.
// Only the `fail*.json` set participates here (pass1..3 have their own
// gate below).
TEST(ReferenceParser, JsonCheckerVerdicts) {
  const std::filesystem::path dir(ZE_JSON_CORPUS_DIR);
  ASSERT_TRUE(std::filesystem::exists(dir)) << ZE_JSON_CORPUS_DIR;

  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    const auto name = entry.path().filename().string();
    if (entry.path().extension() == ".json" && name.rfind("fail", 0) == 0) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  EXPECT_EQ(files.size(), std::size_t{33})
      << "json_checker must hold 33 fail documents";

  std::size_t rejected = 0;
  std::size_t accepted_exceptions = 0;
  for (const auto &path : files) {
    const std::string name = path.filename().string();
    const bool legacy = (name == "fail1.json" || name == "fail18.json");
    const Verdict expected = legacy ? Verdict::Accept : Verdict::Reject;
    const std::string data = read_file(path);
    const Verdict ref = reference_verdict(data);
    std::vector<Event> events;
    const Verdict od = on_demand_verdict(data, events);
    EXPECT_EQ(od, expected) << name << " on-demand walk";
    EXPECT_EQ(ref, expected) << name << " reference walk";
    if (od == Verdict::Accept) {
      accepted_exceptions += 1;
      EXPECT_TRUE(legacy) << name << " accepted outside the legacy pair";
      EXPECT_NO_FATAL_FAILURE(assert_events_in_order(events, name.c_str()));
    } else {
      rejected += 1;
      EXPECT_FALSE(legacy) << name << " rejected on the legacy pair";
    }
  }
  EXPECT_EQ(rejected, std::size_t{31})
      << "31 malformed documents reject; fail1/fail18 are the legacy pair";
  EXPECT_EQ(accepted_exceptions, std::size_t{2});
}

// The 3 passing json_checker documents must accept on both sides.
TEST(ReferenceParser, JsonCheckerPassAccepted) {
  const std::filesystem::path dir(ZE_JSON_CORPUS_DIR);
  std::vector<std::string> failures;
  for (const char *name : {"pass1.json", "pass2.json", "pass3.json"}) {
    const std::string data = read_file(dir / name);
    const Verdict ref = reference_verdict(data);
    std::vector<Event> events;
    const Verdict od = on_demand_verdict(data, events);
    if (ref != Verdict::Accept || od != Verdict::Accept) {
      failures.push_back(std::string(name) + ": reference=" +
                         (ref == Verdict::Accept ? "accept" : "reject") +
                         " on-demand=" +
                         (od == Verdict::Accept ? "accept" : "reject"));
      continue;
    }
    EXPECT_NO_FATAL_FAILURE(
        assert_events_in_order(events, name));
  }
  EXPECT_EQ(failures.size(), std::size_t{0});
  for (const auto &line : failures) {
    ADD_FAILURE() << line;
  }
}

// Every benchmark corpus document must accept on both sides (verdict
// equality over the whole measurement corpus).
TEST(ReferenceParser, BenchmarkCorpusVerdictEquality) {
  const std::filesystem::path bench(ZE_JSON_BENCH_CORPUS_DIR);
  std::vector<std::string> failures;
  for (const char *sub :
       {"checker_batch", "saq_payload_v1", "synthetic_1k", "synthetic_10k",
        "synthetic_100k", "synthetic_1mb"}) {
    auto fails_sub = corpus_disagreements(bench / sub, Verdict::Accept);
    failures.insert(failures.end(), fails_sub.begin(), fails_sub.end());
  }
  EXPECT_EQ(failures.size(), std::size_t{0});
  for (const auto &line : failures) {
    ADD_FAILURE() << line;
  }
}

// Adversarial framing: verdict pair agreement AND the pinned outcome.
TEST(ReferenceParser, AdversarialFraming) {
  const std::vector<FixtureCase> cases = {
      {"", Verdict::Reject},
      {"  \n\t\r ", Verdict::Reject},
      {"{", Verdict::Reject},
      {"[", Verdict::Reject},
      {"{[}", Verdict::Reject},
      {"[}", Verdict::Reject},
      {"{:", Verdict::Reject},
      {"tru", Verdict::Reject},
      {"nul", Verdict::Reject},
      {"fals", Verdict::Reject},
      {"truex", Verdict::Reject},
      {"nullx", Verdict::Reject},
      {"tRue", Verdict::Reject},
      {" == {} ==", Verdict::Reject},
      {"<>", Verdict::Reject},
      {"[]][", Verdict::Reject},
      {"}{", Verdict::Reject},
      {"{ }{ }", Verdict::Reject},
      {"1.", Verdict::Reject},
      {"01", Verdict::Reject},
      {".5", Verdict::Reject},
      {"-", Verdict::Reject},
      {"- ", Verdict::Reject},
      {"+ 1", Verdict::Reject},
      {"+1", Verdict::Reject},
      {"1e", Verdict::Reject},
      {"1e+", Verdict::Reject},
      {"1e-3x", Verdict::Reject},
      {"0.e1", Verdict::Reject},
      {"00.1", Verdict::Reject},
      {"1 2", Verdict::Reject},
      {"1 ,", Verdict::Reject},
      {"true x", Verdict::Reject},
      {"\"a\" 4", Verdict::Reject},
      {"\"just a string\" trailing", Verdict::Reject},
      {"[] []", Verdict::Reject},
      {"{} {}", Verdict::Reject},
      {"[] abc", Verdict::Reject},
      {"[1 2]", Verdict::Reject},
      {"[\"a\" \"b\"]", Verdict::Reject},
      {"[1,]", Verdict::Reject},
      {"[,]", Verdict::Reject},
      {"[1, 2", Verdict::Reject},
      {"[1:", Verdict::Reject},
      {"[1", Verdict::Reject},
      {"[[[", Verdict::Reject},
      {"[{]", Verdict::Reject},
      {"[{]}", Verdict::Reject},  // cross-type closer
      {"[[}", Verdict::Reject},
      {"[{]}}", Verdict::Reject},
      {"[}]", Verdict::Reject},
      {"{]", Verdict::Reject},
      {"{}}", Verdict::Reject},
      {"{\"]\"}", Verdict::Reject}, // one closer, key not followed by ':'
      {"{[1,2]}", Verdict::Reject},  // key must be a string
      {"{,}", Verdict::Reject},
      {"{\"a\":}", Verdict::Reject},
      {"{\"a\"}", Verdict::Reject},
      {"{\"a\" 1}", Verdict::Reject},
      {"{\"a\":: 1}", Verdict::Reject},
      {"{1: 2}", Verdict::Reject},
      {"{true: 1}", Verdict::Reject},
      {"{null: 2}", Verdict::Reject},
      {"{[-a: 1}", Verdict::Reject},
      {"{\"a\":1,}", Verdict::Reject},
      {"{\"a\":1 \"b\":2}", Verdict::Reject},
      {"{\"\":[1,2]}", Verdict::Accept},
      {"{\"a\":1,\"b\":[1,{\"c\":\"d\"}}", Verdict::Reject},
      {"[1,2,]", Verdict::Reject},
      {"[[],[]]", Verdict::Accept},
      {"[]", Verdict::Accept},
      {"{}", Verdict::Accept},
      {"[[]]", Verdict::Accept},
      {"[[[]]]", Verdict::Accept},
      {"[1,[2],[3]]", Verdict::Accept},
      {"{\"a\":1,\"b\":[1,{\"c\":\"d\"}]}", Verdict::Accept},
      {"[{\"a\":[]},{}]", Verdict::Accept},
      {"{} ", Verdict::Accept},
      {"  [1, 2]  ", Verdict::Accept},
      {"\"a,b\"", Verdict::Accept},
      {"[\"a[b],{c}\"]", Verdict::Accept},
      {"[\"a[b,{c}\"]", Verdict::Accept},
      {"[\"\\\\\"]", Verdict::Accept},
      {"[\"x\\q\"]", Verdict::Reject},
      {"[\"\\u0041\"]", Verdict::Accept},
      {"[\"\\ud83d\\ude00\"]", Verdict::Accept},
      {"[\"\\ud800\"]", Verdict::Accept}, // lone surrogate: pass-through
      {"[\"\\x41\"]", Verdict::Reject},
      {"[1e400]", Verdict::Accept},      // overflow -> ±Infinity (V8)
      {"[-0,-0.0e+0,1.5E+30]", Verdict::Accept},
      {"{\"\":[1,2], \"b\":{}}", Verdict::Accept},
      {"{'single':1}", Verdict::Reject},
      {"[null,false,true,\"\"]", Verdict::Accept},
      {"discarded", Verdict::Reject},
      {" [[[[\"deep\"]]]} ", Verdict::Reject}, // cross-type closer depth 4
      {" [[[[\"deep\"]]]]] ", Verdict::Reject}, // unbalanced: 5 closers / 4 openers
      {" [[[[\"deep\"]]]] ", Verdict::Accept},  // balanced 4-deep
  };

  for (std::size_t i = 0; i < cases.size(); ++i) {
    compare_verdicts(cases[i], i);
  }
}

//! Event lists must grow from the root and stay usefully ordered on
//! valid documents — a sanity gate for the general walk machinery.
TEST(ReferenceParser, EventListInvariants) {
  const std::vector<FixtureCase> cases = {
      {"{}", Verdict::Accept},
      {"[1,2,3]", Verdict::Accept},
      {"{\"a\":1,\"b\":[1,{\"c\":\"d\"}]}", Verdict::Accept},
      {"[[[\"deep\"]]]", Verdict::Accept},  };
  for (const auto &item : cases) {
    const std::string body(item.text);
    std::vector<Event> events;
    EXPECT_EQ(on_demand_verdict(body, events), Verdict::Accept) << item.text;
    EXPECT_EQ(reference_verdict(body), Verdict::Accept) << item.text;
    EXPECT_GT(events.size(), std::size_t{0}) << item.text;
    // The root's kind must be the first classification discovered, at
    // the start of the buffer (these fixtures have no leading ws).
    const auto expected_root = zuu::ondemand::ref_value_kind(
        body.data(), static_cast<zuu::core::size_t>(body.size()), 0);
    EXPECT_EQ(events.front().kind, expected_root);
    EXPECT_EQ(events.front().ts, zuu::core::uint64_t{0});
    EXPECT_NO_FATAL_FAILURE(assert_events_in_order(events, item.text));
  }
}

//! Event COUNTS of small shapes vs a hand tally; keys are NOT events.
TEST(ReferenceParser, EventCountsManual) {
  {
    std::vector<Event> events;
    on_demand_verdict("[1,2,3]", events);
    EXPECT_EQ(events.size(), std::size_t{4}); // root + 3 elements
    EXPECT_EQ(events.front().kind, NodeType::Array);
    EXPECT_EQ(events[1].kind, NodeType::Number);
  }
  {
    std::vector<Event> events;
    on_demand_verdict("{\"a\":1}", events);
    EXPECT_EQ(events.size(), std::size_t{2}); // root + one VALUE (not keys)
    EXPECT_EQ(events.front().kind, NodeType::Object);
    EXPECT_EQ(events.back().kind, NodeType::Number);
  }
  {
    std::vector<Event> events;
    on_demand_verdict("\"lonely\"", events);
    EXPECT_EQ(events.size(), std::size_t{1});
    EXPECT_EQ(events.front().kind, NodeType::String);
  }
  {
    std::vector<Event> events;
    on_demand_verdict("{\"a\":{\"b\":null}}", events);
    EXPECT_EQ(events.size(), std::size_t{3}); // root + value obj + null
    EXPECT_EQ(events.back().kind, NodeType::Null);
    EXPECT_EQ(events[1].kind, NodeType::Object);
  }
}

// Scalar roots are legal JSON documents (fail1 parity): the walk events
// reflect the promotion, and the verdict pair agrees.
TEST(ReferenceParser, ScalarRootsAccepted) {
  const std::vector<FixtureCase> cases = {
      {"true", Verdict::Accept},
      {"false ", Verdict::Accept},
      {"null ", Verdict::Accept},
      {" 42 ", Verdict::Accept},  // scalar root: V8 accept
      {" \"s\" ", Verdict::Accept},
  };
  for (std::size_t i = 0; i < cases.size(); ++i) {
    compare_verdicts(cases[i], i);
  }
}

// Custom main: persist the export-helper sidecar (ZE_METADATA_OUT) when the
// run is instrumented, then execute the suite. Plain local runs are no-ops.
int main(int argc, char **argv) {
  zuu::testing::write_export_sidecar("ZE_METADATA_OUT", "ZE_RUN_ID",
                                     "ZE_GIT_COMMIT",
                                     "ze_json_reference_parser_test");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
