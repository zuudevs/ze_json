#include <benchmark/benchmark.h>

#include <ze_json/ondemand/document.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/ze_json.hpp>

constexpr char kJsonPayload[] = R"({
    "id": 1048576,
    "name": "ZeJson High Performance Parser",
    "active": true,
    "nested_metadata": {
        "tags": ["swar", "simd", "ondemand", "lemire", "hoehrmann", "utf8"],
        "description": "This is a very long string that we want the parser to skip completely without allocating any memory at all.",
        "version_score": 3.141592653589793
    },
    "target": "success"
})";

constexpr zuu::types::size_t kPayloadSize = sizeof(kJsonPayload) - 1;
class ParserFixture : public benchmark::Fixture {
  public:
    zuu::types::size_t tape[1024];
    zuu::types::size_t tape_len;

    alignas(8) zuu::types::uint8_t arena_buffer[1024 * 1024];
    zuu::memory::Arena* arena;

    void
    SetUp(const ::benchmark::State& state) {
        arena = create_arena(arena_buffer, sizeof(arena_buffer));

        auto scan_res =
            zuu::scanner::scanner8(kJsonPayload, kPayloadSize, tape, 1024);

        if (scan_res.has_value()) {
            tape_len = scan_res.value();
        } else {
            tape_len = 0;
        }
    }

    void
    TearDown(const ::benchmark::State& state) {
        destroy_arena(arena);
    }
};

BENCHMARK_DEFINE_F(ParserFixture, DocumentInitialization)(
    benchmark::State& state) {
    for (auto _ : state) {
        zuu::ondemand::Document doc(kJsonPayload, tape, tape_len, arena);
        auto root = doc.get_root();

        benchmark::DoNotOptimize(root);
    }
    state.SetBytesProcessed(state.iterations() * kPayloadSize);
}
BENCHMARK_REGISTER_F(ParserFixture, DocumentInitialization)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

BENCHMARK_DEFINE_F(ParserFixture, FindField_Early_And_ParseInt)(
    benchmark::State& state) {
    zuu::types::StringView key("id", 2);

    for (auto _ : state) {
        zuu::ondemand::Document doc(kJsonPayload, tape, tape_len, arena);
        auto obj = doc.get_root().get_object();

        auto val                   = obj.find_field(key);
        zuu::types::int64_t id_val = val.get_int();

        benchmark::DoNotOptimize(id_val);
    }
    state.SetBytesProcessed(state.iterations() * kPayloadSize);
}
BENCHMARK_REGISTER_F(ParserFixture, FindField_Early_And_ParseInt)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

BENCHMARK_DEFINE_F(ParserFixture, ParseDouble_Nested_Lemire)(
    benchmark::State& state) {
    zuu::types::StringView key_nested("nested_metadata", 15);
    zuu::types::StringView key_score("version_score", 13);

    for (auto _ : state) {
        zuu::ondemand::Document doc(kJsonPayload, tape, tape_len, arena);

        auto root_obj = doc.get_root().get_object();

        auto nested_val = root_obj.find_field(key_nested);
        auto nested_obj = nested_val.get_object();

        auto score_val = nested_obj.find_field(key_score);
        double score   = score_val.get_double();

        benchmark::DoNotOptimize(score);
    }
    state.SetBytesProcessed(state.iterations() * kPayloadSize);
}
BENCHMARK_REGISTER_F(ParserFixture, ParseDouble_Nested_Lemire)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

BENCHMARK_DEFINE_F(ParserFixture, Extreme_Deep_Search_SkipChild)(
    benchmark::State& state) {
    zuu::types::StringView key_target("target", 6);

    for (auto _ : state) {
        zuu::ondemand::Document doc(kJsonPayload, tape, tape_len, arena);
        auto obj = doc.get_root().get_object();

        auto val     = obj.find_field(key_target);
        auto str_val = val.get_string();

        benchmark::DoNotOptimize(str_val);
    }
    state.SetBytesProcessed(state.iterations() * kPayloadSize);
}
BENCHMARK_REGISTER_F(ParserFixture, Extreme_Deep_Search_SkipChild)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);