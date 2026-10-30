#include <benchmark/benchmark.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <unordered_map>
#include <ze_json/scanner/scanner.hpp>

#include <ze_json/lookups/utf8_dfa.hpp>
#include <ze_json/utils/swar.hpp>

#include "replica_integrated.hpp"
#include "replica_span_prehybrid.hpp"

namespace fs = std::filesystem;
using namespace zuu;

const fs::path DATASET_DIR =
    fs::path(__FILE__).parent_path().parent_path().parent_path() / "samples" /
    "commons";

[[nodiscard]] bool
load_dataset(std::string& dst, const fs::path& path) {
    if (!fs::exists(path)) {
        std::println(stderr, "File does not exist: {}", path.string());
        return false;
    }

    auto file_size = fs::file_size(path);
    dst.resize(file_size);

    auto reader = std::ifstream(path, std::ios::binary);
    if (!reader.is_open()) {
        std::println(stderr, "Failed to open file: {}", path.string());
        return false;
    }

    reader.read(dst.data(), static_cast<std::streamsize>(file_size));
    if (!reader) {
        std::println(stderr, "Failed to read full file content.");
        return false;
    }

    std::println(
        "Successfully loaded {} ({} bytes)", path.string(), dst.length());
    return true;
}

const std::string&
get_dataset(const std::string& target_name) {
    static std::unordered_map<std::string, std::string> cache;

    if (!cache.contains(target_name)) {
        fs::path filepath = DATASET_DIR / target_name;
        if (!load_dataset(cache[target_name], filepath)) {
            std::println(stderr, "Failed to load dataset");
        }
    }
    return cache[target_name];
}

