// ============================================================================
//  span_iso_benchmark — EKSPERIMEN A1: isolasi biaya span non-string scanner64
// ============================================================================
//  Hukum pengukuran rig ini (docs/development/benchmarking.md): jam antar-
//  proses bergetar 15-45%; satu-satunya perbandingan sahih adalah
//  bersebelahan dalam SATU proses. Eksperimen ini menguji hipotesis A1:
//
//  H1: jalur span non-string scanner64 (LUT kStructural + cek high-bit +
//      DFA UTF-8, byte-per-byte) mendominasi biaya marginal per byte.
//  H2: varian SWAR menurunkan biaya marginal itu secara substansial.
//
//  Empat varian per fill, diadu bersebelahan pada payload yang jumlah
//  TOKEN strukturalnya konstan (4096) dengan panjang FILLER yang
//  menskalakan (Pad). Token konstan => tulisan tape konstan => perubahan
//  waktu murni dari pemindaian span, bukan bottleneck A2 (lebar tape).
//
//    BM_SpanIso_Scanner64_*          — scanner64 asli; slope-nya "real"
//      (termasuk tulis tape + bookkeeping bracket).
//    BM_SpanIso_ByteLoopReplica_*    — replika VERBATIM loop span A1
//      (LUT + cek high-bit + DFA), tanpa tulis tape = isolasi murni.
//    BM_SpanIso_SWARSetReplica_*     — kandidat SWAR 8-byte dengan mask
//      OR dari 10 karakter set terminator persis (kStructural != 0).
//    BM_SpanIso_SWARAdvanceReplica_* — kandidat SWAR dengan fast-path
//      blok spasi penuh/blok digit penuh, fallback ke SWARSet.
//
//  Slope (ns/byte filler) dihitung notebook dari median per Pad.
//
//  GERBANG kebenaran (wajib lolos sebelum ada pengukuran yang sah):
//  count terminator ketiga replika == ukuran tape scanner64 (oracle =
//  library nyata) pada corpus pads {0,7,8,16,63,64,255,256,257} kedua
//  fill + corpus campur (string/escape/UTF-8/literal/eksponen + newline/
//  tab + '!') + corpus invalid-UTF-8 (replika wajib return-error sama).
//  Gagal => SkipWithError pada SEMUA benchmark.
// ============================================================================
#include <benchmark/benchmark.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <unordered_map>
#include <vector>

#include <ze_json/core/error.hpp>
#include <ze_json/core/macros.hpp>
#include <ze_json/lookups/structural.hpp>
#include <ze_json/lookups/utf8_dfa.hpp>
#include <ze_json/scanner/scanner.hpp>
#include <ze_json/types/integral.hpp>
#include <ze_json/types/result.hpp>
#include <ze_json/utils/repeat_byte.hpp>
#include <ze_json/utils/swar.hpp>

#include "../scanner_benchmark/replica_span_prehybrid.hpp"

