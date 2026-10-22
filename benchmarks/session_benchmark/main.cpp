// ==========================================================================
//  KELOMPOK UJI SESSION — API publik C ABI end-to-end:
//  pipeline create (scan + Session), flow find+getter, traversal array,
//  dan path query (flat / nested / array / dalam / multi-kueri).
//  Path query memakai SATU Session lintas iterasi (find_path me-rewind
//  sendiri), flow per-langkah memakai create_session per-iterasi.
// ==========================================================================
#include <benchmark/benchmark.h>
#include <string>
#include <string_view>
#include <vector>

#include <ze_json/scanner/scanner.hpp>
#include <ze_json/ze_json.hpp>

namespace {
using zuu::types::int32_t;
using zuu::types::int64_t;
using zuu::types::size_t;

// Dokumen referensi: field skalar + object bersarang + array string +
// array of object — pola payload dunia nyata yang minimal.
constexpr char kSessionPayload[] = R"({
    "id": 1048576,
    "name": "ZeJson",
    "score": 3.141592653589793,
    "active": true,
    "position": {"x": 24, "y": 48},
    "colors": ["white", "black", "red"],
    "items": [{"n": 1}, {"n": 2}, {"n": 3}]
})";

constexpr size_t kPayloadSize = sizeof(kSessionPayload) - 1;
constexpr size_t kTapeEntries = 512;

} // namespace

class SessionFixture : public benchmark::Fixture {
  public:
    zuu::types::uint64_t tape[kTapeEntries]{};
    alignas(8) zuu::types::uint8_t arena_buffer[1024 * 1024];
    zuu::memory::Arena* arena{nullptr};

    // Session yang hidup lintas iterasi — dipakai kelompok PATH saja
    // (find_path me-rewind kursor sendiri setiap kueri).
    zuu::api::Session* shared{nullptr};

    void
    SetUp(::benchmark::State& state) override {
        arena = create_arena(arena_buffer, sizeof(arena_buffer));

        int32_t err = static_cast<int32_t>(zuu::api::INVALID_JSON);
        shared      = create_session(
            kSessionPayload, kPayloadSize, tape, kTapeEntries, arena, err);
        if (!shared || err != static_cast<int32_t>(zuu::api::OK)) {
            state.SkipWithError("Session shared gagal dibuat.");
        }
    }

    void
    TearDown(::benchmark::State& state) override {
        destroy_arena(arena);
    }
};

// --- Pipeline: scan + Session baru pada tiap iterasi -----------------------

BENCHMARK_DEFINE_F(SessionFixture, CreateSession_Pipeline)(
    benchmark::State& state) {
    for (auto _ : state) {
        reset_arena(arena);

        int32_t err   = static_cast<int32_t>(zuu::api::INVALID_JSON);
        auto* session = create_session(
            kSessionPayload, kPayloadSize, tape, kTapeEntries, arena, err);

        if (!session || err != static_cast<int32_t>(zuu::api::OK)) {
            state.SkipWithError("create_session gagal.");
            return;
        }
        benchmark::DoNotOptimize(session);
    }
}
BENCHMARK_REGISTER_F(SessionFixture, CreateSession_Pipeline)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

// --- Flow per-langkah: create + find + baca (biaya tambahan di atas
//     pipeline create) ----------------------------------------------------

BENCHMARK_DEFINE_F(SessionFixture, FindRead_SingleField)(
    benchmark::State& state) {
    for (auto _ : state) {
        reset_arena(arena);
        int32_t err   = static_cast<int32_t>(zuu::api::INVALID_JSON);
        auto* session = create_session(
            kSessionPayload, kPayloadSize, tape, kTapeEntries, arena, err);
        if (!session) {
            state.SkipWithError("create_session gagal.");
            return;
        }

        if (find_field(session, "score", 5) != 1) {
            state.SkipWithError("find_field tidak menemukan field.");
            return;
        }
        const double score = get_double(session);
        benchmark::DoNotOptimize(score);
    }
}
BENCHMARK_REGISTER_F(SessionFixture, FindRead_SingleField)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

