#include <benchmark/benchmark.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <print>
#include <ze_json/scanner/scanner.hpp>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace zuu;

const fs::path DATASET_DIR =
    fs::path(__FILE__).parent_path().parent_path().parent_path() / "dataset";

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
        /* Verifikasi awal di luar pengukuran benchmark. */                    \
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
MAKE_SCANNER_BENCHMARK(Scanner8, Literal, "literal.json", scanner::scanner8, 512)
MAKE_SCANNER_BENCHMARK(
    Scanner8, GithubEvents, "github_events.json", scanner::scanner8, 316 * 1024)
MAKE_SCANNER_BENCHMARK(
    Scanner8, UpdateCenter, "update-center.json", scanner::scanner8, 3214 * 1024)
MAKE_SCANNER_BENCHMARK(
    Scanner8, Jeopardy, "jeopardy.json", scanner::scanner8, 53 * 1024 * 1024)
MAKE_SCANNER_BENCHMARK(Scanner64, Tiny, "tiny.json", scanner::scanner64, 128)
MAKE_SCANNER_BENCHMARK(Scanner64, Literal, "literal.json", scanner::scanner64, 512)
MAKE_SCANNER_BENCHMARK(
    Scanner64, GithubEvents, "github_events.json", scanner::scanner64, 316 * 1024)
MAKE_SCANNER_BENCHMARK(
    Scanner64, UpdateCenter, "update-center.json", scanner::scanner64, 3214 * 1024)
MAKE_SCANNER_BENCHMARK(
    Scanner64, Jeopardy, "jeopardy.json", scanner::scanner64, 53 * 1024 * 1024)