using namespace zuu;
using zuu::types::size_t;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
//  Generator payload sintetis deterministik
// ---------------------------------------------------------------------------
namespace {

enum class FillKind { WS, NUM };

// Struktur: {"v":[<num><sep><num>...<num>]} dengan jumlah token KONSTAN.
//   FillNum: num = '1' + <pad> digit deterministik; sep = ","
//   FillWS : num = "1";                            sep = "," + <pad> spasi
// Filler digit/space TIDAK PERNAH termasuk set terminator
// (kStructural == 0), sehingga slope hanya dari pemindaian span.
// Ukuran payload per pad: ~4096*(2+pad)+8 B -> Pad 256 ~ 1.06 MB
// (tetap LLC-resident, tidak jatuh ke bandwidth memori).
constexpr size_t kTokens = 4096;

[[nodiscard]] std::string
make_span_payload(FillKind kind, size_t pad, size_t tokens = kTokens) {
    std::string json = "{\"v\":[";
    json.reserve(8 + tokens * (2 + pad) + 2);
    for (size_t i = 0; i < tokens; ++i) {
        json.push_back('1');
        if (kind == FillKind::NUM) {
            for (size_t j = 0; j < pad; ++j) {
                json.push_back(
                    static_cast<char>('0' + ((i * 31 + j * 7) % 10)));
            }
        }
        if (i + 1 < tokens) {
            json.push_back(',');
            if (kind == FillKind::WS) {
                json.append(pad, ' ');
            }
        }
    }
    json += "]}";
    return json;
}

// Corpus campur untuk gerbang — konten string dijaga TIDAK memuat satu pun
// karakter set-terminator (agar count span == tape scanner64 walau ada
// string); escape hanya \\u; UTF-8 valid di dalam DAN di luar string.
// Menyertakan: literal true/false/null, eksponen, newline/tab, '!'.
[[nodiscard]] std::string
make_mixed_gate_corpus() {
    std::string json = "{\"v\":[";
    json += "1,";
    json += R"( true, false, null, 2.5e-3, -1.75E+2,)";
    json += R"( "xyz", "mac\u0041\u00E9 \u2028",)";
    json += R"(""
,)";
    json += "0.5,\n\t -7,";
    json += "!3.25,";
    json += "44,";
    json += "é świetna!";
    json += ",88]";
    json += "}";
    return json;
}

// Korpus invalid sekuens UTF-8 terpotong di akhir payload — scanner64 dan
// replika wajib bersama-sama return-error (bukan count).
[[nodiscard]] std::string
make_invalid_utf8_corpus() {
    std::string json = "{\"v\":[1,2,\xC3]";
    return json;
}

} // namespace