#define MAKE_SCANNER_BENCHMARK(NAME, TARGET, TARGET_PATH, FUNC, RESERVE_SIZE)  \
    static void BM_##NAME##_##TARGET(benchmark::State& state) {                \
        const std::string& json = get_dataset(TARGET_PATH);                    \
                                                                               \
        if (json.empty()) {                                                    \
            state.SkipWithError(                                               \
                "Dataset gagal dimuat, benchmark dihentikan.");                \
            return;                                                            \
        }                                                                      \
                                                                               \
        const size_t capacity =                                                \
            (RESERVE_SIZE) > json.size() ? (RESERVE_SIZE) : json.size();       \
                                                                               \
        std::vector<size_t> tape_idx(capacity);                                \
                                                                               \
        const auto validation = zuu::FUNC(                                     \
            json.data(), json.size(), tape_idx.data(), tape_idx.size());       \
                                                                               \
        if (!validation) {                                                     \
            state.SkipWithError("Scanner gagal pada verifikasi awal.");        \
            return;                                                            \
        }                                                                      \
                                                                               \
        for (auto _ : state) {                                                 \
            auto res = zuu::FUNC(                                              \
                json.data(), json.size(), tape_idx.data(), tape_idx.size());   \
                                                                               \
            benchmark::DoNotOptimize(res);                                     \
            benchmark::ClobberMemory();                                        \
        }                                                                      \
                                                                               \
        state.SetBytesProcessed(                                               \
            state.iterations() * static_cast<int64_t>(json.size()));           \
    }                                                                          \
    BENCHMARK(BM_##NAME##_##TARGET)                                            \
        ->Unit(benchmark::kNanosecond)                                         \
        ->MinTime(15)                                                          \
        ->MinWarmUpTime(5);

MAKE_SCANNER_BENCHMARK(Scanner8, Tiny, "tiny.json", scanner::scanner8, 128)
MAKE_SCANNER_BENCHMARK(
    Scanner8, Literal, "literal.json", scanner::scanner8, 512)
MAKE_SCANNER_BENCHMARK(
    Scanner8, GithubEvents, "github_events.json", scanner::scanner8, 316 * 1024)
MAKE_SCANNER_BENCHMARK(
    Scanner8, UpdateCenter, "update-center.json", scanner::scanner8,
    3214 * 1024)
MAKE_SCANNER_BENCHMARK(
    Scanner8, Jeopardy, "jeopardy.json", scanner::scanner8, 53 * 1024 * 1024)
MAKE_SCANNER_BENCHMARK(Scanner64, Tiny, "tiny.json", scanner::scanner64, 128)
MAKE_SCANNER_BENCHMARK(
    Scanner64, Literal, "literal.json", scanner::scanner64, 512)
MAKE_SCANNER_BENCHMARK(
    Scanner64, GithubEvents, "github_events.json", scanner::scanner64,
    316 * 1024)
MAKE_SCANNER_BENCHMARK(
    Scanner64, UpdateCenter, "update-center.json", scanner::scanner64,
    3214 * 1024)
MAKE_SCANNER_BENCHMARK(
    Scanner64, Jeopardy, "jeopardy.json", scanner::scanner64, 53 * 1024 * 1024)

// --- Pengganda scanner8 vs scanner64 dalam SATU proses ---------------------
// Dataset eksternal tidak tersedia lintas mesin; payload sintetis membuat
// perbandingan kedua scanner bisa berdampingan pada kondisi clock sama.
namespace {
using zuu::types::size_t;

std::string
make_synth_json(size_t target_bytes) {
    std::string json = "{\"widgets\":[";
    size_t index     = 0;
    while (json.size() < target_bytes) {
        json += R"({"index":)";
        json += std::to_string(index++);
        json += R"(,"name":"ZeJson","score":3.5},)";
    }
    json.pop_back(); // Buang koma terakhir sebelum ']'.
    json += "]}";
    return json;
}

void
BM_Scanner_Synth(benchmark::State& state, bool use_scanner64) {
    static const std::string json = make_synth_json(256 * 1024);
    std::vector<size_t> tape_idx(json.size());

    auto scan =
        use_scanner64 ? zuu::scanner::scanner64 : zuu::scanner::scanner8;
    auto validation =
        scan(json.data(), json.size(), tape_idx.data(), tape_idx.size());
    if (!validation) {
        state.SkipWithError("Scanner gagal pada verifikasi awal.");
        return;
    }

    for (auto _ : state) {
        auto res =
            scan(json.data(), json.size(), tape_idx.data(), tape_idx.size());
        benchmark::DoNotOptimize(res);
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<int64_t>(json.size()));
}

void
BM_Scanner8_Synth(benchmark::State& state) {
    BM_Scanner_Synth(state, false);
}

void
BM_Scanner64_Synth(benchmark::State& state) {
    BM_Scanner_Synth(state, true);
}
} // namespace

// MinTime Panjang: peredam noise antar-run (clock bisa bergetar 15-45%
// lintas proses) — raw name dipakai `--benchmark_filter=_Synth`.
BENCHMARK(BM_Scanner8_Synth)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(30)
    ->MinWarmUpTime(3);
BENCHMARK(BM_Scanner64_Synth)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(30)
    ->MinWarmUpTime(3);

// --- REPLIKA INTERLEAVED: pemisahan algoritma v0.1.5 -----------------------
// Adukan varian tersimpan di library MELAWAN replika algoritma lawas
// (Tahap 4) dalam SATU proses — satu-satunya perbandingan yang sahih
// di rig dengan jam lintas-proses bergetar 15-45%. Baca rasio
// library/replica: >1 berarti pemisahan mengembalikan throughput.
// Raw name dipakai `--benchmark_filter=_Replica`.

#define MAKE_REPLICA_BENCHMARK(NAME, TARGET, TARGET_PATH, FUNC)                \
    static void BM_##NAME##_##TARGET(benchmark::State& state) {                \
        const std::string& json = get_dataset(TARGET_PATH);                    \
        if (json.empty()) {                                                    \
            state.SkipWithError(                                               \
                "Dataset gagal dimuat, benchmark dihentikan.");                \
            return;                                                            \
        }                                                                      \
        std::vector<size_t> tape_idx(json.size());                             \
        const auto validation =                                                \
            FUNC(json.data(), json.size(), tape_idx.data(), tape_idx.size());  \
        if (!validation) {                                                     \
            state.SkipWithError("Replica gagal pada verifikasi awal.");        \
            return;                                                            \
        }                                                                      \
        for (auto _ : state) {                                                 \
            auto res = FUNC(                                                   \
                json.data(), json.size(), tape_idx.data(), tape_idx.size());   \
            benchmark::DoNotOptimize(res);                                     \
            benchmark::ClobberMemory();                                        \
        }                                                                      \
        state.SetBytesProcessed(                                               \
            state.iterations() * static_cast<int64_t>(json.size()));           \
    }                                                                          \
    BENCHMARK(BM_##NAME##_##TARGET)                                            \
        ->Unit(benchmark::kNanosecond)                                         \
        ->MinTime(15)                                                          \
        ->MinWarmUpTime(5);

#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1
// scanner8 terintegrasi (Tahap 4) — adu lawan library scanner8 pulih.
MAKE_REPLICA_BENCHMARK(
    Scanner8_IntegratedReplica, Synth, "jeopardy.json",
    scanner8_integrated_replica)
MAKE_REPLICA_BENCHMARK(
    Scanner8_IntegratedReplica, GithubEvents, "github_events.json",
    scanner8_integrated_replica)
MAKE_REPLICA_BENCHMARK(
    Scanner8_IntegratedReplica, Jeopardy, "jeopardy.json",
    scanner8_integrated_replica)

// scanner64 untyped-depth (pra-v0.1.5) — isolasi biaya kedalaman bertipe.
MAKE_REPLICA_BENCHMARK(
    Scanner64_UntypedReplica, Synth, "jeopardy.json", scanner64_untyped_replica)
MAKE_REPLICA_BENCHMARK(
    Scanner64_UntypedReplica, GithubEvents, "github_events.json",
    scanner64_untyped_replica)
MAKE_REPLICA_BENCHMARK(
    Scanner64_UntypedReplica, Jeopardy, "jeopardy.json",
    scanner64_untyped_replica)

// Eksperimen A1 Tahap-6: baseline span byte-per-byte pra-hybrid (v0.1.6)
// beku — pasangan in-run lawan library scanner64 yang kini probe-hybrid.
MAKE_REPLICA_BENCHMARK(
    Scanner64_SpanBaselineReplica, GithubEvents, "github_events.json",
    replica_span_prehybrid::scanner64_prehybrid_span_replica)
MAKE_REPLICA_BENCHMARK(
    Scanner64_SpanBaselineReplica, UpdateCenter, "update-center.json",
    replica_span_prehybrid::scanner64_prehybrid_span_replica)
MAKE_REPLICA_BENCHMARK(
    Scanner64_SpanBaselineReplica, Jeopardy, "jeopardy.json",
    replica_span_prehybrid::scanner64_prehybrid_span_replica)
#endif