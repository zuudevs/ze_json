// Value/Document on-demand API tests (TASKS P7-4).
//
// Rationale: the on-demand object graph must reproduce V8 JSON.parse
// VERDICTS at the framing level (what documents are accepted/rejected and
// which access path surfaces the error) while only materializing tokens
// the program touches (paper contract) — before any performance claim is
// admissible (cpp-highperf-json rule 4; AGENTS.md section 6). Gates:
//   - root framing (empty, scalar + whitespace tail, container ownership);
//   - launcher-table classification (one kOndemandNodeType load per value);
//   - scalar materialization equals direct P7-2/P7-3 scans (get_double /
//     get_string_span + unescape, get_bool, is_null);
//   - array/object framing rejection cases the baseline reject
//     ("[1 2]", trailing commas, missing ':' / value, stray structural);
//   - skip latitude: container children are framed by index only (contents
//     of SKIPPED subtrees stay unvalidated by design, 10.1002_spe.3313);
//   - count_elements agreement with the iteration framing, incl. deep
//     nesting without recursion (5000 levels, iterative index walk).
//
// @cite 10.1002_spe.3313 on-demand JSON (Keiser, Lemire) — iterator-shape
//       and unvisited-subtree latitude this file pins down.
// @cite 10.14778_3115404.3115416 Mison — structural-index-only traversal.
#include <gtest/gtest.h>

#include <ze_json/ondemand/document.hpp>
#include <ze_json/ondemand/number.hpp>
#include <ze_json/ondemand/string.hpp>
#include <ze_json/ondemand/value.hpp>

#include "export_sidecar.hpp" // export-helper sidecar contract (AGENTS.md section 4)

#include <cstdint>
#include <string>
#include <vector>

namespace {

using zuu::ondemand::Arena;
using zuu::ondemand::Document;
using zuu::ondemand::Field;
using zuu::ondemand::NodeType;
using zuu::ondemand::parse_number;
using zuu::ondemand::StringSpan;
using zuu::ondemand::unescape_string;

/** Document + arena bundle (borrowed-buffer lifetime contract honored). */
struct TestDoc {
  std::string json;
  std::vector<std::uint8_t> arena_buf;
  Arena arena;
  Document doc;

  explicit TestDoc(const char *src)
      : json(src), arena_buf(json.size() * sizeof(std::size_t) * 3 + 64),
        arena(arena_buf.data(), arena_buf.size()) {
    auto opened = Document::open(arena, json.data(), json.size());
    if (opened.has_value()) {
      doc = std::move(opened.value());
    }
  }