// ---------------------------------------------------------------------------
//  REPLIKA span non-string — HARUS membekukan algoritma lawas/jadi kandidat.
//  Tanda tangan sama dengan scanner64 (drop-in) namun TIDAK menulis tape:
//  mengembalikan banyaknya terminator span yang ditemukan (dibandingkan
//  lawan ukuran tape scanner64 di gerbang).
// ---------------------------------------------------------------------------
namespace span_replica {

// Langkah DFA UTF-8 satu byte — mirror persis utf8_step scanner64.
constexpr bool
utf8_step_span(types::uint32_t& state, const char* byte_ptr) NOEXCEPT {
    const types::uint8_t type =
        lookups::kUtf8Dfa[static_cast<types::uint8_t>(*byte_ptr)];
    state = lookups::kUtf8Dfa[256 + state + type];
    return state != 12;
}

namespace {

constexpr types::uint64_t kHighBitMask =
    utils::repeat_byte_v<types::uint64_t, 0x80_u8>;
constexpr types::uint64_t kNibbleFMask =
    utils::repeat_byte_v<types::uint64_t, 0xF0_u8>;
constexpr types::uint64_t kNibble3Rep =
    utils::repeat_byte_v<types::uint64_t, 0x30_u8>;
constexpr types::uint64_t kSpaceRep =
    utils::repeat_byte_v<types::uint64_t, 0x20_u8>;
constexpr types::uint64_t kColonRep =
    utils::repeat_byte_v<types::uint64_t, 0x3A_u8>;
constexpr size_t kBlock = sizeof(types::uint64_t);

// SET terminator PERSIS memori kStructural != 0 — 10 karakter berikut,
// tanpa satu pun predikat yang hanya "mungkin-skip".
[[nodiscard]] constexpr types::uint64_t
terminator_set_mask(types::uint64_t block) NOEXCEPT {
    using utils::repeat_byte_v;
    using utils::swar_has_zero_byte;
    return swar_has_zero_byte(
               block ^ repeat_byte_v<types::uint64_t, 0x22_u8>) // '"' quote
           | swar_has_zero_byte(
                 block ^ repeat_byte_v<types::uint64_t, 0x2C_u8>) // ','
           | swar_has_zero_byte(
                 block ^ repeat_byte_v<types::uint64_t, 0x3A_u8>) // ':'
           | swar_has_zero_byte(
                 block ^ repeat_byte_v<types::uint64_t, 0x5B_u8>) // '['
           | swar_has_zero_byte(
                 block ^ repeat_byte_v<types::uint64_t, 0x5D_u8>) // ']'
           | swar_has_zero_byte(
                 block ^ repeat_byte_v<types::uint64_t, 0x7B_u8>) // '{'
           | swar_has_zero_byte(
                 block ^ repeat_byte_v<types::uint64_t, 0x7D_u8>) // '}'
           | swar_has_zero_byte(
                 block ^ repeat_byte_v<types::uint64_t, 0x74_u8>) // 't'
           | swar_has_zero_byte(
                 block ^ repeat_byte_v<types::uint64_t, 0x66_u8>) // 'f'
           | swar_has_zero_byte(
                 block ^ repeat_byte_v<types::uint64_t, 0x6E_u8>); // 'n'
}

// Ekor skalar VERBATIM loop span scanner64. ptr berakhir di terminator
// ATAU end. return: 0 = invalid-utf8 (ptr kena error; pemanggil bail),
// 1 = sehat.
[[nodiscard]] inline int
scalar_span_tail(
    const char*& ptr, const char* end, types::uint32_t& utf8) NOEXCEPT {
    while (ptr < end &&
           lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
        if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
            if (!utf8_step_span(utf8, ptr)) {
                return 0;
            }
        }
        ptr++;
    }
    return 1;
}

// Konsumsi terminator — mirror DFA pada byte struktural scanner64.
[[nodiscard]] inline bool
consume_terminator(const char* ptr, types::uint32_t& utf8) NOEXCEPT {
    if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
        return utf8_step_span(utf8, ptr);
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
//  Replika 1 — BYTE LOOP verbatim: persis loop span non-string scanner64
//  dengan kStructural-LUT, cek high-bit, dan branch DFA; NOP tulis tape.
// ---------------------------------------------------------------------------
[[nodiscard]] types::Result<size_t, core::JsonErrc>
byte_loop_replica(
    const char* const RESTRICT src, size_t src_size,
    types::size_t* RESTRICT /*dst*/, size_t /*dst_size*/) NOEXCEPT {
    types::uint32_t utf8 = 0;
    size_t found         = 0;

    auto ptr       = src;
    const auto end = src + src_size;

    while (ptr < end) {
        while (ptr < end &&
               lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
            if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
                if (!utf8_step_span(utf8, ptr)) [[unlikely]] {
                    return types::Unexpected{core::JsonErrc::InvalidUtf8};
                }
            }
            ptr++;
        }

        if (ptr >= end) {
            break;
        }

        // Byte struktural pun masuk DFA — mirror persis scanner64.
        if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
            if (!utf8_step_span(utf8, ptr)) [[unlikely]] {
                return types::Unexpected{core::JsonErrc::InvalidUtf8};
            }
        }

        // mirror tulis tape scanner64: satu terminator = satu entri.
        ++found;
        ptr++;
    }

    if (utf8 != 0) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }
    return static_cast<size_t>(found);
}

// ---------------------------------------------------------------------------
//  Replika 2 — SWAR SET: blok 8 byte, mask OR dari 10 XOR-HasZero persis;
//  blok ber-non-ASCII or DFA aktif () => ekor skalar verbatim.
// ---------------------------------------------------------------------------
[[nodiscard]] types::Result<size_t, core::JsonErrc>
swar_set_replica(
    const char* const RESTRICT src, size_t src_size,
    types::size_t* RESTRICT /*dst*/, size_t /*dst_size*/) NOEXCEPT {
    types::uint32_t utf8 = 0;
    size_t found         = 0;

    auto ptr       = src;
    const auto end = src + src_size;

    while (ptr < end) {
        bool block_stopped = false;

        // Jalur blok — hanya saat DFA ACCEPT dan muat blok penuh.
        while (utf8 == 0 && ptr + kBlock <= end) {
            types::uint64_t block;
            __builtin_memcpy(&block, ptr, kBlock);

            if ((block & kHighBitMask) != 0) [[unlikely]] {
                break; // non-ASCII -> ekor skalar (utf8 wajib per byte)
            }

            const types::uint64_t mask = terminator_set_mask(block);
            if (mask != 0) {
                ptr += static_cast<size_t>(__builtin_ctzll(mask)) >> 3;
                block_stopped = true; // menunjuk terminator dalam blok
                break;
            }
            ptr += kBlock; // blok bersih, tanpa terminator
        }

        if (!block_stopped && ptr < end &&
            lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
            // Ekor < 8 byte ATAU blok ber-non-ASCII tersangkut di span
            // biasa — ekor skalar.
            if (!scalar_span_tail(ptr, end, utf8)) [[unlikely]] {
                return types::Unexpected{core::JsonErrc::InvalidUtf8};
            }
        }

        if (ptr >= end) {
            break;
        }

        if (!consume_terminator(ptr, utf8)) [[unlikely]] {
            return types::Unexpected{core::JsonErrc::InvalidUtf8};
        }
        ++found;
        ptr++;
    }

    if (utf8 != 0) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }
    return static_cast<size_t>(found);
}

