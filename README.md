# ze_json

> **Parser JSON on-demand berperforma tinggi** — scanner SWAR + tape struktural,
> pembacaan zero-copy tanpa alokasi per-nilai, arena allocator, dan binding
> **Raw WebAssembly memory64** (tanpa Emscripten) untuk JavaScript.

![Lisensi](https://img.shields.io/badge/lisensi-MIT-green)
![C++](https://img.shields.io/badge/C%2B%2B-23-blue)
![Uji](https://img.shields.io/badge/uji-58%2F58%20lolos-success)
![Node](https://img.shields.io/badge/Node-24%2B%20%28memory64%29-brightgreen)

*Dokumen ini tersedia juga dalam [English](README.en.md).*

---

## Gambaran

`ze_json` dibangun di atas dua ide sederhana:

1. **Diskan sekali, baca on-demand.** Scanner SWAR memvalidasi seluruh payload
   (UTF-8, string, tanda kurung) dan **merekam tape** — daftar posisi byte setiap
   karakter struktural — *tanpa membangun tree*. Nilai hanya di-*parse* tepat
   ketika dibaca, dan string **tidak disalin**: pembacaan menunjuk langsung ke
   memori payload (zero-copy); unescape hanya berjalan bila benar-benar
   diperlukan dan hasilnya dialokasi dari arena.
2. **Semua alokasi dari arena.** `Session` + string hasil unescape hidup dalam
   satu arena bump-allocator; siklus dokumen berikutnya hanya butuh
   `reset_arena` — hot loop **tanpa alokasi** dan tanpa fragmentasi.

Pipeline lengkapnya:

```
payload ──scanner8/scanner64──▶ tape (posisi struktural)
                                   │
              Session (arena) ─────┴─▶ find / array traversal / path query
                                   │
                        C ABI (EXPORT_NAME) ──▶ JavaScript (wasm64)
```

## Performa (hasil terukur, data lengkap di `out/results/`)

Semua angka dari mesin pengembang (`12 core × 2,096 MHz`, Clang LLVM 23,
google-benchmark v1.9.5, Node v24.19.0) — lihat tabel lengkap di
[`out/results/benchmark/summary_*.md`](out/results/benchmark) dan
[`out/results/test/summary_*.md`](out/results/test).

| Aspek | Hasil |
|---|---|
| Throughput scanner | **1,1–1,3 GB/s** di semua dataset (61 MB korpus) |
| Scanner64 vs Scanner8 | **1,11–1,34× lebih cepat** pada file besar (`UpdateCenter` 1,34×, `Jeopardy` 1,17×) |
| Pipeline C++ per dokumen kecil | create (scan + Session) **238 ns** · find×3 + getter **411 ns** |
| Path query C++ | **41 ns** (flat) — **208 ns** untuk 3 kueri berbeda tipe |
| Pencarian object lebar (512 field) | field pertama **18 ns** vs field terakhir **7,6 µs** (bukti biaya linear forward-only) |
| Hot loop JS (demo 4) | per-langkah **0,75 µs/dok** vs path **0,92 µs/dok** untuk kunci top-level berurutan — path tetap **1 panggilan wasm** untuk kedalaman apa pun |

## Struktur repository

```
ze_json/
├── include/ze_json/        API publik C++, on-demand layer, scanner, memory
│   ├── ze_json.hpp         Session + C ABI (EXPORT_NAME) + enum Status/ValueType
│   ├── scanner/scanner.hpp scanner8 / scanner64 (SWAR, validasi UTF-8)
│   ├── ondemand/           document.hpp, json_object.hpp, json_value.hpp,
│   │                       json_iterator.hpp, json_array.hpp
│   ├── core/macros.hpp     makro NOEXCEPT / EXPORT_NAME / RESTRICT
│   └── memory/arena.hpp    bump allocator
├── src/                    implementasi (scanner, ondemand, C ABI, compiler_rt stub)
├── tests/                  7 grup uji google-test (1 kelompok = 1 folder):
│                           scanner, document, object, number, session, array, path
├── benchmarks/             3 grup google-benchmark (1 kelompok = 1 folder):
│                           scanner (dataset), parser (Document), session (C ABI)
├── js/                     PUBLIC API JavaScript (@zuudevs/ze-json)
│   ├── ze_json.mjs         binding wasm64 + konstanta layout + kelas ZeJson/Doc
│   └── test/main.mjs       25 asseri smoke lintas ABI
├── examples/js/example.mjs demo end-to-end di JS (4 demo)
├── scripts/                agregator metrik: test_metrics.js, benchmark_metrics.js
├── cmake/                  preset CMake (C++23, LLVM/clang, helper vcpkg)
├── out/results/            artefak metrik ter-commit (JSON + Markdown)
└── dataset/                korpus 61 MB — tidak masuk git (distribusi terpisah)
```

## Build

Kebutuhan: **CMake ≥ 3.40**, toolchain clang (LLVM), **Node.js ≥ 24** untuk
jalur JS (memory64 aktif default pada Node 24+).

```bash
# Native (pengujian & benchmark)
cmake --preset native-debug          # atau native-release
cmake --build build/native-debug

# WebAssembly memory64 (untuk JavaScript)
cmake --preset wasm64-release
cmake --build build/wasm64-release
#   → build/wasm64-release/src/ze_json_core.wasm
```

## Pemakaian C++

```cpp
using namespace zuu;

memory::Arena* arena = create_arena(buffer, sizeof(buffer));

// 1 lapis tinggi: Session — scan + baca per-langkah (root object)
types::size_t tape[512];
api::Status st = api::OK;
api::Session* s = create_session(payload, len, tape, 512, arena, st);

find_field(s, "position.x", 10);   // find_path juga tersedia via Session
types::int64_t id = get_int(s);    // kursor di angka sekarang

// 1 lapis ala on-demand: Document + JsonObject + array traversal
ondemand::Document doc(payload, tape, tape_len, arena);
auto found = doc.get_root().get_object().find_field({"colors", 6});
//            json_iterator::get_array / array_next / json_value::skip ...

destroy_arena(arena);
```

Detail kontrak kursor (drain array, konsumsi `null`, rewind setelah miss)
didokumentasikan lewat uji — baca komentar di `tests/session_test/`
dan `tests/array_test/`.

## C ABI (wilayah export wasm)

| Kategori | Export |
|---|---|
| Arena | `create_arena`, `reset_arena`, `destroy_arena`, `get_arena_used_space` |
| Session | `create_session`, `find_field`, `destroy_session`, `value_type` |
| Getter | `get_int`, `get_double`, `get_bool`, `get_string` |
| Array | `array_size`, `array_next` |
| Path query | `find_path`, `get_int_at`, `get_double_at`, `get_bool_at`, `get_string_at` |
| Scanner | `scanner8`, `scanner64`, `scanner32` |

Semua kode status dalam `Status` (`OK`, `INVALID_JSON`, `NOT_FOUND`,
`TYPE_MISMATCH`, `INVALID_PATH`, …) didokumentasikan di
[`include/ze_json/ze_json.hpp`](include/ze_json/ze_json.hpp).

## Pemakaian JavaScript

```js
import { ZeJson } from "./js/ze_json.mjs";

const ze = await ZeJson.load();          // muat ze_json_core.wasm
const doc = ze.openDocument(jsonText);

doc.find("id");                    // → { found: true, type: "Number" }
doc.asInt();                       // → 21n (BigInt, ABI 64-bit)
doc.intAt("position.x");           // → { value: 24, status: "OK" }
doc.stringAt("colors[1]");         // → { value: "black", status: "OK" }

for (let i = 0; i < doc.arraySize(); i++) {
  if (doc.arrayNext()) doc.asString();   // traversal array + drain
}

ze.close();                        // rilis arena di akhir siklus hidup
```

Paket npm ([`js/package.json`](js/package.json)):

```bash
cd js
npm run build:wasm   # cmake --preset wasm64-release && build
npm test             # 25 asseri smoke via ABI
npm pack             # @zuudevs/ze-json-0.1.0.tgz (~23 kB, wasm ikut ter-pack)
```

## Pengujian

7 grup × 2 preset = **58 test, semua lolos** (native-debug & native-release).
Satu kelompok uji = satu folder: `tests/{scanner,document,object,number,
session,array,path}_test/`.

```bash
# Metrik uji (ringkasan + laporan gtest JSON per grup → out/results/test/)
node scripts/test_metrics.js native-debug native-release
```

## Benchmark

3 kelompok × **28 benchmark**: scanner (10), parser/Document (9),
session/C ABI end-to-end (9).

```bash
node scripts/benchmark_metrics.js native-release        # full run
node scripts/benchmark_metrics.js --parse-only native-release   # re-summarize
# → out/results/benchmark/summary_<hash>.{json,md}
```

## Batasan yang dikenal

- **Kunci exact-byte**: pencarian `find_field`/path membandingkan byte mentah
  payload — kunci dengan escape (`"\u0061"`) tidak cocok dengan path
  `"a"` (terdokumentasi di header).
- **1 Session = 1 dokumen ber-root object**: array root perlu dibungkus
  object atau dibaca via `Document` langsung.
- **Kursor forward-only**: `find_field` kumulatif hanya maju; untuk akses
  acak gunakan path query (tiap kueri me-rewind dari root).
- **Kunci yang ditulis ke region harus ditulis ulang setelah `reset_arena`**
  — region payload tetap, tapi Session baru harus di-*create* ulang.

## Dataset

Korpus benchmark (`Tiny`, `Literal`, `GithubEvents`, `UpdateCenter`,
`Jeopardy` — total ~61 MB) **tidak dimasukkan ke git**
([`.gitignore`](.gitignore)); jalankan benchmark dengan menaruh kembali
file korpus di `dataset/`, atau sesuaikan nama dataset daftar benchmark
dengan file milik Anda.

## Lisensi

[MIT](LICENSE) — © 2026 **Rafi Indra Pramudhito Zuhayr**.
Dikembangkan sebagai inti tugas skripsi (2026).
