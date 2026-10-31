# Development: Testing

> Bagian dari [docs/development/](.) — lihat juga
> [workflow.md](workflow.md) · [naming.md](naming.md) ·
> [benchmarking.md](benchmarking.md) ·
> [research-integration.md](research-integration.md) ·
> [research-walkthrough.md](research-walkthrough.md).

## Filosofi

1. **Uji = dokumentasi kontrak.** Ritme kursor (drain array, konsumsi
   `null`, pengampunan `']'`-sisa, forward-only `find`) dirumuskan di
   komentar header, lalu DIKUNCI oleh asersi — pembaca kode mendapat
   narasi di [docs/architecture/session.md](../architecture/session.md)
   dan bukti mesin di `tests/`.
2. **1 kelompok uji = 1 folder** — mudah diimprovisasi dan ditemukan;
   pendaftaran eksplisit di
   [`tests/CMakeLists.txt`](../../tests/CMakeLists.txt) lewat
   `add_test_target`.
3. **Setiap perubahan perilaku menuntut asersi baru di grup yang sama
   sebelum commit** — tahap 2/3/4 masing-masing datang dengan +4/+4/+6
   asersi regresi.
4. **Kedua sisi ABI diuji**: native (google-test) dan JS (smoke) —
   keduanya wajib hijau untuk commit perf/rilis.

## Peta grup native (51 test per preset × 2 preset)

| Grup | Jumlah | Cakupan |
|---|---:|---|
| `scanner_test` | 11 | tape vs scanner8/64 (kesetaraan + penolakan), edge buffer, validasi UTF-8 terintegrasi (sekuens multibyte TERBELAH di batas blok 8 byte: 2/3/4-byte, gap, escape), **kedalaman bracket bertipe** (mismatch jenis ditolak kedua varian; saling-silang `[{]}` = batas terdokumentasi, varian wajib setuju) |
| `document_test` | 2 | lapis on-demand `Document` mentah |
| `number_test` | 3 | parser angka (int/double, tepi format) |
| `object_test` | 5 | pencarian field, ritme kursor objek |
| `session_test` | 6 | `Session` end-to-end: create, find+getter, drain, **pemilihan varian scanner** (AUTO/SCALAR/WIDE setara), **version compatibility** (get_version == ZE_JSON_VERSION_NUMBER) |
| `array_test` | 7 | traversal counted, streaming=counted, angka 0-entri, ambigu `'[]'` vs `'[1]'`, over-drain, bauran pola |
| `path_test` | 14 | sintaks path, tipe-salah, indeks root lazy (4), indeks elemen array (4), kunci escape, dup-key pertama, nested |
| `json_checker_test` | 3 | pembanding REFERENCE json.org (Crockford) pada korpus resmi pass*/fail* — dijalankan pada **SETIAP varian scanner** platform (scanner8; scanner64 pada LE 64-bit): pass diterima semua validator; fail ditolak reference; tiap varian hanya wajib menolak subset **struktural** (`fail2/8/32/33` — fail33 tipe-bracket-tak-cocok tertangkap stack-bit v0.1.5) — 29 divergensi gramatikal terkunci sebagai batas desain scanner |

Korpus + sumber reference C (domain publik) diambil SEKALI lewat
[`scripts/install_JSON_checker/main.py`](../../scripts/install_JSON_checker/main.py)
(json.org): sumber → `third_party/JSON_checker/` (ter-commit),
korpus → `samples/JSON_Checker/` (submodule) — CI tidak butuh unduhan.
Baca komentar di
[`tests/JSON_checker_test/main.cpp`](../../tests/JSON_checker_test/main.cpp)
untuk metodologi ranah struktural vs gramatikal.

```bash
# Semua grup, satu preset
cmake --build build/native-release
ctest --test-dir build/native-release --output-on-failure

# Satu grup cepat
cmake --build build/native-release --target path_test
./build/native-release/tests/path_test.exe

# Laporan per grup (JSON gtest) → out/results/test/<grup>_<hash>.json
cmake --build build/native-release --target run_and_export_path_test_report
```

Agregasi + ringkasan Markdown lintas preset:

```bash
node scripts/test_metrics.js native-debug native-release
# → out/results/test/summary_<hash>.{json,md}
```