// ---------------------------------------------------------------------------
//  Replika 3 — SWAR ADVANCE: fast-path blok spasi penuh + blok digit penuh
//  (high-nibble 0x3 tanpa ':'), fallback SWARSet, ekor skalar verbatim.
// ---------------------------------------------------------------------------
[[nodiscard]] types::Result<size_t, core::JsonErrc>
swar_advance_replica(
    const char* const RESTRICT src, size_t src_size,
    types::size_t* RESTRICT /*dst*/, size_t /*dst_size*/) NOEXCEPT {
    types::uint32_t utf8 = 0;
    size_t found         = 0;

    auto ptr       = src;
    const auto end = src + src_size;

    while (ptr < end) {
        bool block_stopped = false;

        while (utf8 == 0 && ptr + kBlock <= end) {
            types::uint64_t block;
            __builtin_memcpy(&block, ptr, kBlock);

            if ((block & kHighBitMask) != 0) [[unlikely]] {
                break;
            }
            // Fast path 1: blok spasi penuh (0x2020..2020) — skip langsung.
            if ((block ^ kSpaceRep) == 0) {
                ptr += kBlock;
                continue;
            }
            // Fast path 2: blok digit penuh — semua high-nibble 0x3 dan
            // TANPA ':' (0x3A satu-satunya terminator set-nibble-3).
            // Byte lain nibble-3 (';','<','=','>','?') memang kStructural 0.
            if (((block & kNibbleFMask) ^ kNibble3Rep) == 0 &&
                utils::swar_has_zero_byte(block ^ kColonRep) == 0) {
                ptr += kBlock;
                continue;
            }

            const types::uint64_t mask = terminator_set_mask(block);
            if (mask != 0) {
                ptr += static_cast<size_t>(__builtin_ctzll(mask)) >> 3;
                block_stopped = true;
                break;
            }
            ptr += kBlock;
        }

        if (!block_stopped && ptr < end &&
            lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
            if (!scalar_span_tail(ptr, end, utf8)) [[unlikely]] {
                return types::Unexpected{core::JsonErrc::InvalidUtf8};
            }
        }

        if (ptr >= end) {
            break;
        }

        if (!consume_terminator(ptr, utf8)) [[unlikely]] {
            return types::Unexpected{core::JsonErrc::InvalidUtf8};
        }
        ++found;
        ptr++;
    }

    if (utf8 != 0) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }
    return static_cast<size_t>(found);
}

