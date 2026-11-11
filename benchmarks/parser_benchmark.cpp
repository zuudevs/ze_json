// On-demand parser component benchmarks (TASKS P7-7 — the P4-3/P4-4
// baselines).
//
// Rationale: measure the parser stack level-by-level over the deterministic
// benchmark corpora (corpus/benchmarks, P0-8 manifests) so every later
// optimization (P4-3 lookups, P4-4 PGO) re-measures against THESE records:
//   BM_ParseReference          — ref_parse_document: the scalar full-walk
//                                byte reader's verdict + consumed bytes
//                                (P7-6 anchor; the grammar floor).
//   BM_ParseOpen               — Document::open: the structural scan + index
//                                + match-table staging into the arena; the
//                                staged representation the lazy layer runs on.
//   BM_ParseOnDemandFullWalk   — open + get_root + visit EVERY value and
//                                promote every scalar through the real
//                                accessors (get_string_span/get_double/
//                                get_bool/is_null); the full-walk verdict
//                                path the P7-6 differential gates.
//   BM_ParseOnDemandWalkOnly   — open + get_root + visit EVERY value but
//                                consume it via node classification only
//                                (no scalar promotion); the R5 phase-split
//                                counterpart of the harness wasm-walk-full
//                                context (open / walk / materialize).
// One change per measurement, in-situ comparison only (same machine, preset,
// corpus); no cross-machine record: AGENTS.md sections 4 & 6.
//
// @cite 10.48550_arXiv.1902.08318 simdjson: staged parsing — throughput of
//       the staging stage bounds the on-demand parser on top of it.
// @cite 10.14778_3115404.3115416 Mison: the structural index is the document
//       representation; parsing cost then lives in the scans.
// @cite 10.1002_spe.2984 number parsing at a gigabyte per second: the number
//       promotion cost carried by the full-walk measurement.
// @cite 10.1002_spe.3313 on-demand JSON: BM_ParseOpen/BM_ParseOnDemandFullWalk
//       measure the lazy layer whose component costs this paper defines.
// @cite 10.1145_2807591.2807644 scientific benchmarking: repetitions with
//       in-situ comparisons guard against noise-driven conclusions.
#include <benchmark/benchmark.h>

#include <ze_json/ondemand/arena.hpp>
#include <ze_json/ondemand/document.hpp>
#include <ze_json/ondemand/reference_parser.hpp>
#include <ze_json/ondemand/value.hpp>

#include "export_sidecar.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {

using zuu::core::size_t;

struct Document {
  std::string name;
  std::string data;
};

std::vector<Document> load_corpus_dir(const std::filesystem::path &dir) {
  std::vector<Document> docs;
  if (!std::filesystem::exists(dir)) {
    return docs;
  }
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    if (entry.path().filename() == "MANIFEST.json") {
      continue; // manifests are provenance, not documents to parse
    }
    std::ifstream in(entry.path(), std::ios::binary);
    // Braces: the paren form is the classic most-vexing-parse declaration.
    const std::string data{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    docs.push_back({entry.path().filename().string(), data});
  }
  std::sort(docs.begin(), docs.end(), [](const Document &a, const Document &b) {
    return a.name < b.name;
  });
  return docs;
}

// Static cache: registered benchmark functions borrow these vectors, so
// the vectors must outlive every registration + run (same contract as the
// scanner benchmark's loader).
const std::vector<Document> &corpus_docs(const char *sub_dir) {
  static std::map<std::string, std::vector<Document>> cache;
  auto it = cache.find(sub_dir);
  if (it == cache.end()) {
    it = cache
             .emplace(sub_dir,
                      load_corpus_dir(std::filesystem::path(
                                          ZE_JSON_BENCH_CORPUS_DIR) /
                                      sub_dir))
             .first;
  }
  return it->second;
}

//! Visit EVERY value and promote every scalar through the REAL on-demand
//! accessors — the same promotion set the P7-6 differential test walks
//! (strings validate via get_string_span, numbers round via get_double,
//! literals via get_bool/is_null). Returns false on any framing/grammar
//! rejection (corpus documents are all valid; rejection is a bug).
bool walk_value(const zuu::ondemand::Value &value, size_t &out_values) {
  switch (value.type()) {
  case zuu::ondemand::NodeType::Object: {
    out_values += 1;
    const auto begin = value.begin_object();
    if (!begin) {
      return false;
    }
    auto iter = begin.value();
    while (!iter.done_) {
      if (iter.failed_ != zuu::core::JsonErrc::None) {
        return false;
      }
      const auto field = *iter;
      if (!field) {
        return false; // key grammar / ':' framing surfaces here
      }
      if (!walk_value(field.value().value, out_values)) {
        return false;
      }
      ++iter;
    }
    return iter.failed_ == zuu::core::JsonErrc::None;
  }
  case zuu::ondemand::NodeType::Array: {
    out_values += 1;
    const auto begin = value.begin_array();
    if (!begin) {
      return false;
    }
    auto iter = begin.value();
    while (!iter.done_) {
      if (iter.failed_ != zuu::core::JsonErrc::None) {
        return false;
      }
      const auto element = *iter;
      if (!element) {
        return false;
      }
      if (!walk_value(element.value(), out_values)) {
        return false;
      }
      ++iter;
    }
    return iter.failed_ == zuu::core::JsonErrc::None;
  }
  case zuu::ondemand::NodeType::String:
    out_values += 1;
    return value.get_string_span().has_value();
  case zuu::ondemand::NodeType::Number:
    out_values += 1;
    return value.get_double().has_value();
  case zuu::ondemand::NodeType::BoolTrue:
  case zuu::ondemand::NodeType::BoolFalse:
    out_values += 1;
    return value.get_bool().has_value();
  case zuu::ondemand::NodeType::Null:
    out_values += 1;
    return value.is_null();
  case zuu::ondemand::NodeType::Unknown:
  default:
    return false;
  }
}