  [[nodiscard]] bool ok() const { return doc.opened(); }
};

/** Open + assert success, returning the test bundle. */
[[nodiscard]] TestDoc must_open(const char *json) {
  TestDoc t(json);
  EXPECT_TRUE(t.ok());
  return t;
}

/** Decode a string span through the P7-3 layer into an owned std::string. */
[[nodiscard]] std::string decode(const StringSpan &span) {
  std::vector<char> buf(span.total_len + 1);
  const auto &decoded =
      unescape_string(span.start, span.total_len, buf.data(), buf.size());
  EXPECT_TRUE(decoded.has_value());
  return std::string(buf.data(), decoded.value());
}

// === Root framing =========================================================

TEST(ValueDocument, RootScalarsMaterialize) {
  {
    const auto &t = must_open("5");
    const auto &root = t.doc.get_root();
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(root.value().type(), NodeType::Number);
    EXPECT_EQ(root.value().get_double().value(), 5.0);
  }
  {
    const auto &t = must_open("  true\n");
    const auto &root = t.doc.get_root();
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(root.value().type(), NodeType::BoolTrue);
    EXPECT_TRUE(root.value().get_bool().value());
  }
  {
    const auto &t = must_open("\"hi\"");
    const auto &root = t.doc.get_root();
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(root.value().type(), NodeType::String);
    const auto &span = root.value().get_string_span();
    ASSERT_TRUE(span.has_value());
    EXPECT_FALSE(span.value().escaped);
    EXPECT_EQ(decode(span.value()), "hi");
  }
  {
    const auto &t = must_open("null");
    const auto &root = t.doc.get_root();
    ASSERT_TRUE(root.has_value());
    EXPECT_TRUE(root.value().is_null());
  }
}

TEST(ValueDocument, RootRejectionsMatchBaseline) {
  // Empty / whitespace-only (open is structural-only: an EMPTY document
  // binds fine and surfaces EmptyJson at get_root — the scanner contract).
  {
    TestDoc t("");
    EXPECT_TRUE(t.ok());
    const auto &root = t.doc.get_root();
    EXPECT_TRUE(root.has_error());
    EXPECT_EQ(root.error(), zuu::core::JsonErrc::EmptyJson);
  }
  {
    const auto &t = must_open("  \t\n");
    const auto &root = t.doc.get_root();
    EXPECT_TRUE(root.has_error());
    EXPECT_EQ(root.error(), zuu::core::JsonErrc::EmptyJson);
  }
  // Scalar + trailing garbage ("5 5", "5,", "true x").
  for (const char *src : {"5 5", "5,", "true x", "\"a\" 4", "-0 1", "1e3\r,"}) {
    const auto &root = must_open(src).doc.get_root();
    EXPECT_TRUE(root.has_error()) << src;
  }
  // Broken scalar bodies at the root (grammar layer or scanner rejects —
  // the unterminated '""' is caught by the scanner itself at open).
  for (const char *src : {"tru", "nul", "fals", "\"", "{\"\"", "-"}) {
    TestDoc t(src);
    const auto &root = t.doc.get_root();
    EXPECT_TRUE(root.has_error()) << src;
  }
  // Two documents are not one document; trailing structural or garbage.
  for (const char *src : {"[] []", "{} {}", "{} abc", "[] x", "{} ,"}) {
    const auto &root = must_open(src).doc.get_root();
    EXPECT_TRUE(root.has_error()) << src;
  }
  // Launcher bytes that cannot start a value.
  for (const char *src : {"]", "}", ":", ",x", "+5", ".5"}) {
    const auto &root = must_open(src).doc.get_root();
    EXPECT_TRUE(root.has_error()) << src;
  }
}

TEST(ValueDocument, RootContainerOwnership) {
  {
    const auto &t = must_open("[]");
    const auto &root = t.doc.get_root();
    ASSERT_TRUE(root.has_value()) << "[case-empty]";
    EXPECT_EQ(root.value().type(), NodeType::Array);
    EXPECT_EQ(root.value().count_elements().value(), 0U)
        << "[case-empty-count]";
  }
  {
    const auto &t = must_open("[1,2,3]");
    const auto &root = t.doc.get_root();
    ASSERT_TRUE(root.has_value()) << "[case-arr]";
    EXPECT_EQ(root.value().count_elements().value(), 3U) << "[case-arr-count]";
  }
  {
    const auto &t = must_open("{ \"k\": [true, null], \"j\": 2 }");
    const auto &root = t.doc.get_root();
    ASSERT_TRUE(root.has_value()) << "[case-obj]";
    EXPECT_EQ(root.value().type(), NodeType::Object);
    EXPECT_EQ(root.value().count_elements().value(), 2U) << "[case-obj-count]";
  }
}

// === Launcher classification =============================================

TEST(ValueDocument, LauncherTableClassification) {
  struct Case {
    const char *json;
    NodeType expected;
  };
  const Case cases[] = {
      {"[1]", NodeType::Array},       {"{\"a\": 1}", NodeType::Object},
      {"1", NodeType::Number},        {"-3", NodeType::Number},
      {"\"s\"", NodeType::String},    {"true", NodeType::BoolTrue},
      {"false", NodeType::BoolFalse}, {"null", NodeType::Null},
  };
  for (const auto &kase : cases) {
    const auto &t = must_open(kase.json);
    const auto &root = t.doc.get_root();
    ASSERT_TRUE(root.has_value()) << kase.json;
    EXPECT_EQ(root.value().type(), kase.expected) << kase.json;
  }
}

// === Container framing rejections (V8 rejects these documents) ===========

TEST(ValueDocument, ArrayFramingRejections) {
  for (const char *src :
       {"[,]", "[1,]", "[1 2]", "[\"a\" \"b\"]", "[1 ,  , 2]", "[1, 2",
        "[1:", "[,1]", "[\"x\":1]", "[1e]", "[01]", "[true false]"}) {
    const auto &t = must_open(src);
    ASSERT_TRUE(t.ok()) << "scanner accepted " << src;
    const auto &root = t.doc.get_root();
    // The rejection may surface at root framing (truncation) or at the
    // advanced walk — either way the document reads back as BROKEN.
    const bool framed =
        root.has_value() && root.value().count_elements().has_value();
    EXPECT_FALSE(framed) << "framing accepted " << src;
  }
}

TEST(ValueDocument, ObjectFramingRejections) {
  for (const char *src : {"{\"a\" 1}", "{\"a\": 5,}", "{\"a\": }", "{\"a\"}",
                          "{,}", "{\"a\": 1 2}", "{\"a\":: 1}", "{1: 2}"}) {
    const auto &t = must_open(src);
    ASSERT_TRUE(t.ok()) << "scanner accepted " << src;
    const auto &root = t.doc.get_root();
    const bool framed =
        root.has_value() && root.value().count_elements().has_value();
    EXPECT_FALSE(framed) << "framing accepted " << src;
  }
}

// === Iteration framing: arrays ===========================================

TEST(ValueDocument, ArrayIterationAndTypes) {
  const auto &t = must_open(R"([1, "two", [true], {"k": null}, false, null])");
  const auto &root = t.doc.get_root();
  ASSERT_TRUE(root.has_value());
  const auto &begin = root.value().begin_array();
  ASSERT_TRUE(begin.has_value());
  auto it = begin.value();
  int index = 0;
  for (; it.operator bool(); ++it, ++index) {
    const auto &value = *it;
    ASSERT_TRUE(value.has_value()) << index;
    switch (index) {
    case 0:
      EXPECT_EQ(value.value().type(), NodeType::Number);
      EXPECT_EQ(value.value().get_double().value(), 1.0);
      break;
    case 1:
      EXPECT_EQ(value.value().type(), NodeType::String);
      EXPECT_EQ(decode(value.value().get_string_span().value()), "two");
      break;
    case 2: {
      EXPECT_EQ(value.value().type(), NodeType::Array);
      const auto &nest = value.value().begin_array();
      ASSERT_TRUE(nest.has_value());
      auto nit = nest.value();
      ++nit;
      EXPECT_EQ(nit.failure(), zuu::core::JsonErrc::None);
      EXPECT_TRUE(nit.done_);
      break;
    }
    case 3:
      EXPECT_EQ(value.value().type(), NodeType::Object);
      break;
    case 4:
      EXPECT_FALSE(value.value().get_bool().value());
      break;
    case 5:
      EXPECT_TRUE(value.value().is_null());
      break;
    default:
      FAIL() << "too many elements";
    }
  }
  EXPECT_EQ(index, 6);
}

TEST(ValueDocument, SkippedSubtreeStaysUnvalidated) {
  // Outer walk frames the child container by INDEX ONLY — the broken
  // inner framing "1 2" is NEVER surfaced unless the child is entered.
  const auto &t = must_open("[[1 2]]");
  const auto &root = t.doc.get_root();
  ASSERT_TRUE(root.has_value());
  const auto &begin = root.value().begin_array();
  ASSERT_TRUE(begin.has_value());
  auto it = begin.value();
  ++it; // container child skipped
  EXPECT_EQ(it.failure(), zuu::core::JsonErrc::None);
  EXPECT_TRUE(it.done_);
  // Materializing the SAME child surfaces the framing error at ++.
  const auto &entered = root.value().begin_array();
  ASSERT_TRUE(entered.has_value());
  auto enter = entered.value();
  const auto &value = *enter;
  ASSERT_TRUE(value.has_value());
  const auto &inner = value.value().begin_array();
  ASSERT_TRUE(inner.has_value());
  auto iit = inner.value();
  ++iit; // child "1" framed, gap "2" non-whitespace -> violation
  EXPECT_NE(iit.failure(), zuu::core::JsonErrc::None);
}

TEST(ValueDocument, DeepSkipIsIterative) {
  // 5000-deep single chains must not recurse into the arena or the call
  // stack: every layer skips through the flat index (Mison model).
  std::string json;
  json.reserve(10001);
  for (int i = 0; i < 5000; ++i) {
    json.push_back('[');
  }
  for (int i = 0; i < 5000; ++i) {
    json.push_back(']');
  }
  const auto &t = must_open(json.c_str());
  const auto &root = t.doc.get_root();
  ASSERT_TRUE(root.has_value());
  EXPECT_EQ(root.value().count_elements().value(), 1U);
}

// === Iteration framing: objects ==========================================

TEST(ValueDocument, ObjectIterationAndCounts) {
  const auto &t = must_open(R"({"a": 1, "b": {"c": [true, null]}, "d": "x"})");
  const auto &root = t.doc.get_root();
  ASSERT_TRUE(root.has_value());
  const auto &begin = root.value().begin_object();
  ASSERT_TRUE(begin.has_value());
  auto it = begin.value();
  std::string keys;
  int index = 0;
  for (; it.operator bool(); ++it, ++index) {
    const auto &field = *it;
    ASSERT_TRUE(field.has_value()) << index;
    keys += decode(field.value().key);
    if (index == 1) {
      // "b" is a container: count its children without materializing.
      EXPECT_EQ(field.value().value.count_elements().value(), 1U);
    }
    if (index == 2) {
      EXPECT_EQ(decode(field.value().value.get_string_span().value()), "x");
    }
  }
  EXPECT_EQ(index, 3);
  EXPECT_EQ(keys, "abd");
}

TEST(ValueDocument, ScalarRootParityWithLayers) {
  // get_double must equal the P7-2 layer exactly on the same token bytes.
  const char *const samples[] = {"-0.5", "9007199254740993", "1e-400", "-3",
                                 "0.1"};
  for (const char *src : samples) {
    const auto &t = must_open(src);
    const auto &root = t.doc.get_root();
    ASSERT_TRUE(root.has_value()) << src;
    const auto &through_api = root.value().get_double();
    const auto &direct = parse_number(t.json.data(), t.json.size());
    ASSERT_TRUE(through_api.has_value()) << src;
    ASSERT_TRUE(direct.has_value()) << src;
    EXPECT_EQ(through_api.value(), direct.value()) << src;
  }
}

// === Detached values ======================================================

TEST(ValueDocument, DetachedValueContract) {
  const zuu::ondemand::Value detached;
  EXPECT_EQ(detached.type(), NodeType::Unknown);
  EXPECT_TRUE(detached.get_double().has_error());
  EXPECT_TRUE(detached.get_bool().has_error());
  EXPECT_TRUE(detached.get_string_span().has_error());
  EXPECT_FALSE(detached.is_null());
  EXPECT_TRUE(detached.count_elements().has_error());
  EXPECT_TRUE(detached.begin_array().has_error());
  EXPECT_TRUE(detached.begin_object().has_error());
}

TEST(ValueDocument, BeginRejectionsOnScalarValues) {
  const auto &t = must_open("[1]");
  const auto &root = t.doc.get_root();
  ASSERT_TRUE(root.has_value());
  const auto &element = root.value().begin_array();
  ASSERT_TRUE(element.has_value());
  auto it = element.value();
  const auto &v = *it;
  ASSERT_TRUE(v.has_value());
  EXPECT_TRUE(v.value().begin_array().has_error());
  EXPECT_TRUE(v.value().begin_object().has_error());
  EXPECT_EQ(v.value().count_elements().has_error(), true);
}

// === String materialization through zero-copy spans ======================

TEST(ValueDocument, StringSpansDecodeThroughP73) {
  const auto &t = must_open(R"(["A\n", "plain", "\ud83d\ude00", "\ud800"])");
  const auto &root = t.doc.get_root();
  ASSERT_TRUE(root.has_value());
  const auto &begin = root.value().begin_array();
  ASSERT_TRUE(begin.has_value());
  auto it = begin.value();

  const auto &plain = *it;
  ASSERT_TRUE(plain.has_value());
  const auto &p_span = plain.value().get_string_span();
  ASSERT_TRUE(p_span.has_value());
  EXPECT_TRUE(p_span.value().escaped); // "\n" IS an escape
  EXPECT_EQ(decode(p_span.value()), "A\n");
  ++it;

  const auto &raw = *it;
  ASSERT_TRUE(raw.has_value());
  const auto &r_span = raw.value().get_string_span();
  ASSERT_TRUE(r_span.has_value());
  EXPECT_FALSE(r_span.value().escaped);
  EXPECT_EQ(decode(r_span.value()), "plain");
  ++it;

  const auto &astral = *it;
  ASSERT_TRUE(astral.has_value());
  const auto &a_span = astral.value().get_string_span();
  ASSERT_TRUE(a_span.has_value());
  EXPECT_TRUE(a_span.value().escaped);
  EXPECT_EQ(decode(a_span.value()),
            std::string("\xF0\x9F\x98\x80", 4)); // U+1F600 pair -> 4 bytes
  ++it;

  const auto &lone = *it;
  ASSERT_TRUE(lone.has_value());
  const auto &l_span = lone.value().get_string_span();
  ASSERT_TRUE(l_span.has_value());
  EXPECT_TRUE(l_span.value().escaped);
  EXPECT_EQ(decode(l_span.value()), "\\ud800"); // verbatim pass-through
  ++it;
  EXPECT_TRUE(it.done_);
}

} // namespace

// Custom main: persist the export-helper sidecar (ZE_METADATA_OUT) when the
// run is instrumented, then execute the suite. Plain local runs are no-ops.
int main(int argc, char **argv) {
  zuu::testing::write_export_sidecar("ZE_METADATA_OUT", "ZE_RUN_ID",
                                     "ZE_GIT_COMMIT", "ze_json_value_test");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