// ---------------------------------------------------------------------------
//  Replika 4 — SWAR HYBRID (probe adaptif): loop skalar VERBATIM sebagai
//  mesin, dengan PROBE blok murah (2 predikat, tanpa mask terminator)
//  yang hanya menyala ketika span SEBELUMNYA >= 8 byte (bukti A1: SWAR
//  menang di span panjang — Pad64/256 — tetapi KALAH di span pendek,
//  Pad8, karena overhead per blok melebihi pencarian byte).
//  Probe hanya melompat blok penuh spasi ATAU digit-run penuh tanpa ':'
//  — tak ada terminator yang lepas (set terminator tidak memuat byte
//  spasi/digit; ':' satu-satunya terminator nibble-3 dan dieksplisitkan).
//  Blok apa pun yang gagal predikat jatuh ke byte-loop — biaya Δ per
//  token hanya satu muat + 2 cmp, dirawat oleh ekor skalar.
// ---------------------------------------------------------------------------
[[nodiscard]] types::Result<size_t, core::JsonErrc>
swar_hybrid_replica(
    const char* const RESTRICT src, size_t src_size,
    types::size_t* RESTRICT /*dst*/, size_t /*dst_size*/) NOEXCEPT {
    types::uint32_t utf8 = 0;
    size_t found         = 0;

    // span pertama diizinkan probe (start state = "span panjang");
    // realita terukur: aproximasi tidak mengubah semantik apa pun.
    size_t prev_span_len = kBlock;

    auto ptr       = src;
    const auto end = src + src_size;

    while (ptr < end) {
        const char* const span_start = ptr;

        // PROBE adaptif: berulang di dalam span — tiap lompatan 8 byte
        // penuh (spasi ATAU digit-run tanpa ':') menghemat 8 byte-loop.
        // Gate: DFA ACCEPT + span sebelumnya >= 1 blok + muat blok penuh.
        while (utf8 == 0 && prev_span_len >= kBlock && ptr + kBlock <= end) {
            types::uint64_t block;
            __builtin_memcpy(&block, ptr, kBlock);
            if ((block & kHighBitMask) == 0 &&
                ((block ^ kSpaceRep) == 0 ||
                 (((block & kNibbleFMask) ^ kNibble3Rep) == 0 &&
                  utils::swar_has_zero_byte(block ^ kColonRep) == 0))) {
                ptr += kBlock;
                continue;
            }
            break; // predikat gagal — span pendek/campuran: byte-loop
        }

        while (ptr < end &&
               lookups::kStructural[static_cast<types::uint8_t>(*ptr)] == 0) {
            if (utf8 != 0 || (*ptr & 0x80) != 0) [[unlikely]] {
                if (!utf8_step_span(utf8, ptr)) [[unlikely]] {
                    return types::Unexpected{core::JsonErrc::InvalidUtf8};
                }
            }
            ptr++;
        }
        // panjang span TOTAL (probe + byte-loop) — sinyal adaptivitas.
        prev_span_len = static_cast<size_t>(ptr - span_start);

        if (ptr >= end) {
            break;
        }

        if (!consume_terminator(ptr, utf8)) [[unlikely]] {
            return types::Unexpected{core::JsonErrc::InvalidUtf8};
        }
        ++found;
        ptr++;
    }

    if (utf8 != 0) [[unlikely]] {
        return types::Unexpected{core::JsonErrc::InvalidUtf8};
    }
    return static_cast<size_t>(found);
}

} // namespace span_replica