//! Level 1: the scalar full-walk reader (no index, no generated tables).
void reference_parse_batch(benchmark::State &state,
                           const std::vector<Document> *docs) {
  if (docs->empty()) {
    state.SkipWithError("corpus batch empty (check ZE_JSON_BENCH_CORPUS_DIR)");
    return;
  }
  size_t total_bytes = 0;
  for (const Document &doc : *docs) {
    total_bytes += doc.data.size();
  }

  for (auto _ : state) {
    size_t consumed = 0;
    for (const Document &doc : *docs) {
      const auto result = zuu::ondemand::ref_parse_document(
          doc.data.data(), static_cast<size_t>(doc.data.size()));
      if (!result.has_value()) {
        state.SkipWithError("reference parser rejected a corpus document");
        return;
      }
      consumed += result.value();
    }
    benchmark::DoNotOptimize(consumed);
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(total_bytes));
}

//! Level 2: Document::open only — staging (scan + index + match + pairing).
void ondemand_open_batch(benchmark::State &state,
                         const std::vector<Document> *docs) {
  if (docs->empty()) {
    state.SkipWithError("corpus batch empty (check ZE_JSON_BENCH_CORPUS_DIR)");
    return;
  }
  size_t max_document = 0;
  size_t total_bytes = 0;
  for (const Document &doc : *docs) {
    max_document = std::max(max_document, doc.data.size());
    total_bytes += doc.data.size();
  }
  // Upper bound per document.hpp: three arena blocks of at most
  // src_size * sizeof(size_t) entries (index, match, pairing stack).
  std::vector<size_t> arena(max_document * 3 + 64);
  zuu::ondemand::Arena backing(arena.data(), arena.size() * sizeof(size_t));

  for (auto _ : state) {
    size_t opened = 0;
    for (const Document &doc : *docs) {
      backing.reset(); // rewind the bump offset; buffers stay hot
      const auto result = zuu::ondemand::Document::open(
          backing, doc.data.data(), static_cast<size_t>(doc.data.size()));
      if (!result.has_value()) {
        state.SkipWithError("Document::open rejected a corpus document");
        return;
      }
      benchmark::DoNotOptimize(result.value().index_count());
      opened += 1;
    }
    benchmark::DoNotOptimize(opened);
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(total_bytes));
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(docs->size()));
}

//! Structural walk WITHOUT scalar promotion — visit every value, consume it
//! through the node classification only (type() reads the staged match
//! table; no get_string_span / get_double / get_bool). The native
//! counterpart of the node harness `wasm-walk-full` context (R5 phase
//! split: open / walk / full materialization — AGENTS.md measurement
//! phases). Returns false on framing/grammar rejection; the count
//! surfaces the visited-value sink.
//!
//! @cite 10.1002_spe.3313 on-demand JSON: structural consumption without
//!       materializing the value tree is the phase this walk isolates.
bool walk_value_structural(const zuu::ondemand::Value &value, size_t &out_values) {
  switch (value.type()) {
  case zuu::ondemand::NodeType::Object: {
    out_values += 1;
    const auto begin = value.begin_object();
    if (!begin) {
      return false;
    }
    auto iter = begin.value();
    while (!iter.done_) {
      if (iter.failed_ != zuu::core::JsonErrc::None) {
        return false;
      }
      const auto field = *iter;
      if (!field) {
        return false; // key grammar / ':' framing surfaces here
      }
      if (!walk_value_structural(field.value().value, out_values)) {
        return false;
      }
      ++iter;
    }
    return iter.failed_ == zuu::core::JsonErrc::None;
  }
  case zuu::ondemand::NodeType::Array: {
    out_values += 1;
    const auto begin = value.begin_array();
    if (!begin) {
      return false;
    }
    auto iter = begin.value();
    while (!iter.done_) {
      if (iter.failed_ != zuu::core::JsonErrc::None) {
        return false;
      }
      const auto element = *iter;
      if (!element) {
        return false;
      }
      if (!walk_value_structural(element.value(), out_values)) {
        return false;
      }
      ++iter;
    }
    return iter.failed_ == zuu::core::JsonErrc::None;
  }
  case zuu::ondemand::NodeType::String:
  case zuu::ondemand::NodeType::Number:
  case zuu::ondemand::NodeType::BoolTrue:
  case zuu::ondemand::NodeType::BoolFalse:
  case zuu::ondemand::NodeType::Null:
    out_values += 1;
    return true; // the classification above was the consumption
  case zuu::ondemand::NodeType::Unknown:
  default:
    return false;
  }
}

