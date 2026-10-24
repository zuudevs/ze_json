# Changelog

Format mengikuti [Keep a Changelog](https://keepachangelog.com/id/1.1.0/);
versi mengikuti [SemVer](https://semver.org/). Rilis npm
`@zuudevs/ze-json` diterbitkan lewat CI pada tag `v*`.

## [Belum dirilis]

### Ditambahkan

- **Metadata versi tersinkron**: `project(VERSION 0.1.3)` di
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

[Belum dirilis]: https://github.com/zuudevs/ze_json/compare/v0.1.3...HEAD
[0.1.3]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.3
[0.1.2]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.2
[0.1.1]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.1
[0.1.0]: https://github.com/zuudevs/ze_json/releases/tag/v0.1.0
