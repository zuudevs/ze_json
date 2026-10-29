# Changelog

Format mengikuti [Keep a Changelog](https://keepachangelog.com/id/1.1.0/);
versi mengikuti [SemVer](https://semver.org/). Rilis npm
`@zuudevs/ze-json` diterbitkan lewat CI pada tag `v*`.

## [Belum dirilis]

### Ditambahkan

- Eksperimen A1 `benchmarks/span_iso_benchmark/` (PR #4→#5): isolasi span
  non-string dengan replika ByteLoop/SWARSet/SWARAdvance/Hybrid + gerbang
  kebenaran count==tape, pasangan terhimpit dataset nyata `BM_SpanReal_*`
  (dengan replika `replica_span_prehybrid.hpp` — baseline `scanner64`
  v0.1.6 beku), dan aturan notebook in-run baru
  (`spaniso_*`, `spanreal_hybrid_vs_prehybrid`,
  `spanhybrid_library_vs_spanbaseline`). Tanpa perubahan `src/`: adopsi
  probe span hybrid ke `scanner64` terukur regresi di semua dataset nyata
  (V1/V2, 0,45–0,99) dan di-revert — lihat
  `docs/architecture/optimizations.md` §Tahap-6.
- Eksperimen Tahap-7 `benchmarks/session_benchmark/` (C2 — validator
  gramatikal tape-walk): replika beku
  `replica_grammar_tapewalk.hpp` (mesin status 10-state + stack-bit;
  gerbang korpus json.org 33 fail, 26 reject-target, sisanya
  accept-sengaja string-lazy/depth), pasangan in-run
  `BM_GRAM_Base_*` vs `BM_GRAM_Tapewalk_*` untuk 7 dataset nyata +
  payload sintetis, aturan notebook `gram_base_vs_tapewalk`, artefak
  AMD uProf `out/results/profiling/amduprof_tahap7_*.csv`. Hasil
  NEGATIF: overhead +90–201% vs `create_session`; dekomposisi
  α ≈ 7,1 ns/entri-tape dominan (R²=1,0) → pass O(token) kedua tidak
  bisa mendekati gerbang ±5–10%; tidak diadopsi ke `src/` — lihat
  `docs/architecture/optimizations.md` §Tahap-7 dan
  `docs/architecture/grammar-validation.md` §Hasil pengukuran.
- Mode CLI headless notebook `scripts/analyze_result/main.py`: saat
  dijalankan sebagai `python main.py` (di luar Jupyter) figure
  matplotlib disimpan ke `out/analysis/figures/*.png` alih-alih
  membuka jendela interaktif; di Jupyter tetap inline.

### Diubah

- **Struktur proyek**: `JSON_checker` di-vendor ke
  `third_party/JSON_checker/` (target CMake `json_checker` dengan
  `SAMPLE_DIR` PUBLIC → `samples/JSON_Checker`); korpus json.org
  dipindah dari `tests/JSON_checker_test/corpus/` ke submodule
  `samples/JSON_Checker`; `scripts/invoke_JSON_checker/` diganti
  `scripts/install_JSON_checker/` — installer/downloader json.org
  (sumber → `third_party/`, korpus → `samples/`).
- **Toolchain CMake dimodularisasi**: modul baru `exporter.cmake`
  (`add_export_benchmark/test_target`), `coverage.cmake` (llvm-cov),
  `profiler.cmake` (7 config AMDuProf CLI), `optimizations.cmake`
  restrukturisasi ke `add_optimizations_target` + dukungan PGO
  (`pgo.cmake`, auto-detect `merged.profdata`) + `-march=native`;
  preset baru `native-release-with-debug-info` dan
  `wasm{32,64}-release-with-debug-info` (RelWithDebInfo, untuk
  profiling bersimbol); `src/CMakeLists.txt` pakai
  `$<BUILD_INTERFACE>`/`$<INSTALL_INTERFACE>`. Tanpa perubahan
  semantik `src/` (sisa diff = clang-format, terverifikasi `git
  diff -w`).

## [0.1.6] — 2026-10-26

### Diperbaiki

- **Revert goto-based automaton** (PR #2 → #3): goto state machine memberikan
  validasi gramatikal penuh (20/33 file fail json.org tertolak) tetapi
  menggerus throughput berat (S64 Jeopardy 1423 → 511 Mi/s). Kembali ke
  arsitektur stack-bit + SWAR dari PR #1 — throughput pulih (S64 Jeopardy
  1252 Mi/s, +145% dari goto).
  Pelajaran: goto automaton paper (Langdale & Lemire 2019) adalah untuk
  Stage 2 (parser atas indeks), bukan Stage 1 (scanner) — menggabungkan
  keduanya = overhead di jalur panas.

### Dipertahankan dari PR #1–#2

- Pemisahan algoritma scanner: `scalar.cpp` (scanner8, validator UTF-8
  terpisah) vs `parallel.cpp` (scanner64/32, UTF-8 terintegrasi SWAR).
- Stack-bit bracket validation: `["mismatch"}` dan `[{]}` tertolak.
- `ALIGNED(val)` macro di `macros.hpp` (typo `__attributes__` diperbaiki).
- Scanner variant selection: `create_session_with_scanner(SCANNER_AUTO/SCALAR/WIDE)` +
  `ZeJson.load({ scanner: "auto"|"scalar"|"wide" })`.
- Version compatibility: `ZE_JSON_VERSION_NUMBER` (2026000106 = 0.1.6) +
  `get_version()` ABI → JS `ze.versionNumber` / `ze.hasScannerSelection`.
- Scanner32 kini membawa validasi bracket (dahulu TANPA validasi sama sekali).
- `json_checker_test` lintas varian scanner + replika interleaved sentinel.
- Submodule `samples/`, notebook analisis, dokumentasi modular + Mermaid.
- `docs/architecture/e2e-flow.md` + `benchmarks/scanner_benchmark/replica_integrated.hpp`.
- Aturan: setiap fix diawali bump versi di `CMakeLists.txt`.

## [0.1.4] — 2026-10-25

### Ditambahkan

- **Metadata versi tersinkron**: `project(VERSION 0.1.4)` di
  `CMakeLists.txt` + `vcpkg.json` + `js/package.json`;
  `include/ze_json/version.hpp` DIGENERASI saat configure dari
  `cmake/templates/version.hpp.in` via `cmake/modules/generator.cmake`
  (ter-ignore; `ze_json.hpp` meng-include-nya).
- **`tests/JSON_checker_test/`** — grup uji ke-8: pembanding REFERENCE
  resmi json.org (implementasi C Douglas Crockford, domain publik,
  ter-commit) pada korpus klasik pass*/fail*. Metodologi jujur: pass
  diterima dua validator; fail ditolak reference; scanner hanya wajib
  menolak subset **struktural** (`fail2/8/32`) — 30 divergensi gramatikal
  terkunci sebagai batas desain scanner struktural (termasuk
  `fail33` "tipe bracket tak cocok": depth counter tak bertipe — kandidat
  perbaikan terdokumentasi).
- `scripts/invoke_JSON_checker/main.py` — pengunduh korpus + sumber
  reference dari json.org (sekali jalan; hasil ter-commit) +
  `requirements.txt`.
- `scripts/` direorganisasi per kegunaan: `analyze_result/` (notebook
  analisis artefak + requirements), `invoke_JSON_checker/`; agregator
  metrik JS tetap di root.

## [0.1.3] — 2026-10-24

### Performa

- **Salin payload `encodeInto`** — `writeText` menulis UTF-8 langsung ke
  view memori wasm (dahulu `encode()` mengalokasi buffer sementara +
  `set()` menyalin lagi). Bukti interleaved `js_copy_*.json`: ×4 (11 B)
  s.d. **×24** (64 KiB ASCII), ×1,4 (UTF-8 padat); E2E sapuan ukuran:
  ze scan menang di **semua ukuran teruji** (titik impas scan murni
  turun dari 268 B ke <95 B), baca 2 field di 256 KiB naik ke **~3,9×**
  (dahulu ~2,7×).

### Ditambahkan

- **Submodule `samples/`** menggantikan `dataset/` (semula dideklarasikan
  dengan URL salah-ketik `zuudevs/zuu_json_sample` dan tak pernah
  ter-commit; kini gitlink resmi ke
  `https://github.com/zuudevs/ze_json_sample.git`) — korpus simdjson
  ~61 MB kini tampil di repository; klon dengan `--recurse-submodules`.
- `scripts/analyze_result/main.py` — notebook analisis artefak
  `out/results/` (inventaris & klasifikasi skema, pasangan Scanner8/64
  dalam-run, before/after eksploratif, sapuan JS, laporan test) +
  `scripts/requirements.txt` (venv); ekspor tabel ke `out/analysis/`
  (ter-ignore — turunan artefak).
- `js/benchmark/sizes.mjs` — sapuan ukuran bertahap `JSON.parse` vs
  ze_json (95 B–256 KiB × 6 workload, metrik terisolasi: payload
  deterministik, SINK anti-DCE, satu proses) dan
  `js/benchmark/copy.mjs` — micro-benchmark interleaved strategi salin
  payload ke wasm (encode+set vs encodeInto).
- Struktur dokumentasi modular `docs/`: `architecture/*.md`, `api.md`,
  `changelog.md` (berkas ini) — diagram dalam sintaksis Mermaid.
- `docs/development/` — panduan pengembangan: `workflow.md` (siklus,
  preset, CI, rilis), `naming.md` (konvensi penamaan & gaya kode),
  `testing.md` (filosofi uji, peta grup, menambah uji + jebakan),
  `benchmarking.md` (aturan pengukuran tahan-noise, artefak, resep).

## [0.1.2] — 2026-10-24

### Performa — empat tahap optimasi bottleneck (semua dibuktikan
benchmark before/after ter-commit; rincian di
[architecture/optimizations.md](architecture/optimizations.md))

- **scanner64** dipilih `create_session` pada LE 64-bit — E2E pipeline
  **×1,14–1,21** (512/64 KiB, satu proses) [`e408674`].
- **Indeks root lazy** — kunci jauh objek lebar menjadi O(1): 1024 field
  tengah **×195**, akhir **×366** (6589→34 ns, 12995→36 ns); objek ≤24
  field tak tersentuh; kunci ber-escape dikecualikan + fallback sekuensial
  demi semantik identik [`23a12ee`].
- **Streaming `array_next`** — pola baca satu lintasan kini berfungsi
  (dahulu tanpa `array_size` selalu 0); mesin `ArrayIterMode`; elemen
  angka terakhir (0 entri) ditangani penanda final-number + lookahead
  [`47e9cb7`].
- **Validasi UTF-8 terintegrasi** ke jalan scanner (menggantikan pass
  terpisah): payload UTF-8 padat **−42,7%** E2E (interleaved; ASCII tanpa
  regresi); sekuens multibyte aman terbelah di batas blok SWAR 8 byte.
  **Indeks elemen array lazy** untuk segmen `[index]`: 1024 elemen
  tengah **×40**, akhir **×75** [`8e0f3e3`].

### Diperbaiki

- **Getter JS menolak tipe salah** — `asString`/`asInt`/`asDouble`/
  `asBoolean` kini melempar `Error` berstatus `TYPE_MISMATCH` (dahulu:
  `asString` pada kursor habas melempar `RangeError` kabur; getter
  numerik memulangkan nilai sampah diam-diam).

### Dokumentasi

- README (id/en): tabel hasil 4 tahap menggantikan klaim lama "biaya
  linear object lebar"; `js/README.md`: kontrak getter + pola streaming.

### Uji

- 45 test native per preset (7 grup; +2 scanner, +4 path, +4 array),
  40 asersi smoke JS. *(Catatan: pesan commit rilis sempat menulis 53 —
  hitungan aktual per preset 45.)*

## [0.1.1] — 2026-10-22

### Diperbaiki

- **Payload >63,9 KB gagal `INVALID_UTF8` palsu** — stack + `.rodata`
  wasm menimpa payload; dipindah ke atas layout tetap
  (`--global-base=3153920`) [`01b129d`], plus regresi payload >64 KiB pada
  smoke test [`319578a`].

### Infrastruktur

- CI npm-publish memaksa Node 24 sebelum smoke test wasm [`10a4401`].
- `js/README.md` — halaman paket npm.

## [0.1.0] — 2026-10-22

Rilis perdana.

### Ditambahkan

- **Scanner SWAR + tape struktural**: scanner8/scanner64/scanner32,
  validasi UTF-8 (DFA Höhrmann + jalur cepat SWAR ASCII), kedalaman
  bracket, tape posisi struktural; angka 0 entri, literal 1 entri, string
  2 entri.
- **Lapis on-demand**: `Document`, `JsonValue`, `JsonObject`, `JsonArray`,
  `JsonIterator` — kursor forward-only, zero-copy, `skip_child`.
- **Arena bump-allocator** + `Session` C ABI (`create_session`,
  `find_field`, getter, `array_size`/`array_next`, path query
  `find_path`/`get_*_at`).
- **Binding JS Raw WebAssembly memory64** (tanpa Emscripten): kelas
  `ZeJson`/`Doc`, layout memori tetap, paket npm `@zuudevs/ze-json`,
  smoke test lintas ABI, contoh end-to-end.
- **Uji & benchmark**: 7 grup google-test + 3 grup google-benchmark +
  skrip agregasi metrik (JSON/Markdown ter-commit di `out/results/`).
- README bilingual (id/en), lisensi MIT.

[Belum dirilis]: https://github.com/zuudevs/ze_json/compare/v0.1.6...HEAD
[0.1.6]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.6
[0.1.4]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.4
[0.1.3]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.3
[0.1.2]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.2
[0.1.1]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.1
[0.1.0]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.0