// ---------------------------------------------------------------------------
//  GERBANG kebenaran — oracle = scanner64 (library nyata)
// ---------------------------------------------------------------------------
namespace {

using ScanFn = types::Result<size_t, core::JsonErrc> (*)(
    const char* const RESTRICT, size_t, types::size_t* RESTRICT,
    size_t) NOEXCEPT;

bool g_gate_ok = true;
std::string g_gate_detail;

[[nodiscard]] bool
check_corpus(
    const std::string& corpus, const char* corpus_name, ScanFn oracle,
    ScanFn replica, const char* replica_name) {
    if (!oracle) {
        return true; // varian tak tersedia pada target ini (guard arsitektur)
    }

    std::vector<types::size_t> tape(corpus.size());
    const auto base =
        oracle(corpus.data(), corpus.size(), tape.data(), tape.size());

    const auto rep =
        replica(corpus.data(), corpus.size(), tape.data(), tape.size());

    if (bool(base) != bool(rep)) {
        g_gate_detail = std::string(corpus_name) + " | " + replica_name +
                        ": status berbeda (" + (base ? "OK" : "ERROR") +
                        " vs " + (rep ? "OK" : "ERROR") + ")";
        return false;
    }
    if (base.has_error()) {
        return true; // kedua-duanya error — konsisten
    }
    if (static_cast<size_t>(base.value()) != static_cast<size_t>(rep.value())) {
        g_gate_detail = std::string(corpus_name) + " | " + replica_name +
                        ": count " +
                        std::to_string(static_cast<size_t>(rep.value())) +
                        " != tape scanner64 " +
                        std::to_string(static_cast<size_t>(base.value()));
        return false;
    }
    return true;
}

void
run_gate() {
#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1
    ScanFn oracle = zuu::scanner::scanner64;
#else
    ScanFn oracle = nullptr;
#endif

    const ScanFn replicas[] = {
        span_replica::byte_loop_replica,
        span_replica::swar_set_replica,
        span_replica::swar_advance_replica,
        span_replica::swar_hybrid_replica,
    };
    const char* replica_names[] = {
        "ByteLoopReplica",
        "SWARSetReplica",
        "SWARAdvanceReplica",
        "SWARHybridReplica",
    };

    const size_t gate_pads[] = {0, 7, 8, 16, 63, 64, 255, 256, 257};
    for (auto pad : gate_pads) {
        const std::string ws  = make_span_payload(FillKind::WS, pad);
        const std::string num = make_span_payload(FillKind::NUM, pad);

        std::string name_ws  = "FillWS/Pad:" + std::to_string(pad);
        std::string name_num = "FillNum/Pad:" + std::to_string(pad);

        for (size_t r = 0; r < std::size(replicas); ++r) {
            if (!check_corpus(
                    ws, name_ws.c_str(), oracle, replicas[r],
                    replica_names[r]) ||
                !check_corpus(
                    num, name_num.c_str(), oracle, replicas[r],
                    replica_names[r])) {
                g_gate_ok = false;
                return;
            }
        }
    }

    const std::string mixed = make_mixed_gate_corpus();
    for (size_t r = 0; r < std::size(replicas); ++r) {
        if (!check_corpus(
                mixed, "Mixed", oracle, replicas[r], replica_names[r])) {
            g_gate_ok = false;
            return;
        }
    }

    const std::string invalid = make_invalid_utf8_corpus();
    for (size_t r = 0; r < std::size(replicas); ++r) {
        if (!check_corpus(
                invalid, "InvalidUtf8", oracle, replicas[r],
                replica_names[r])) {
            g_gate_ok = false;
            return;
        }
    }
}

const bool g_gate_result = (run_gate(), g_gate_ok);

// ---------------------------------------------------------------------------
//  Benchmark — satu varian per fungsi, Args per sama Pasangan tepat
//  bersebelahan (urutan registrasi = urutan eksekusi Google Benchmark)
// ---------------------------------------------------------------------------
void
span_iso_run(benchmark::State& state, FillKind kind, ScanFn scan) {
    if (!g_gate_result) {
        state.SkipWithError(g_gate_detail);
        return;
    }

    const size_t pad       = static_cast<size_t>(state.range(0));
    const std::string json = make_span_payload(kind, pad);
    std::vector<types::size_t> tape_idx(json.size());

    const auto validation =
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
    // label ukuran payload (byte) — mempercepat pemetaan slope di notebook.
    state.SetLabel(std::to_string(json.size()));
}

#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1
void
BM_SpanIso_Scanner64_FillWS(benchmark::State& state) {
    span_iso_run(state, FillKind::WS, zuu::scanner::scanner64);
}
void
BM_SpanIso_Scanner64_FillNum(benchmark::State& state) {
    span_iso_run(state, FillKind::NUM, zuu::scanner::scanner64);
}
#endif

void
BM_SpanIso_ByteLoopReplica_FillWS(benchmark::State& state) {
    span_iso_run(state, FillKind::WS, span_replica::byte_loop_replica);
}
void
BM_SpanIso_ByteLoopReplica_FillNum(benchmark::State& state) {
    span_iso_run(state, FillKind::NUM, span_replica::byte_loop_replica);
}
void
BM_SpanIso_SWARSetReplica_FillWS(benchmark::State& state) {
    span_iso_run(state, FillKind::WS, span_replica::swar_set_replica);
}
void
BM_SpanIso_SWARSetReplica_FillNum(benchmark::State& state) {
    span_iso_run(state, FillKind::NUM, span_replica::swar_set_replica);
}
void
BM_SpanIso_SWARAdvanceReplica_FillWS(benchmark::State& state) {
    span_iso_run(state, FillKind::WS, span_replica::swar_advance_replica);
}
void
BM_SpanIso_SWARAdvanceReplica_FillNum(benchmark::State& state) {
    span_iso_run(state, FillKind::NUM, span_replica::swar_advance_replica);
}
void
BM_SpanIso_SWARHybridReplica_FillWS(benchmark::State& state) {
    span_iso_run(state, FillKind::WS, span_replica::swar_hybrid_replica);
}
void
BM_SpanIso_SWARHybridReplica_FillNum(benchmark::State& state) {
    span_iso_run(state, FillKind::NUM, span_replica::swar_hybrid_replica);
}

} // namespace