// Flow 3 field skalar analog dengan pattern hot-loop JS (id/score/active).
BENCHMARK_DEFINE_F(SessionFixture, FindRead_ThreeFields)(
    benchmark::State& state) {
    for (auto _ : state) {
        reset_arena(arena);
        int32_t err   = static_cast<int32_t>(zuu::api::INVALID_JSON);
        auto* session = create_session(
            kSessionPayload, kPayloadSize, tape, kTapeEntries, arena, err);
        if (!session) {
            state.SkipWithError("create_session gagal.");
            return;
        }

        int64_t sum = 0;
        if (find_field(session, "id", 2) == 1) {
            sum ^= get_int(session);
        }
        if (find_field(session, "score", 5) == 1) {
            sum += static_cast<int64_t>(get_double(session) * 1e6);
        }
        if (find_field(session, "active", 6) == 1) {
            sum += get_bool(session);
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK_REGISTER_F(SessionFixture, FindRead_ThreeFields)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

// Traversal array literal: find + array_size + besar semua elemen.
BENCHMARK_DEFINE_F(SessionFixture, ArrayRead_AllElements)(
    benchmark::State& state) {
    for (auto _ : state) {
        reset_arena(arena);
        int32_t err   = static_cast<int32_t>(zuu::api::INVALID_JSON);
        auto* session = create_session(
            kSessionPayload, kPayloadSize, tape, kTapeEntries, arena, err);
        if (!session) {
            state.SkipWithError("create_session gagal.");
            return;
        }

        int64_t sum = 0;
        if (find_field(session, "colors", 6) != 1) {
            state.SkipWithError("find_field tidak menemukan array.");
            return;
        }
        const size_t count = array_size(session);
        while (array_next(session) == 1) {
            const char* data = nullptr;
            size_t len       = 0;
            get_string(session, data, len);
            sum += static_cast<int64_t>(len);
        }
        benchmark::DoNotOptimize(sum);
        benchmark::DoNotOptimize(count);
    }
}
BENCHMARK_REGISTER_F(SessionFixture, ArrayRead_AllElements)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

// --- Path query: SATU panggilan C ABI per kueri, lintas iterasi -----------

BENCHMARK_DEFINE_F(SessionFixture, PathQuery_Flat)(benchmark::State& state) {
    int32_t err = static_cast<int32_t>(zuu::api::OK);
    for (auto _ : state) {
        err              = static_cast<int32_t>(zuu::api::OK);
        const auto value = get_int_at(shared, "id", 2, err);
        if (err != static_cast<int32_t>(zuu::api::OK)) {
            state.SkipWithError("get_int_at gagal.");
            return;
        }
        benchmark::DoNotOptimize(value);
    }
}
BENCHMARK_REGISTER_F(SessionFixture, PathQuery_Flat)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

BENCHMARK_DEFINE_F(SessionFixture, PathQuery_Nested)(benchmark::State& state) {
    int32_t err = static_cast<int32_t>(zuu::api::OK);
    for (auto _ : state) {
        err              = static_cast<int32_t>(zuu::api::OK);
        const auto value = get_int_at(shared, "position.x", 10, err);
        if (err != static_cast<int32_t>(zuu::api::OK)) {
            state.SkipWithError("get_int_at gagal.");
            return;
        }
        benchmark::DoNotOptimize(value);
    }
}
BENCHMARK_REGISTER_F(SessionFixture, PathQuery_Nested)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

BENCHMARK_DEFINE_F(SessionFixture, PathQuery_ArrayElement)(
    benchmark::State& state) {
    int32_t err = static_cast<int32_t>(zuu::api::OK);
    for (auto _ : state) {
        err              = static_cast<int32_t>(zuu::api::OK);
        const char* data = nullptr;
        size_t len       = 0;
        get_string_at(shared, "colors[1]", 9, data, len, err);
        if (err != static_cast<int32_t>(zuu::api::OK)) {
            state.SkipWithError("get_string_at gagal.");
            return;
        }
        benchmark::DoNotOptimize(data);
        benchmark::DoNotOptimize(len);
    }
}
BENCHMARK_REGISTER_F(SessionFixture, PathQuery_ArrayElement)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

BENCHMARK_DEFINE_F(SessionFixture, PathQuery_Deep)(benchmark::State& state) {
    int32_t err = static_cast<int32_t>(zuu::api::OK);
    for (auto _ : state) {
        err = static_cast<int32_t>(zuu::api::OK);
        // Rantai: key → index array → key (items[1].n)
        const auto value = get_int_at(shared, "items[1].n", 10, err);
        if (err != static_cast<int32_t>(zuu::api::OK)) {
            state.SkipWithError("get_int_at gagal.");
            return;
        }
        benchmark::DoNotOptimize(value);
    }
}
BENCHMARK_REGISTER_F(SessionFixture, PathQuery_Deep)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

// 3 kueri path (int + double + bool) — analog FindRead_ThreeFields.
BENCHMARK_DEFINE_F(SessionFixture, PathQuery_ThreeTypes)(
    benchmark::State& state) {
    int32_t err = static_cast<int32_t>(zuu::api::OK);
    for (auto _ : state) {
        int64_t sum = 0;
        err         = static_cast<int32_t>(zuu::api::OK);
        sum ^= get_int_at(shared, "id", 2, err);

        err = static_cast<int32_t>(zuu::api::OK);
        sum += static_cast<int64_t>(get_double_at(shared, "score", 5, err));

        err = static_cast<int32_t>(zuu::api::OK);
        sum += get_bool_at(shared, "active", 6, err);

        if (err != static_cast<int32_t>(zuu::api::OK)) {
            state.SkipWithError("path query gagal.");
            return;
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK_REGISTER_F(SessionFixture, PathQuery_ThreeTypes)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

// --- Pipeline create_session pada payload sintetis multi-ukuran -----------

namespace {
using zuu::types::int32_t;
using zuu::types::size_t;
using zuu::types::uint64_t;
using zuu::types::uint8_t;

// Dokumen sintetis: root object berisi array panjang dengan item object
// 3 field skalar — meniru profil dokumen dunia nyata (twitter-like)
// tanpa bergantung pada dataset eksternal, deterministik penuh.
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

// Per ukuran payload: payload + tape + arena yang hidup lintas iterasi;
// reset_arena dipanggil per-iterasi seperti flow benchmark di atas.
struct SizedCase {
    std::string json;
    std::vector<zuu::types::uint64_t> tape;
    std::vector<zuu::types::uint8_t> arena_storage;
    zuu::memory::Arena* arena{nullptr};

    void
    resize_for(size_t target_bytes) {
        json = make_synth_json(target_bytes);
        // Alokasi tape longgar: setiap byte payload berpotensi jadi satu
        // entry (kasus terburuk seluruhnya tanda kutip).
        tape.assign(json.size(), 0);
        arena_storage.assign(4 * 1024 * 1024, 0);
        arena = create_arena(arena_storage.data(), arena_storage.size());
    }
};

// Sabuk ukuran payload (KiB) — kelipatan mengikuti tepi blok SWAR 4 KiB.
constexpr int kSizeCount = 5;
const size_t kSizesKib[kSizeCount] = {1, 16, 64, 256, 512};

SizedCase&
get_sized_case(int index) {
    // Static cache: dibangun lazy sekali, dipakai lintas iterasi.
    static SizedCase cases[kSizeCount];
    SizedCase& sized = cases[index];
    if (!sized.arena) {
        sized.resize_for(kSizesKib[index] * 1024);
    }
    return sized;
}

void
BM_CreateSession_Sizes(benchmark::State& state) {
    // Argumen = indeks ke kSizesKib.
    auto& sized = get_sized_case(static_cast<int>(state.range(0)));
    if (!sized.arena) {
        state.SkipWithError("arena sintetis gagal dibuat.");
        return;
    }

    // Validasi sekali di luar loop: wajib sukses agar pengukuran tidak
    // mengukur jalur error.
    {
        int32_t err   = static_cast<int32_t>(zuu::api::INVALID_JSON);
        auto* session = create_session(
            sized.json.data(), sized.json.size(), sized.tape.data(),
            sized.tape.size(), sized.arena, err);
        if (!session || err != static_cast<int32_t>(zuu::api::OK)) {
            state.SkipWithError(
                "payload sintetis tidak lolos create_session.");
            return;
        }
        reset_arena(sized.arena);
    }

    for (auto _ : state) {
        reset_arena(sized.arena);
        int32_t err   = static_cast<int32_t>(zuu::api::INVALID_JSON);
        auto* session = create_session(
            sized.json.data(), sized.json.size(), sized.tape.data(),
            sized.tape.size(), sized.arena, err);
        if (!session || err != static_cast<int32_t>(zuu::api::OK)) {
            state.SkipWithError("create_session gagal di tengah pengukuran.");
            return;
        }
        benchmark::DoNotOptimize(session);
    }
}
BENCHMARK(BM_CreateSession_Sizes)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(30)
    ->MinWarmUpTime(1)
    ->ArgName("KiB")
    ->Arg(0)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4);

} // namespace

// --- E2E create_session: jalur baru vs replikasi jalur lama, SATU proses ---
// Noise clock antar-proses bisa 15-45%; membandingkan kedua jalur berdampingan
// dalam proses + run sama membuat pengganda E2E bisa dipercaya.

namespace {
using zuu::types::int32_t;
using zuu::types::size_t;

void
run_pipeline(benchmark::State& state, bool legacy_scanner8) {
    auto& sized = get_sized_case(static_cast<int>(state.range(0)));
    if (!sized.arena) {
        state.SkipWithError("arena sintetis gagal dibuat.");
        return;
    }

    for (auto _ : state) {
        reset_arena(sized.arena);

        if (legacy_scanner8) {
            // Replikasi eksak create_session lama: scanner8 + alokasi
            // Session dari arena (lihat src/ze_json.cpp sebelum switch).
            auto scan_res = zuu::scanner::scanner8(
                sized.json.data(), sized.json.size(),
                reinterpret_cast<zuu::types::size_t*>(sized.tape.data()),
                sized.tape.size());
            if (!scan_res) {
                state.SkipWithError("scanner8 gagal.");
                return;
            }
            auto* session = sized.arena->make<zuu::api::Session>(
                sized.json.data(),
                static_cast<const zuu::types::uint64_t*>(sized.tape.data()),
                scan_res.value(), sized.arena);
            if (!session) {
                state.SkipWithError("Session lama gagal dialokasi.");
                return;
            }
            benchmark::DoNotOptimize(session);
        } else {
            // Jalur baru: create_session (scanner64 di dalam C ABI).
            int32_t err   = static_cast<int32_t>(zuu::api::INVALID_JSON);
            auto* session = create_session(
                sized.json.data(), sized.json.size(), sized.tape.data(),
                sized.tape.size(), sized.arena, err);
            if (!session || err != static_cast<int32_t>(zuu::api::OK)) {
                state.SkipWithError("create_session gagal.");
                return;
            }
            benchmark::DoNotOptimize(session);
        }
    }
}

void
BM_CreateSession_Pipeline_Scanner8(benchmark::State& state) {
    run_pipeline(state, true);
}

void
BM_CreateSession_Pipeline_Scanner64(benchmark::State& state) {
    run_pipeline(state, false);
}
} // namespace

// Argumen mengikuti indeks kSizesKib (lihat blok CreateSession_Sizes di atas).
BENCHMARK(BM_CreateSession_Pipeline_Scanner8)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(30)
    ->MinWarmUpTime(3)
    ->ArgName("Idx")
    ->Arg(2)
    ->Arg(4);

BENCHMARK(BM_CreateSession_Pipeline_Scanner64)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(30)
    ->MinWarmUpTime(3)
    ->ArgName("Idx")
    ->Arg(2)
    ->Arg(4);
