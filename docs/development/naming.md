# Development: Konvensi Penamaan & Gaya Kode

> Bagian dari [docs/development/](.) — lihat juga
> [workflow.md](workflow.md) · [testing.md](testing.md) ·
> [benchmarking.md](benchmarking.md).

Sumber kebenaran pemformatan: [`.clang-format`](../../.clang-format)
(basis LLVM, C++23, indent 4, `AlwaysBreakAfterReturnType: All`,
`BreakBeforeBraces: Attach`, align konsekutif aktif). Tabel di bawah
adalah konvensi penamaan yang TIDAK dijalankan formatter — dibaca dari
konsensus kode yang ada.

## C++

| Elemen | Konvensi | Contoh |
|---|---|---|
| Namespace | `zuu::*` dua tingkat, topik per sub-namespace | `zuu::api`, `zuu::ondemand`, `zuu::scanner`, `zuu::memory`, `zuu::utils`, `zuu::types`, `zuu::lookups`, `zuu::core` |
| Tipe & kelas | PascalCase | `JsonValue`, `Session`, `Arena`, `JsonIterator`, `RootFieldSlot` |
| Fungsi & metode | snake_case | `find_field_checked`, `create_session`, `array_next`, `count_elements` |
| Member privat | akhiran `_` | `iter_`, `src_`, `idx_slots_`, `array_mode_` |
| Konstanta compile-time | prefiks `k` | `kStructural`, `kUtf8Dfa`, `kIdxQuota`, `kArrIdxQuota` |
| Enum ABI `Status` | enumerasi SCREAMING_SNAKE (kode ABI) | `INVALID_JSON`, `TYPE_MISMATCH` |
| Enum `ValueType` | enumerasi PascalCase | `ValueType::Number`, `ValueType::Null` |
| `enum class` dalam kelas | PascalCase | `ArrayIterMode::Streaming` |
| Makro proyek | KAPITAL, dari `core/macros.hpp` | `NOEXCEPT`, `EXPORT_NAME`, `RESTRICT`, `EXTERN_C_BEGIN/END` |
| Literal bertipe | sufiks `_u8/_u16/_u32/_u64` | `0x22_u8`, `0x0000010000000193_u64` |
| Atribut | `[[nodiscard]]` pada hasil bermakna; `[[likely]]`/`[[unlikely]]` pada cabang statistik | lihat `ze_json.hpp` |

Aturan gaya penting:

- **Komentar Bahasa Indonesia**, kalimat lengkap, menjelaskan *mengapa*
  (bukan menerjemahkan kode). Kontrak perilaku (ritme kursor, drain,
  batasan exact-byte) didokumentasikan di komentar header dan
  dipertahankan oleh uji.
- Fungsi keluaran ABI selalu `NOEXCEPT` + `EXPORT_NAME` — galat
  dikomunikasikan lewat status/status-out, bukan eksepsi.
- Satu header = satu kelas/wajah; include path `<ze_json/…>` tanpa `.h`.

## File & folder

| Elemen | Konvensi | Contoh |
|---|---|---|
| File C++ | snake_case | `json_iterator.hpp`, `hash_fnv1a.hpp`, `ze_json.cpp` |
| Folder uji | `<topik>_test/` — **1 kelompok uji = 1 folder** | `tests/path_test/main.cpp` |
| Folder benchmark | `<topik>_benchmark/` — 1 kelompok = 1 folder | `benchmarks/session_benchmark/main.cpp` |
| Skrip tooling | snake_case `.js` | `benchmark_metrics.js`, `test_metrics.js` |
| Dokumentasi | kebab-case `.md` | `docs/architecture/js-binding.md` |

## JavaScript

| Elemen | Konvensi | Contoh |
|---|---|---|
| Kelas | PascalCase | `ZeJson`, `Doc` |
| Metode | camelCase | `openDocument`, `arrayNext`, `stringAt` |
| Konstanta layout | SCREAMING_SNAKE | `KEY_BASE`, `PAYLOAD_CAP`, `TAPE_ENTRIES` |
| Ekspor | kelas + konstanta; tanpa default export | `export { STATUS, VALUE_TYPE, ZeJson }` |

## Pesan commit (conventional commits, bahasa Indonesia)

Prefiks yang dipakai proyek: `feat`, `fix`, `perf`, `refactor`, `test`,
`bench`, `docs`, `style`, `chore`, `ci`, `release(npm)`. Subjek singkat
bahasa Indonesia; scope opsional di kurung. Untuk commit `perf`/`release`,
**bukti benchmark before/after wajib masuk body** — tulis pesan panjang ke
file lalu `git commit -F <file>` (hindari quoting PowerShell).

Contoh dari riwayat:

```
perf(object): indeks root lazy — path query objek lebar O(1)
fix(js)+release(npm): v0.1.2 — getter TYPE_MISMATCH + dokumentasi 4 tahap
bench(js): sapuan ukuran JSON.parse vs ze_json — titik impas terukur
```

## Diagram dalam Markdown

Semua diagram ditulis dalam **sintaksis Mermaid** (GitHub merender
native). Validasi sebelum commit:

```bash
npx -y jsdom  # bila perlu; lihat pola validasi: mermaid.parse() per blok
```

Pola yang dipakai: `flowchart LR/TD` untuk pipeline/keputusan (label node
selalu dalam tanda kutip), `stateDiagram-v2` untuk mesin state
(mis. `ArrayIterMode`).