// Registrasi bersebelahan per Pasangan: Scanner -> ByteLoop -> SWARSet ->
// SWARAdvance. Scanner64 hadir di SEMUA Pasangan {0,8,64,256}; replika di
// {8,64,256} (Pasangan 0 adalah dasar slope scanner64 tanpa filler).
#define REGISTER_PADED(FUNC_NAME, PAD)                                         \
    BENCHMARK(FUNC_NAME)                                                       \
        ->ArgName("Pad")                                                       \
        ->Args({PAD})                                                          \
        ->Unit(benchmark::kNanosecond)                                         \
        ->MinTime(15)                                                          \
        ->MinWarmUpTime(3);

#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1
#define REGISTER_SPAN_GROUP(FILL_TAG, PAD)                                     \
    REGISTER_PADED(BM_SpanIso_Scanner64_Fill##FILL_TAG, PAD)                   \
    REGISTER_PADED(BM_SpanIso_ByteLoopReplica_Fill##FILL_TAG, PAD)             \
    REGISTER_PADED(BM_SpanIso_SWARSetReplica_Fill##FILL_TAG, PAD)              \
    REGISTER_PADED(BM_SpanIso_SWARAdvanceReplica_Fill##FILL_TAG, PAD)          \
    REGISTER_PADED(BM_SpanIso_SWARHybridReplica_Fill##FILL_TAG, PAD)
#else
#define REGISTER_SPAN_GROUP(FILL_TAG, PAD)                                     \
    REGISTER_PADED(BM_SpanIso_ByteLoopReplica_Fill##FILL_TAG, PAD)             \
    REGISTER_PADED(BM_SpanIso_SWARSetReplica_Fill##FILL_TAG, PAD)              \
    REGISTER_PADED(BM_SpanIso_SWARAdvanceReplica_Fill##FILL_TAG, PAD)          \
    REGISTER_PADED(BM_SpanIso_SWARHybridReplica_Fill##FILL_TAG, PAD)
#endif

REGISTER_PADED(BM_SpanIso_Scanner64_FillWS, 0) // dasar slope tanpa filler
REGISTER_PADED(BM_SpanIso_Scanner64_FillNum, 0)
// Hybrid ikut diukur pada Pasangan 0 — pengujian regresi-tanpa-filler:
// gate probe adaptif harus menyala-mati tanpa overhead di region padat.
REGISTER_PADED(BM_SpanIso_SWARHybridReplica_FillWS, 0)
REGISTER_PADED(BM_SpanIso_SWARHybridReplica_FillNum, 0)

REGISTER_SPAN_GROUP(WS, 8)
REGISTER_SPAN_GROUP(Num, 8)
REGISTER_SPAN_GROUP(WS, 64)
REGISTER_SPAN_GROUP(Num, 64)
REGISTER_SPAN_GROUP(WS, 256)
REGISTER_SPAN_GROUP(Num, 256)

// ---------------------------------------------------------------------------
//  TAHAP-6: pasangan TERSEBELAHAN pada dataset nyata (samples/commons/) —
//  library
//  scanner64 (span probe-hybrid) vs replika pra-hybrid beku. Adjacency
//  eksplisit per dataset: dua slot konsekutif = mitigasi getaran jam
//  antar-benchmark (bergetar 15-45% terdokumentasi).
// ---------------------------------------------------------------------------
#if __LITTLE_ENDIAN__ == 1 && __IS_64_BIT__ == 1

const fs::path SAMPLES_DIR =
    fs::path(__FILE__).parent_path().parent_path().parent_path() / "samples" /
    "commons";
[[nodiscard]] const std::string&
get_span_real_dataset(const std::string& name) {
    static std::unordered_map<std::string, std::string> cache;
    if (!cache.contains(name)) {
        auto& data = cache[name];
        std::ifstream reader(SAMPLES_DIR / name, std::ios::binary);
        if (!reader.is_open()) {
            std::println(stderr, "Gagal memuat {}", name);
            data.clear();
            return data;
        }
        auto file_size = fs::file_size(SAMPLES_DIR / name);
        data.resize(file_size);
        reader.read(data.data(), static_cast<std::streamsize>(file_size));
    }
    return cache[name];
}

#define REGISTER_SPAN_REAL_PAIR(FAMILY, FILE_NAME, CAPACITY)                   \
    static void BM_SpanReal_Hybrid_##FAMILY(benchmark::State& state) {         \
        static const std::string& json = get_span_real_dataset(FILE_NAME);     \
        if (json.empty()) {                                                    \
            state.SkipWithError("Dataset gagal");                              \
            return;                                                            \
        }                                                                      \
        std::vector<types::size_t> tape((CAPACITY));                           \
        for (auto _ : state) {                                                 \
            auto res = zuu::scanner::scanner64(                                \
                json.data(), json.size(), tape.data(), tape.size());           \
            benchmark::DoNotOptimize(res);                                     \
            benchmark::ClobberMemory();                                        \
        }                                                                      \
        state.SetBytesProcessed(                                               \
            state.iterations() * static_cast<int64_t>(json.size()));           \
    }                                                                          \
    BENCHMARK(BM_SpanReal_Hybrid_##FAMILY)                                     \
        ->Unit(benchmark::kNanosecond)                                         \
        ->MinTime(15)                                                          \
        ->MinWarmUpTime(3);                                                    \
    static void BM_SpanReal_PreHybrid_##FAMILY(benchmark::State& state) {      \
        static const std::string& json = get_span_real_dataset(FILE_NAME);     \
        if (json.empty()) {                                                    \
            state.SkipWithError("Dataset gagal");                              \
            return;                                                            \
        }                                                                      \
        std::vector<types::size_t> tape((CAPACITY));                           \
        for (auto _ : state) {                                                 \
            auto res =                                                         \
                replica_span_prehybrid::scanner64_prehybrid_span_replica(      \
                    json.data(), json.size(), tape.data(), tape.size());       \
            benchmark::DoNotOptimize(res);                                     \
            benchmark::ClobberMemory();                                        \
        }                                                                      \
        state.SetBytesProcessed(                                               \
            state.iterations() * static_cast<int64_t>(json.size()));           \
    }                                                                          \
    BENCHMARK(BM_SpanReal_PreHybrid_##FAMILY)                                  \
        ->Unit(benchmark::kNanosecond)                                         \
        ->MinTime(15)                                                          \
        ->MinWarmUpTime(3);

REGISTER_SPAN_REAL_PAIR(github_events, "github_events.json", 316 * 1024)
REGISTER_SPAN_REAL_PAIR(mesh, "mesh.json", 352 * 1024)
REGISTER_SPAN_REAL_PAIR(twitter, "twitter.json", 648 * 1024)
REGISTER_SPAN_REAL_PAIR(citm_catalog, "citm_catalog.json", 1768 * 1024)
REGISTER_SPAN_REAL_PAIR(update_center, "update-center.json", 3214 * 1024)
REGISTER_SPAN_REAL_PAIR(canada, "canada.json", 2256 * 1024)
REGISTER_SPAN_REAL_PAIR(jeopardy, "jeopardy.json", 54 * 1024 * 1024)
#endif