## Smoke JS (40 asersi lintas ABI)

`js/test/main.mjs` menguji binding end-to-end: semua tipe skalar + null,
traversal counted & streaming + drain, path query (benar/miss/
type-mismatch/invalid), penolakan payload rusak, payload besar ber-UTF-8,
penolakan getter pada tipe salah (v0.1.2), reusable region.

```bash
cd js
npm run build:wasm        # bila wasm berubah
# SALIN ARTEFAK LOKAL (gitignored!) — lihat workflow.md § gotcha
cp ../build/wasm64-release/src/ze_json_core.wasm .
npm test                  # 40 asersi
```

## Menambah uji — pola & jebakan

**Pola fixture** (lihat `PathTest` di `tests/path_test/main.cpp`):

```cpp
class PathTest : public ::testing::Test {
  protected:
    types::uint8_t buffer[1024 * 1024]{};      // arena 1 MB
    memory::Arena* arena_handle{nullptr};
    types::uint64_t tape_[256]{};               // KECIL — lihat catatan

    PathTest() noexcept : arena_handle(create_arena(buffer, sizeof(buffer))) {}

    api::Session* make_session(const char* payload, types::size_t n,
                                types::int32_t& err) { /* … */ }
};
```

**Jebakan yang sudah menampar (jangan diulang):**

- **Tape fixture 256 entri** — payload lebar (objek 128 field, array
  1024 elemen) TIDAK muat. Pakai pola vektor lokal seperti
  `RootWideIndexPathQueries`: buffer arena lokal +
  `std::vector<types::uint64_t> tape(json.size())` (kapasitas per-byte
  adalah batas atas aman).
- **`find()` forward-only** — setelah `find("missing")` kursor mati di
  `'}'`; kueri `find` berikutnya untuk field SEBELUM posisi itu gagal.
  Urutkan asersi maju sesuai urutan dokumen, atau buka dokumen baru /
  pakai path query (yang me-rewind sendiri).
- **Salah `path_len`** — `StringView(key, len)` byte-exact;
  `"after"` (5) dikirim len 6 membaca byte NUL → NOT_FOUND senyap.
  Hitung ulang setiap mengedit literal.
- **`null` harus dikonsumsi** (`consumeNull` di JS) sebelum find
  berikutnya — karakter `n` tersisa mengacaukan ritme.
- **Kunci escape** tidak pernah cocok dengan path teks-pintar (kontrak
  exact-byte) — asersi harus menguji KONSISTENSI antar mode, bukan
  kecocokan escape.

**Mendaftarkan grup baru:** buat folder `tests/<nama>_test/main.cpp`,
tambah `add_test_target(<nama>_test "${CMAKE_CURRENT_SOURCE_DIR}/<nama>_test/main.cpp")`
di `tests/CMakeLists.txt` — `gtest_discover_tests` otomatis mendaftarkan
tiap `TEST_F` ke ctest.

## Regresi wajib sebelum commit

```bash
# Native — semua grup, kedua preset bila menyentuh C++
ctest --test-dir build/native-release --output-on-failure
# Smoke JS — bila menyentuh C++ ATAU binding
cd js && npm test
```

Untuk perubahan performa, tambahkan benchmark before/after —
disiplinnya di [benchmarking.md](benchmarking.md).

## Bukti test yang bisa dipertanggungjawabkan

`ctest` menjawab "apakah hijau sekarang". Ia tidak menjawab "apakah hijau
pada commit yang menjadi dasar angka performa itu". Untuk yang kedua,
jalankan suite lewat eksperimen `correctness-gtest-suite` di repo
[`research/`](../../research/):

```powershell
ze-research run --experiment correctness-gtest-suite
ze-research ingest --run-id <RUN_ID>
```

Run itu merekam per kasuS: nama suite, nama kasus, status, dan durasi, plus
commit source, hash binary, dan digest korpus `samples/JSON_checker`.
Eksperimen performa di registry yang sama mendeklarasikan
`correctness_gate`, sehingga **ditolak** bila tidak ada run test VERIFIED pada
commit yang identik.

Efek praktisnya: angka performa dan bukti korektness terhubung lewat commit,
bukan lewat ingatan pelaku perubahan. Detail: 
[research-integration.md](research-integration.md).
