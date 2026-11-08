// Scanner throughput benchmarks (TASKS P1-5, P1-6).
//
// Rationale: measure the structural pre-scan stage over the deterministic
// benchmark corpora (corpus/benchmarks, P0-8 manifests) — reference_scan
// (plain scalar, the correctness anchor) versus scanner8 (table-driven fast
// path). Every fast-path change must be re-measured against its baseline
// record, one change per measurement, same machine + preset + corpus
// (cpp-highperf-json workflow; AGENTS.md section 6).
//
// @cite 10.14778_3115404.3115416 Mison: structural indexing pre-scan — the
//       stage under measurement.
// @cite 10.48550_arXiv.1902.08318 simdjson: staging emits the
//       next-structural-index stream; throughput of this stage bounds the
//       on-demand parser.
// @cite 10.1145_2807591.2807644 scientific benchmarking: repetitions with
//       in-situ comparisons guard against noise-driven conclusions.
#include <benchmark/benchmark.h>

#include <ze_json/ondemand/reference.hpp>
#include <ze_json/ondemand/scanner.hpp>

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
      continue; // manifests are provenance, not documents to scan
    }
    std::ifstream in(entry.path(), std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    docs.push_back({entry.path().filename().string(), data});
  }
  std::sort(docs.begin(), docs.end(), [](const Document &a, const Document &b) {
    return a.name < b.name;
  });
  return docs;
}

const std::vector<Document> &corpus_docs(const char *sub_dir) {
  static std::map<std::string, std::vector<Document>> cache;
  auto it = cache.find(sub_dir);
  if (it == cache.end()) {
    it = cache
             .emplace(sub_dir, load_corpus_dir(
                                   std::filesystem::path(ZE_JSON_BENCH_CORPUS_DIR) /
                                   sub_dir))
             .first;
  }
  return it->second;
}

template <typename ScanFn>
void scan_document(benchmark::State &state, ScanFn scan, const Document *doc) {
  if (doc->data.empty()) {
    state.SkipWithError("corpus document missing (check ZE_JSON_BENCH_CORPUS_DIR)");
    return;
  }
  std::vector<size_t> dst(doc->data.size(), 0);

  size_t produced = 0;
  for (auto _ : state) {
    auto result = scan(doc->data.data(), doc->data.size(), dst.data(), dst.size());
    if (!result.has_value()) {
      state.SkipWithError("scanner rejected a corpus document");
      return;
    }
    benchmark::DoNotOptimize(produced = result.value());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(doc->data.size()));
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(produced));
}

template <typename ScanFn>
void scan_batch(benchmark::State &state, ScanFn scan,
                const std::vector<Document> *docs) {
  if (docs->empty()) {
    state.SkipWithError("corpus batch empty (check ZE_JSON_BENCH_CORPUS_DIR)");
    return;
  }
  size_t total_bytes = 0;
  size_t produced_per_iteration = 0;
  size_t max_document = 0;
  for (const Document &doc : *docs) {
    total_bytes += doc.data.size();
    max_document = std::max(max_document, doc.data.size());
  }
  std::vector<size_t> dst(max_document, 0);

  for (auto _ : state) {
    size_t produced = 0;
    for (const Document &doc : *docs) {
      auto result = scan(doc.data.data(), doc.data.size(), dst.data(), dst.size());
      if (!result.has_value()) {
        state.SkipWithError("scanner rejected a corpus batch member");
        return;
      }
      produced += result.value();
    }
    benchmark::DoNotOptimize(produced_per_iteration = produced);
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(total_bytes));
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(produced_per_iteration));
}

void bench_reference_document(benchmark::State &state, const Document *doc) {
  scan_document(state, &zuu::ondemand::reference_scan, doc);
}

void bench_scanner8_document(benchmark::State &state, const Document *doc) {
  scan_document(state, &zuu::ondemand::scanner8, doc);
}

void bench_reference_batch(benchmark::State &state,
                           const std::vector<Document> *docs) {
  scan_batch(state, &zuu::ondemand::reference_scan, docs);
}

void bench_scanner8_batch(benchmark::State &state,
                          const std::vector<Document> *docs) {
  scan_batch(state, &zuu::ondemand::scanner8, docs);
}

void register_corpus(const char *sub_dir, const char *label) {
  const std::vector<Document> &docs = corpus_docs(sub_dir);
  if (docs.empty()) {
    return; // corpus not present: registration skipped, runs explain why
  }
  const std::string base = std::string(label);
  if (docs.size() == 1) {
    const Document *doc = &docs.front();
    benchmark::RegisterBenchmark(
        ("BM_ReferenceScan/" + base).c_str(), bench_reference_document, doc)
        ->Repetitions(5)
        ->Unit(benchmark::kMicrosecond);
    benchmark::RegisterBenchmark(("BM_Scanner8/" + base).c_str(),
                                 bench_scanner8_document, doc)
        ->Repetitions(5)
        ->Unit(benchmark::kMicrosecond);
    return;
  }
  const std::vector<Document> *docs_ptr = &docs;
  benchmark::RegisterBenchmark(("BM_ReferenceScan/" + base).c_str(),
                               bench_reference_batch, docs_ptr)
      ->Repetitions(5)
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark(("BM_Scanner8/" + base).c_str(),
                               bench_scanner8_batch, docs_ptr)
      ->Repetitions(5)
      ->Unit(benchmark::kMicrosecond);
}

} // namespace

int main(int argc, char **argv) {
  // Export helper contract (AGENTS.md section 4): persist the sidecar before
  // anything else, then hand control to google benchmark.
  zuu::testing::write_export_sidecar("METADATA_OUT", "RUN_ID", "GIT_COMMIT",
                                     "ze_json_scanner_benchmark");

  benchmark::Initialize(&argc, argv);

  // Size ladder (single documents, byte-exact per P0-8 manifests).
  register_corpus("synthetic_1k", "synthetic_1k");
  register_corpus("synthetic_10k", "synthetic_10k");
  register_corpus("synthetic_100k", "synthetic_100k");
  register_corpus("synthetic_1mb", "synthetic_1mb");
  // SAQ store-shaped workload (one benchmark per store file).
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
