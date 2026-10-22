#include <benchmark/benchmark.h>
#include <string>
#include <vector>

#include <ze_json/ondemand/document.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/ze_json.hpp>

// ==========================================================================
//  KELOMPOK UJI PARSER — lapisan Document (on-demand), payload kecil
// ==========================================================================
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
    SetUp(::benchmark::State& state) override {
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
    TearDown(::benchmark::State& state) override {
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

// ==========================================================================
//  KELOMPOK UJI PARSER — object lebar (512 field): biaya pencarian linear
//  direkam sebagai fungsi jarak kunci (pertama / tengah / terakhir).
// ==========================================================================
namespace {

constexpr std::size_t kWideFields = 512;

// '{"key0":"v0","key1":"v1",...,"key511":"v511"}'
std::string
build_wide_payload(std::size_t fields) {
    std::string out = "{";
    for (std::size_t i = 0; i < fields; ++i) {
        if (i > 0) {
            out += ',';
        }
        out += R"("key)";
        out += std::to_string(i);
        out += R"(":"v)";
        out += std::to_string(i);
        out += '"';
    }
    out += '}';
    return out;
}

} // namespace

class WideObjectFixture : public benchmark::Fixture {
  public:
    std::string payload;
    std::vector<zuu::types::size_t> tape;

    void
    SetUp(::benchmark::State& state) override {
        payload = build_wide_payload(kWideFields);
        tape.resize(payload.size());
        const auto scan = zuu::scanner::scanner8(
            payload.data(), payload.size(), tape.data(), tape.size());
        if (!scan.has_value()) {
            state.SkipWithError("Payload lebar gagal diskan.");
        }
    }
};

// Cari field PERTAMA: jarak pencarian linear minimum.
BENCHMARK_DEFINE_F(WideObjectFixture, FindField_First_InWideObject)(
    benchmark::State& state) {
    zuu::types::StringView key_first("key0", 4);
    const auto len = payload.size();

    for (auto _ : state) {
        zuu::ondemand::Document doc(
            payload.data(), tape.data(), tape.size(), nullptr);
        auto obj = doc.get_root().get_object();

        auto val = obj.find_field(key_first);

        benchmark::DoNotOptimize(val);
    }
    state.SetBytesProcessed(state.iterations() * len);
}
BENCHMARK_REGISTER_F(WideObjectFixture, FindField_First_InWideObject)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

// Cari field TENGAH (key255): separuh jarak pencarian.
BENCHMARK_DEFINE_F(WideObjectFixture, FindField_Middle_InWideObject)(
    benchmark::State& state) {
    zuu::types::StringView key_mid("key255", 6);
    const auto len = payload.size();

    for (auto _ : state) {
        zuu::ondemand::Document doc(
            payload.data(), tape.data(), tape.size(), nullptr);
        auto obj = doc.get_root().get_object();

        auto val = obj.find_field(key_mid);

        benchmark::DoNotOptimize(val);
    }
    state.SetBytesProcessed(state.iterations() * len);
}
BENCHMARK_REGISTER_F(WideObjectFixture, FindField_Middle_InWideObject)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

// Cari field TERAKHIR (key511): biaya pencarian linear penuh.
BENCHMARK_DEFINE_F(WideObjectFixture, FindField_Last_InWideObject)(
    benchmark::State& state) {
    zuu::types::StringView key_last("key511", 6);
    const auto len = payload.size();

    for (auto _ : state) {
        zuu::ondemand::Document doc(
            payload.data(), tape.data(), tape.size(), nullptr);
        auto obj = doc.get_root().get_object();

        auto val = obj.find_field(key_last);

        benchmark::DoNotOptimize(val);
    }
    state.SetBytesProcessed(state.iterations() * len);
}
BENCHMARK_REGISTER_F(WideObjectFixture, FindField_Last_InWideObject)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

// ==========================================================================
//  KELOMPOK UJI STRING — payload tanpa escape (zero-copy langsung ke
//  memory payload) vs payload ber-escape (aliran unescape arena).
// ==========================================================================
constexpr char kPlainStringJson[]   = R"({"s":"white black red"})";
constexpr char kEscapedStringJson[] = R"({"s":"Ze\nJson \"one\" two\u00eb"})";

class StringReadFixture : public benchmark::Fixture {
  public:
    zuu::types::size_t tape_plain[128];
    zuu::types::size_t tape_esc[128];
    zuu::types::size_t plain_len{0};
    zuu::types::size_t esc_len{0};
    alignas(8) zuu::types::uint8_t arena_buffer[1024 * 1024];
    zuu::memory::Arena* arena{nullptr};

    void
    SetUp(::benchmark::State& state) override {
        arena = create_arena(arena_buffer, sizeof(arena_buffer));

        auto plain = zuu::scanner::scanner8(
            kPlainStringJson, sizeof(kPlainStringJson) - 1, tape_plain, 128);
        auto esc = zuu::scanner::scanner8(
            kEscapedStringJson, sizeof(kEscapedStringJson) - 1, tape_esc, 128);
        if (plain.has_value()) {
            plain_len = plain.value();
        } else {
            state.SkipWithError("Payload tanpa-escape gagal diskan.");
        }
        if (esc.has_value()) {
            esc_len = esc.value();
        } else {
            state.SkipWithError("Payload escape gagal diskan.");
        }
    }

    void
    TearDown(::benchmark::State& state) override {
        destroy_arena(arena);
    }
};

BENCHMARK_DEFINE_F(StringReadFixture, GetString_PlainPayload)(
    benchmark::State& state) {
    zuu::types::StringView key("s", 1);
    for (auto _ : state) {
        zuu::ondemand::Document doc(
            kPlainStringJson, tape_plain, plain_len, arena);
        auto obj = doc.get_root().get_object();
        auto val = obj.find_field(key);
        auto sv  = val.get_string(); // zero-copy, tanpa unescape
        benchmark::DoNotOptimize(sv);
    }
}
BENCHMARK_REGISTER_F(StringReadFixture, GetString_PlainPayload)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);

BENCHMARK_DEFINE_F(StringReadFixture, GetString_EscapedPayload)(
    benchmark::State& state) {
    zuu::types::StringView key("s", 1);
    for (auto _ : state) {
        zuu::ondemand::Document doc(
            kEscapedStringJson, tape_esc, esc_len, arena);
        auto obj = doc.get_root().get_object();
        auto val = obj.find_field(key);
        auto sv  = val.get_string(); // unescape → arena
        benchmark::DoNotOptimize(sv);
    }
}
BENCHMARK_REGISTER_F(StringReadFixture, GetString_EscapedPayload)
    ->Unit(benchmark::kNanosecond)
    ->MinTime(15)
    ->MinWarmUpTime(5);
