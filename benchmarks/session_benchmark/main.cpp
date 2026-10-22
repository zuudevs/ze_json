// ==========================================================================
//  KELOMPOK UJI SESSION — API publik C ABI end-to-end:
//  pipeline create (scan + Session), flow find+getter, traversal array,
//  dan path query (flat / nested / array / dalam / multi-kueri).
//  Path query memakai SATU Session lintas iterasi (find_path me-rewind
//  sendiri), flow per-langkah memakai create_session per-iterasi.
// ==========================================================================
#include <benchmark/benchmark.h>
#include <string_view>

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