//! Level 2 (R5 phase split): staging PLUS structural walk only — every
//! value consumed via classification, NO scalar promotion. Against
//! BM_ParseOpen it isolates the traversal cost; against
//! BM_ParseOnDemandFullWalk the promotion cost.
void ondemand_walkonly_batch(benchmark::State &state,
                             const std::vector<Document> *docs) {
  if (docs->empty()) {
    state.SkipWithError("corpus batch empty (check ZE_JSON_BENCH_CORPUS_DIR)");
    return;
  }
  size_t max_document = 0;
  size_t total_bytes = 0;
  for (const Document &doc : *docs) {
    max_document = std::max(max_document, doc.data.size());
    total_bytes += doc.data.size();
  }
  std::vector<size_t> arena(max_document * 3 + 64);
  zuu::ondemand::Arena backing(arena.data(), arena.size() * sizeof(size_t));

  for (auto _ : state) {
    size_t values = 0;
    for (const Document &doc : *docs) {
      backing.reset();
      const auto opened = zuu::ondemand::Document::open(
          backing, doc.data.data(), static_cast<size_t>(doc.data.size()));
      if (!opened.has_value()) {
        state.SkipWithError("Document::open rejected a corpus document");
        return;
      }
      const auto root = opened.value().get_root();
      if (!root.has_value() || !walk_value_structural(root.value(), values)) {
        state.SkipWithError("structural walk rejected a corpus document");
        return;
      }
    }
    benchmark::DoNotOptimize(values);
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(total_bytes));
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(docs->size()));
}

//! Level 3: full on-demand walk — staging PLUS visiting every value and
//! promoting every scalar (the workflow of the correctness gates).
void ondemand_fullwalk_batch(benchmark::State &state,
                             const std::vector<Document> *docs) {
  if (docs->empty()) {
    state.SkipWithError("corpus batch empty (check ZE_JSON_BENCH_CORPUS_DIR)");
    return;
  }
  size_t max_document = 0;
  size_t total_bytes = 0;
  for (const Document &doc : *docs) {
    max_document = std::max(max_document, doc.data.size());
    total_bytes += doc.data.size();
  }
  std::vector<size_t> arena(max_document * 3 + 64);
  zuu::ondemand::Arena backing(arena.data(), arena.size() * sizeof(size_t));

  for (auto _ : state) {
    size_t values = 0;
    for (const Document &doc : *docs) {
      backing.reset();
      const auto opened = zuu::ondemand::Document::open(
          backing, doc.data.data(), static_cast<size_t>(doc.data.size()));
      if (!opened.has_value()) {
        state.SkipWithError("Document::open rejected a corpus document");
        return;
      }
      const auto root = opened.value().get_root();
      if (!root.has_value() || !walk_value(root.value(), values)) {
        state.SkipWithError("full walk rejected a corpus document");
        return;
      }
    }
    benchmark::DoNotOptimize(values);
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(total_bytes));
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(docs->size()));
}

void register_corpus(const char *sub_dir, const char *label) {
  const std::vector<Document> &docs = corpus_docs(sub_dir);
  if (docs.empty()) {
    return; // corpus not present: registration skipped, runs explain why
  }
  const std::vector<Document> *docs_ptr = &docs;
  benchmark::RegisterBenchmark(
      (std::string("BM_ParseReference/") + label).c_str(),
      reference_parse_batch, docs_ptr)
      ->Repetitions(5)
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark(
      (std::string("BM_ParseOpen/") + label).c_str(), ondemand_open_batch,
      docs_ptr)
      ->Repetitions(5)
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark(
      (std::string("BM_ParseOnDemandWalkOnly/") + label).c_str(),
      ondemand_walkonly_batch, docs_ptr)
      ->Repetitions(5)
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark(
      (std::string("BM_ParseOnDemandFullWalk/") + label).c_str(),
      ondemand_fullwalk_batch, docs_ptr)
      ->Repetitions(5)
      ->Unit(benchmark::kMicrosecond);
}

} // namespace

int main(int argc, char **argv) {
  // Export helper contract (AGENTS.md section 4): persist the sidecar before
  // anything else, then hand control to google benchmark.
  zuu::testing::write_export_sidecar("METADATA_OUT", "RUN_ID", "GIT_COMMIT",
                                     "ze_json_parser_benchmark");

  benchmark::Initialize(&argc, argv);

  // Size ladder (single documents, byte-exact per P0-8 manifests).
  register_corpus("synthetic_1k", "synthetic_1k");
  register_corpus("synthetic_10k", "synthetic_10k");
  register_corpus("synthetic_100k", "synthetic_100k");
  register_corpus("synthetic_1mb", "synthetic_1mb");
  // SAQ store-shaped workload (one batch across the store files).
  register_corpus("saq_payload_v1", "saq");
  // Whole-document batch stress (many small valid documents).
  register_corpus("checker_batch", "checker_batch");

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
