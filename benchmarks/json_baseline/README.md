# SAQ Report JSON.parse baseline

Benchmark ini mengukur latency dan throughput `JSON.parse(rawPayload)` atas enam
payload serial dari repositori SAQ Report. Harness-nya milik repo `ze_json`;
sumber data aplikasinya adalah checkout `saq-report` terpisah.

**Yang diukur:** satu operasi — `JSON.parse` atas byte payload yang persis
terdistage ke dalam run directory.
**Yang TIDAK diukur:** `ze_json`, `cloneJson()` penuh, pemuatan database, akses
jaringan MongoDB, maupun transfer HTTP. Lihat [Semantik pengukuran](#semantik-pengukuran).

## Prasyarat

| Kebutuhan | Versi minimum | Catatan |
|---|---|---|
| Node.js | ≥ 18 | `feeder.js` memakai `fetch()` bawaan; lingkungan dev memakai ≥ 24 |
| `git` | bebas | generator mencatat revisi `saq-report` |
| checkout `saq-report` | — | hanya untuk mensuplai `routes/_helpers.js` `generateId()` (snapshot dummy) |

## Tata letak repositori

```text
ze_json/                              ← repo induk (Git)
├── benchmarks/json_baseline/
│   ├── capture-samples.js            tangkap snapshot nyata (butuh .env + MongoDB)
│   ├── feeder.js                     kirim payload ke receiver & picu pengukuran
│   ├── run-baseline.js               orkestrator: receiver → feeder → finalisasi
│   ├── server.js                     receiver HTTP + penghitung waktu
│   └── README.md                     berkas ini
├── samples/                          ← SUBMODULE Git (ze_json_sample), bukan direktori biasa
│   └── saq-report/
│       ├── assistants.json
│       ├── courses.json
│       ├── lecturers.json
│       ├── schedules.json
│       ├── reports.json
│       ├── globalConfig.json
│       └── manifest.json             digest, ukuran, tipe root, profil struktur
└── scripts/saq-dummy-generator.js    bangkitkan snapshot dummy
```

> **`samples/` adalah submodule Git.** Setiap berkas di dalamnya dilacak oleh
> repositori `ze_json_sample`, bukan oleh `ze_json`. Konsekuensinya ada di
> peringatan berikut.

## Peringatan: regenerasi snapshot mengotori submodule dan memblokir run formal

`scripts/saq-dummy-generator.js` dan `capture-samples.js` menulis ke
`samples/saq-report/`, yaitu **di dalam submodule `samples`**. Menjalankan salah
satu skrip itu membuat pointer submodule kotor, dan `ze_json_research` menolak
setiap run formal dengan pesan yang tampak tak berhubungan:

```text
ERROR: Run formal memerlukan working tree bersih. Commit atau simpan perubahan
dengan strategi snapshot yang eksplisit terlebih dahulu.
```

Pemulihan (setelah Anda sengaja mengubah snapshot):

```powershell
git -C samples add -A
git -C samples commit -m "chore(samples): perbarui snapshot saq-report"
git add samples
git commit -m "chore: pin samples ke snapshot baru"
```

Dua konsekuensi yang mudah terlewat:

1. **Angka `dataset_sha256` di definisi eksperimen ikut usang.** Runner menghash
   ulang dataset sebelum menjalankan perintah dan **membatalkan run** bila tidak
   cocok. Perbarui nilai di
   `research/research/experiments/baseline/js-parse-saq.yaml` (sumber: field
   `payload_sha256` di `samples/saq-report/manifest.json`) sebelum run berikutnya.
2. **Commit tadi menggeser commit induk.** Gerbang korektness membandingkan commit
   induk, jadi run korektness harus dijalankan ulang sebelum run performa boleh
   berjalan lagi. Aturan lengkap: ../../research/docs/data-integrity.md dan
   [research-integration.md](../../docs/development/research-integration.md).

Snapshot yang ada saat ini bertipe `synthetic_dummy` — lihat peringatannya di
bawah.

## Siapkan snapshot dummy

Jalankan dari root repo `ze_json`. `SAQ_REPORT_ROOT` menunjuk ke root checkout
`saq-report` karena generator mengimpor implementasi `generateId()` asli dari
`routes/_helpers.js`.

```powershell
# Default bila SAQ_REPORT_ROOT tidak diset: <repo>/../../saq-report
# (pada checkout penulis ini: D:\kuliah_\7_semester\skripsi\v3\src\saq-report)
$env:SAQ_REPORT_ROOT = "D:\kuliah_\7_semester\skripsi\v3\src\saq-report"
node .\scripts\saq-dummy-generator.js
```

Ini menulis tepat enam berkas payload JSON ringkas plus
`samples/saq-report/manifest.json`. Lima array repositori berisi satu rekaman
dummy yang saling tertaut; `globalConfig` berupa objek. Manifest menandai snapshot
sebagai `synthetic_dummy` dan mencatat `snapshot_id`, `captured_at_utc`, revisi
Git `saq-report`, serta `payload_sha256` tiap berkas.

> **Data dummy hanya untuk uji integrasi dan karakterisasi parser. Bukan sampel
> representatif dari data produksi SAQ Report.** Keterbatasan ini sudah disalin
> ke `limitations` definisi eksperimen, sehingga ikut terbawa ke setiap laporan
> yang dikutip dari run ini.

## Tangkap snapshot nyata

Pakai `SAQ_REPORT_ROOT` yang sama menunjuk ke checkout `saq-report`. Pastikan
`.env` dan konfigurasi MongoDB di sana valid.

```powershell
$env:SAQ_REPORT_ROOT = "D:\kuliah_\7_semester\skripsi\v3\src\saq-report"
node .\benchmarks\json_baseline\capture-samples.js
```

Skrip ini memanggil `connectDB()`, `initRepositories()`, dan metode `read()`
masing-masing repositori. Ia **tidak menulis** ke database. Berkas sampel ditulis
sebagai byte `JSON.stringify(value)` UTF-8 ringkas, tanpa newline tambahan.
Manifest merekam SHA-256, panjang byte, tipe root, dan profil struktur tiap
payload, serta revisi Git `saq-report`.

Snapshot nyata dapat memuat data pribadi atau operasional. Simpan secara lokal dan
jaga agar tidak masuk riwayat Git publik; perlakukan `samples/saq-report/`
sesuai aturan penanganan data proyek.

## Variabel lingkungan

Semua variabel yang dipakai kode, bukan hanya yang biasa diset:

| Variabel | Default | Keterangan |
|---|---|---|
| `SAQ_BENCH_WARMUP_ITERATIONS` | `100` | iterasi pemanas sebelum batch terukur |
| `SAQ_BENCH_ITERATIONS` | `200` | iterasi per batch terukur (`iterations_per_sample`) |
| `SAQ_BENCH_ROUNDS` | `20` | jumlah batch terukur (`sample_count`) |
| `SAQ_BENCH_HOST` | `127.0.0.1` | host receiver |
| `SAQ_BENCH_PORT` | `8787` | port receiver |
| `SAQ_BENCH_MAX_PAYLOAD_BYTES` | 64 MiB | batas ukuran payload yang diterima receiver |
| `SAQ_BENCH_RUN_ID` | dibangkitkan | ID run; divalidasi terhadap pola aman |
| `SAQ_BENCH_SHUTDOWN_TOKEN` | dibangkitkan | token acak untuk perintah shutdown |
| `SAQ_BENCH_URL` | diturunkan | URL dasar yang dipakai feeder |
| `SAQ_BENCH_SAMPLE_DIR` | diturunkan | direktori input yang sudah distage |
| `SAQ_BENCH_DATA_DIR` | `~/ResearchData/saq-report-json-baseline` | akar direktori run bila `RUN_DIR` tak diset |
| `SAQ_BENCH_RUN_DIR` | `<DATA_DIR>/runs/<RUN_ID>` | direktori keluaran run |

**Tiga variabel terakhir ditimpa oleh `run-baseline.js`.** Wrapper menyetel
`SAQ_BENCH_DATA_DIR`, `SAQ_BENCH_RUN_DIR`, dan `SAQ_BENCH_RUN_ID` dari argumen
`--run-dir`/`--run-id` yang diberi runner, dan membangkitkan
`SAQ_BENCH_SHUTDOWN_TOKEN` sendiri. Itu sebabnya pemeriksaan di `server.js`
("`SAQ_BENCH_RUN_DIR` harus berada di dalam `SAQ_BENCH_DATA_DIR`") selalu lolos di
jalur formal, dan **mengedit ketiga variabel itu dari shell tidak berpengaruh**
ketika dijalankan lewat wrapper.

Dari daftar di atas, hanya lima variabel pertama yang perlu — dan sebaiknya wajib
— dideklarasikan pada `execution.env_passthrough` agar nilainya terekam di
manifest run. `SAQ_BENCH_MAX_PAYLOAD_BYTES` tidak dicatat; ia hanya pengaman, tidak
mengubah apa yang diukur.

## Jalankan melalui ze_json_research

Runner research memakai **mode `script`** dan memanggil `run-baseline.js`. Wrapper
itu memang menjalankan keseluruhan orkestrasi: menyalakan receiver HTTP, menunggu
health check, menjalankan feeder, memverifikasi finalisasi, lalu mematikan
receiver. Direktori run eksternal dari research diberikan ke receiver, sehingga
artefak hasil ikut dikumpulkan ke manifest research terluar.

Satu run merekam tiga hal sekaligus yang tidak bisa disintesis kemudian: byte
input yang persis (distage ke `inputs/saq-report/`), versi interpreter Node, dan
nilai variabel pengukuran.

Atur data root research di luar kedua repositori Git:

```powershell
$env:ZE_JSON_SOURCE_DIR  = (Get-Location).Path          # root repo ze_json
$env:ZE_JSON_RESEARCH_HOME = "D:\kuliah_\7_semester\skripsi\research-data"
$env:SAQ_BENCH_WARMUP_ITERATIONS = "100"
$env:SAQ_BENCH_ITERATIONS = "200"
$env:SAQ_BENCH_ROUNDS = "20"

cd research
py -3.11 -m venv .venv; .\.venv\Scripts\Activate.ps1
python -m pip install -e ".[dev]"
ze-research init
```

Definisi lengkap ada di
`research/research/experiments/baseline/js-parse-saq.yaml`. Blok `execution`-nya:

```yaml
  execution:
    preset: null
    mode: script
    require_clean_worktree: true
    steps:
      - id: run
        command:
          - node
          - "{source_dir}/benchmarks/json_baseline/run-baseline.js"
          - --run-dir
          - "{run_dir}"
          - --run-id
          - "{run_id}"
    env_passthrough:
      - SAQ_BENCH_ITERATIONS
      - SAQ_BENCH_WARMUP_ITERATIONS
      - SAQ_BENCH_ROUNDS
      - SAQ_BENCH_PORT
      - SAQ_BENCH_HOST
    expected_outputs:
      - baseline-server/manifest.json
      - raw/benchmark_results.jsonl
      - metadata/saq-report-sample-manifest.json
      - inputs/saq-report/manifest.json
      - inputs/saq-report/assistants.json
      - inputs/saq-report/courses.json
      - inputs/saq-report/lecturers.json
      - inputs/saq-report/schedules.json
      - inputs/saq-report/reports.json
      - inputs/saq-report/globalConfig.json
```

Perhatikan dua perbedaan dari eksperimen C++:

- **Mode `script` tidak butuh `VCPKG_ROOT`** dan tidak menjalankan
  configure/build. Identitas biner diganti identitas interpreter: runner
  mendaftarkan versi Node (`node --version`) ke `build.interpreter_version`.
- **Ada `correctness_gate`.** Definisi ini hanya bisa berjalan bila sudah ada run
  `correctness-gtest-suite` berstatus VERIFIED pada commit source yang sama.
  Jadi urutannya: korektness dulu, baru baseline JS. Detailnya di
  [research-integration.md](../../docs/development/research-integration.md).

Definisi tetap harus direview dan disetujui (`approved_for_run`, semua referensi
`reviewed`) sebelum run formal. Snapshot dummy tidak boleh diperlakukan sebagai
beban kerja aplikasi yang representatif.

## Ke mana hasilnya pergi

Satu run menghasilkan `raw/benchmark_results.jsonl` — **satu baris per payload**,
bukan satu baris per iterasi. Baris itu sudah diringkas oleh harness (min, median,
mean, stdev, max, CV%, jumlah sample, daftar `sample_us_per_op`), dan membawa
`payload_sha256`, `sample_manifest_sha256`, `snapshot_id`, dan
`payload_bytes_utf8`.

Runner research menghash seluruh berkas itu sebagai artefak run. Saat ingest:

- enam baris masuk ke tabel **`js_parse_results`**, bukan `benchmark_results`;
- satuannya **µs/op**, sengaja terpisah dari ns/op Google Benchmark supaya
  seseorang tidak dapat merata-rata median JS dengan baris iterasi C++;
- laporan ditulis ke
  `$ZE_JSON_RESEARCH_HOME\reports\js-parse-saq-report.json`, memuat `run_ids`,
  fingerprint definisi, digest dataset, versi interpreter, dan gate.

## Smoke test langsung

Tanpa melewati CLI research formal, wrapper bisa diuji terhadap snapshot dummy
yang ada. Ini **bukan run formal**: tidak ada manifest research, tidak ada
verifikasi artefak, dan tidak ada ingest.

```powershell
$env:SAQ_BENCH_WARMUP_ITERATIONS = "5"
$env:SAQ_BENCH_ITERATIONS = "20"
$env:SAQ_BENCH_ROUNDS = "3"
node .\benchmarks\json_baseline\run-baseline.js `
  --run-dir "$env:TEMP\saq-baseline-smoke" `
  --run-id "saq-baseline-smoke"
```

Aman memakai `$env:TEMP` karena wrapper menyetel `SAQ_BENCH_DATA_DIR` ke run dir
itu sendiri. Keluaran yang diharapkan:

```text
%TEMP%/saq-baseline-smoke/
├── baseline-server/manifest.json          manifest receiver + hash artefak
├── raw/benchmark_results.jsonl           6 baris hasil terukur
├── inputs/saq-report/                    byte payload yang persis + manifest
└── metadata/saq-report-sample-manifest.json
```

## Semantik pengukuran

- **Operasi:** `JSON.parse(rawPayload)` saja.
- **Region terukur:** pemanggilan `JSON.parse` berulang di dalam setiap batch
  terukur.
- **Dikecualikan:** transfer HTTP dan pengumpulan body, verifikasi SHA-256,
  pemeriksaan korektness, profil struktural, pemanasan, serialisasi hasil, dan
  penulisan berkas.
- **`sample_us_per_op`:** waktu batch dibagi jumlah iterasinya. Bukan latency satu
  pemanggilan yang distimestamp terpisah.
- **Median dan mean:** dalam mikrosekon per operasi. Throughput dihitung dari mean
  dan jumlah byte UTF-8 payload.
- **`sample_count`:** jumlah batch terukur per payload.

Batas interpretasi yang perlu dibawa saat mengutip angka: hasil ini **tidak**
mengukur `ze_json`, `cloneJson()`, pemuatan database, atau akses jaringan
MongoDB. Perbandingan antar-payload dalam satu run sahih karena berbagi proses,
port, dan konfigurasi iterasi; angka absolut lintas run justru bergantung pada
getaran clock yang sudah terdokumentasi
[di benchmarking.md](../../docs/development/benchmarking.md), jadi kutip bersama
`run_id` dan `manifest_sha256`.

## Membaca hasilnya

```powershell
$runId = "20261030T151906-6ffee7ec5b7e"    # ganti dengan run Anda
ze-research report --experiment js-parse-saq --run-ids $runId
ze-research export  --experiment js-parse-saq
```

Contoh keluaran snapshot `synthetic_dummy` saat ini (Node v24.19.0, median µs/op):

| Payload | Byte | Median µs/op | CV% |
|---|---:|---:|---:|
| `assistants` | 140 | 0,829 | 46,3 |
| `lecturers` | 163 | 0,884 | 41,4 |
| `courses` | 201 | 1,024 | 4,6 |
| `schedules` | 192 | 1,271 | 50,9 |
| `reports` | 428 | 1,632 | 17,1 |
| `globalConfig` | 261 | 2,609 | 15,9 |

Dua hal yang harus dibaca bersama tabel di atas: **latency tidak monoton terhadap
ukuran** (`globalConfig` 261 B lebih lambat dari `reports` 428 B — struktur yang
menentukan, bukan byte), dan `assistants`/`schedules` punya koefisien variasi
46%/51% sehingga angka keduanya perlu dikutip bersama rentangnya. Sumber
otoritatif adalah berkas JSON laporan, bukan tabel ini.
