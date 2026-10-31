# Development: Integrasi Research (benchmark & test berprovenance)

> Bagian dari [docs/development/](.) — lihat juga
> [benchmarking.md](benchmarking.md) · [testing.md](testing.md) ·
> [workflow.md](workflow.md).

Dokumen ini menjelaskan bagaimana `benchmarks/` dan `tests/` direkam melalui
repo [`research/`](../../research/) agar angka di laporan/skripsi dapat
dipertanggungjawabkan. Artefak lama di `out/results/` tetap berguna untuk
eksplorasi; jalur formal memakai repo research.

Baru pertama kali? Mulai dari
[research-walkthrough.md](research-walkthrough.md) — panduan langkah-demi-langkah
dari shell bersih sampai angka yang bisa dikutip, termasuk tabel pemecahan masalah.

## Mengapa dua lokasi berbeda

Artefak di `out/results/` hanya berisi angka. Ia tidak mencatat commit source,
digest dataset, hash binary, hasil test pada commit yang sama, atau perintah
apa yang dijalankan. Karena itu artefak tersebut **tidak dapat membedakan**
"scanner64 lebih cepat" dari "scanner64 diukur pada dataset lain, binary basi,
atau suite test sedang gagal".

Repo `research/` mencatat semuannya dalam satu **run**:

```text
commit source + status submodule + working tree
        → perintah configure/build/run (persis seperti dideklarasikan)
        → digest dataset SAAT RUN (bukan saat analisis)
        → hash setiap executable
        → exit code + log stdout/stderr per langkah
        → artefak keluaran + SHA-256 + ukuran
        → gate korektness (run test VERIFIED pada commit sama)
        → manifest.json yang bisa diverifikasi ulang
```

`ze-research verify` menghitung ulang semua hash di atas; `ze-research ingest`
memasukkannya ke SQLite; `ze-research report` mengeluarkan angka beserta
`run_id`, fingerprint definisi, digest dataset, dan gate yang mendukungnya.

## Lingkungan

```powershell
$env:ZE_JSON_SOURCE_DIR = "D:\kuliah_\7_semester\skripsi\v3\src\ze_json"
$env:ZE_JSON_RESEARCH_HOME = "D:\kuliah_\7_semester\skripsi\research-data"   # DI LUAR kedua repo Git
$env:VCPKG_ROOT = "C:\vcpkg"    # dibutuhkan; runner menolak run bila kosong

cd research
py -3.11 -m venv .venv; .\.venv\Scripts\Activate.ps1
python -m pip install -e ".[dev]"
python -m pytest -q                     # 34 tes
ze-research validate                    # validasi seluruh eksperimen
```

Data root **harus** di luar repo induk maupun repo research. Jangan taruh di
`D:\kuliah_\7_semester\skripsi\v3\research-data` kalau folder `v3` itu sendiri
repo Git — build tree dan dataset 55 MB akan masuk Git.

## Snapshot hasil pertama

Dua run formal sudah terekam di `D:\kuliah_\7_semester\skripsi\research-data`:

| Run | Eksperimen | Status | Hasil |
|---|---|---|---|
| `20261030T091602-f2c7d18d11b1` | `correctness-gtest-suite` | COMPLETED / VERIFIED | **51 kasus lulus**, 8 biner di-hash, korpus 37 berkas cocok |
| `20261030T091653-1337119c867e` | `scanner-width-comparison` | COMPLETED / VERIFIED | 21 baris benchmark, **0 tak terpetakan**, gate menunjuk run korektness di commit sama `a206cbf1bf08` |

Angka yang dapat dikutip (median dari baris `run_type == "iteration"`):

| Workload | scanner8 (ns/op) | scanner64 (ns/op) | Rasio | Selisih |
|---|---:|---:|---:|---:|
| `tiny.json` | 154.80 | 219.66 | 1,419 | **scanner8 −41,9% lebih cepat** |
| `literal.json` | 335.76 | 601.17 | 1,790 | **scanner8 −79,0% lebih cepat** |
| `github_events.json` | 296.837 | 310.374 | 1,046 | scanner8 −4,6% lebih cepat |
| `synthetic-256k` | 263.005 | 265.560 | 1,010 | praktis imbang |
| `update-center.json` | 3.252.497 | 2.556.021 | 0,786 | **scanner64 −21,4% lebih cepat** |
| `jeopardy.json` | 62.040.284 | 40.641.836 | 0,655 | **scanner64 −34,5% lebih cepat** |

Arahnya **berbalik sesuai ukuran payload**, jadi klaim "{yang} lebih cepat"
tanpa menyebut workload tidak sesuai dengan data ini. `H1` pada definisi
(scanner64 latency lebih rendah) hanya benar untuk payload besar.

Sumber otoritatif adalah berkas laporan, bukan tabel di atas:

```powershell
Get-Content D:\kuliah_\7_semester\skripsi\research-data\reports\scanner-width-comparison-report.json
```

Laporan membawa `run_ids`, `definition_sha256`, `dataset_sha256`, hash
manifest, dan `gates` untuk setiap baris. Batch ini direkam dengan pasangan
`scanner8-library:scanner64-library` dan `--run-ids 20261030T091653-1337119c867e`.

Batas interpretasi: registrasi dijalankan berurutan dalam satu proses
(`same_process_sequential`), **bukan** interleaved per-iterasi; median diambil
dari baris iterasi karena baris agregat tidak direkam satu kali.

## Snapshot empat eksperimen tambahan

Semuanya berjalan pada commit `5352587` dengan run korektness VERIFIED pada commit
yang sama (gate mencatat run `20261030T145002-97508c84d608`), dan seluruh ingest
`--strict` lolos tanpa baris tak terpetakan.

| Eksperimen | Run | Baris | Hasil utama |
|---|---|---|---|
| `parser-ondemand` | `20261030T144043-c9f0e4fb40f7` | 9 | find field pertama vs terakhir pada object 512 field: **20,27 ns → 8.188,51 ns (rasio 404×)** — pencarian linear terkonfirmasi |
| `span-hybrid` | `20261030T145123-774a2abe752c` | 48 | hybrid vs replika pra-hybrid: menang di 5 dataset (−5,0% … −22,7%), **kalah** di twitter (+30,3%), update_center (+6,3%), jeopardy (+5,2%) |
| `session-api` (pasangan GRAM) | `20261030T162357-c0de848da815` | 64 | tape-walk **1,9×–3,8× lebih lambat** dari Base (+89%…+281%) — konsisten dengan catatan hasil negatif Tahap-7 |
| `js-parse-saq` | `20261030T151906-6ffee7ec5b7e` | 6 baris µs/op | median 0,829 µs (assistants) … 2,609 µs (globalConfig); digest tiap payload terekam, interpreter Node v24.19.0 |

Tiga temuan yang **membantah klaim sederhana**, dan karena itu justru layak dikutip:

1. **Span hybrid bukan perbaikan menyeluruh.** Arahnya bergantung dataset; pada
   twitter justru 30% lebih lambat. Klaim "probe-hybrid lebih cepat" tanpa
   menyebut dataset tidak sesuai data ini.
2. **Parser linear search terkonfirmasi kuantitatif**: 404× bukan 512×, karena
   pencarian berhenti saat kunci tengah/akhir ditemukan — bukan faktor tetap.
3. **Baseline JS tidak monoton terhadap ukuran**: `globalConfig` (261 B) lebih
   lambat dari `reports` (428 B). Struktur yang menentukan, bukan byte. Payload
   `assistants`/`schedules` juga punya koefisien variasi 46%/51%, jadi angka itu
   perlu dibaca bersama rentainya.

Catatan `js-parse-saq`: snapshot bertipe `synthetic_dummy` (satu rekaman dummy per
repositori), sehingga hasilnya **bukan** sampel beban kerja produksi SAQ Report.
Sumber otoritatif tetap berkas laporan di
`D:\kuliah_\7_semester\skripsi\research-data\reports\`.

## Snapshot repeat: bukti tiap klaim kini berdiri di 3 sampel

Sebelum bagian ini, kelima eksperimen performa hanya punya **satu** run masing-masing.
Pada commit `b0f8140` dijalankan satu run korektness plus `--repeat 3` untuk setiap
eksperimen performa, sehingga setiap klaim kini berdiri di tiga sampel pada satu
commit yang sama.

| Eksperimen | Run | Baris |
|---|---|---|
| `parser-ondemand` | `043940`, `044529`, `045120` | 9 × 3 |
| `js-parse-saq` | `045933`, `045940`, `045947` | 6 baris µs/op × 3 |
| `span-hybrid` | `050118`, `052802`, `055408` | 48 × 3 |
| `scanner-width-comparison` | `062120`, `063543`, `064959` | 21 × 3 |
| `session-api` | `070614`, `080019`, `111152` | 64 × 3 |

Ke-16 run itu semua `COMPLETED`/`VERIFIED`, semuanya pada
`source.git_commit = b0f81401…`, dan setiap run performa mencatat `gates` menunjuk
run korektness `20261031T043834-e7f9a0e761c3` pada **commit yang sama**.

### Apa yang repeat ubah dari kesimpulan

Inilah alasan sesi ini bernilai — bukan lagi "satu angka", melainkan "seberapa
konsisten, dan apakah arahnya bertahan":

| Perbandingan | Rasio (median antar-run) | Rentang | Arah bertahan di 3 run? |
|---|---:|---|---|
| **span**: prehybrid → hybrid, `jeopardy` | 0,988 | 0,963–0,997 | **ya** |
| **span**: prehybrid → hybrid, `twitter` | 0,992 | 0,908–0,998 | **ya** |
| **span**: prehybrid → hybrid, `canada` | 1,016 | 0,978–1,047 | tidak (menyeberangi 1) |
| **span**: prehybrid → hybrid, `github-events` | 1,015 | 0,985–1,017 | tidak |
| **span**: `citm-catalog` / `mesh` / `update-center` | 1,004 / 0,995 / 0,996 | 0,993–1,088 | tidak |
| **scanner**: scanner8 → scanner64, `update-center` | 0,693 | 0,675–0,874 | **ya** |
| **scanner**: scanner8 → scanner64, `github-events` | 0,834 | 0,811–0,959 | **ya** |
| **scanner**: scanner8 → scanner64, `tiny` | 1,067 | 1,058–1,071 | tidak (scanner8 lebih cepat) |
| **scanner**: scanner8 → scanner64, `literal` | 1,339 | 1,325–1,481 | tidak (scanner8 lebih cepat) |
| **scanner**: `jeopardy` | 0,875 | 0,786–1,293 | tidak (rentang sangat lebar) |
| **session**: gram Base → tapewalk | 1,861–3,021 | 1,653–3,137 | tidak — tapi **arah selalu tapewalk lebih lambat** |

Tiga hal yang hanya terlihat setelah pengulangan:

1. **`jeopardy` pada scanner bukan klaim yang kuat.** Sebelumnya satu run memberi
   −34,5%; dengan tiga run rentangnya 0,786–1,293 — menyeberangi 1. Ini bukan
   "scanner64 lebih cepat", ini "bervariasi, perlu lebih banyak sampel".
   `literal` (+1,339) dan `tiny` (+1,067) justru konsisten mengarah ke scanner8.
2. **`session` tapewalk memang jauh lebih lambat, dan itu konsisten.** Median
   1,861×–3,021× dengan rentang sempit per workload; arah "tapewalk lebih lambat"
   bertahan di **setiap run** untuk **setiap** workload. Ini klaim terkuat yang
   dimiliki data saat ini.
3. **Span hybrid hampir impar.** Rasio mengerucut ke sekitar 1,00 (0,988–1,016), dan
   hanya `jeopardy`/`twitter` yang bertahan satu arah. Klaim "probe-hybrid lebih
   cepat" tidak didukung data berulang; yang dapat diklaim adalah "tidak berbeda
   nyata pada mayoritas dataset, lebih baik pada dua yang besar".

### Noise floor dari run-run yang benar-benar identik commit-nya

Ini pertama kalinya lantai noise diukur dari run pada **commit yang sama**
(sebelumnya dua run scanner berada pada commit berbeda, sehingga sebarannya
tercampur perubahan kode):

| Eksperimen | Konteks | Median CV antar-run | Median rentang |
|---|---:|---:|---:|
| `parser-ondemand` | 9 | 8,9% | 17,6% |
| `span-hybrid` | 48 | 8,8% | 16,5% |
| `scanner-width-comparison` | 21 | 9,1% | 16,8% |
| `session-api` | 64 | 13,6% | 25,9% |

Bandingkan dengan rentang 15–45% yang terdokumentasi di
[benchmarking.md](benchmarking.md): rentang **terukur** dari run identik-commit
adalah 8,8–13,6% untuk CV — artinya sebagian besar jangkauan 15–45% itu berasal dari
perubahan lintas-commit/lingkungan, bukan dari komposer jam sesungguhnya pada
mesin ini. Ini kembali memperkuat aturan: selisih kecil dari satu run tetap tidak
dapat diklaim sebagai efek.

Catatan jujur: `session-api` memiliki 2 run `rep 1/3`+`rep 2/3` dan 1 run tunggal
(`111152`) — pengulangan ketiga terputus ketika batas waktu shell membunuh runner
setelah benchmark menulis JSON tetapi sebelum manifest ditulis. Run tanpa manifest
dibuang sesuai kontrak sistem, lalu diulang secara tunggal. Bidang `repeat` di
manifest menggambarkan hal ini apa adanya, dan agregasi tetap sah karena ketiga run
berada pada commit dan definisi yang sama.

## Membaca hasil lewat notebook

Notebook [`scripts/analyze_result/main.py`](../../scripts/analyze_result/main.py)
kini membaca **dua** sumber sekaligus: artefak lama `out/results/` **dan** data
root research. Jalankan:

```powershell
$env:ZE_JSON_RESEARCH_HOME = "D:\kuliah_\7_semester\skripsi\research-data"
$env:MPLBACKEND = "Agg"      # shell non-interaktif
python .\scripts\analyze_result\main.py
```

Yang berubah dari segi data, bukan sekadar tampilan:

- Setiap run research menjadi **satu "file" tersendiri** bagi pair engine, jadi
  sebuah pasangan tetap lahir di dalam satu run (kondisi same-process yang
  diinginkan), bukan campuran dua run.
- Kolom `provenance` memisahkan baris `research` dari `local`; `run_id` dan
  `revision` (commit dari manifest, bukan tebakan nama berkas) menempel pada
  setiap baris.
- Lantai noise mengelompokkan pengulangan satu eksperimen lewat `replica_group`
  dari manifest — bukan lagi menebak dari pola nama berkas.
- Baseline JS dimuat sebagai skema tersendiri (`research_js_parse`) agar baris
  µs/op tidak menyamar sebagai baris ns/op Google Benchmark.

### Lantai noise terukur (bukan diklaim) — lihat bagian repeat di atas

Notebook mengukur sebaran antar-run langsung dari dua run formal
`scanner-width-comparison` pada dua commit (`c7d7b6b0e7c0` dan `a206cbf1bf08`) —
21 konteks benchmark:

| Ukuran | Nilai |
|---|---|
| Median sebaran antar-run | **24,0%** |
| Rentang | 3,7% … 77,8% |
| Kuartil ke-10 / ke-90 | 5,5% … 51,6% |
| Konteks dengan sebaran > 45% | **6 dari 21** |

Dua implikasi praktis untuk skripsi: rentang 15–45% yang terdokumentasi memang
terukur, dan **ekornya nyata** — `BM_Scanner8_IntegratedReplica_Jeopardy` sampai
77,8% dan `BM_Scanner8_GithubEvents` 55,8%. Jadi selisih di bawah ±25% dari satu
run tidak dapat diklaim sebagai efek; itulah alasan `run --repeat N` plus
`report --across-runs` ada.

Catatan kejujuran: kedua run itu pada **commit berbeda**, jadi kelompok ini juga
memuat perubahan kode. Ukurannya tetap sah sebagai lantai noise karena setiap
pasangan dibandingkan dalam run yang sama.

## Eksperimen yang tersedia

| ID | Yang diukur | Catatan |
|---|---|---|
| `scanner-width-comparison` | `benchmarks/scanner_benchmark` — Scanner8/64 + 3 replika beku | 5 dataset nyata + payload sintetis + slot `Synth` replika (membaca `jeopardy.json`) |
| `correctness-gtest-suite` | 8 executable test, 51 kasus | Sumber data untuk gate korektness |

Definisi ada di `research/research/experiments/`. Keduanya **berstatus `draft`**
dengan referensi `unreviewed`: gerbang `--for-run` sengaja menolak sampai
manusia memeriksa dan menyetujui. Jangan menandai `reviewed`/`approved_for_run`
hanya agar run berhasil — itu administratif, bukan bukti.

## Urutan kerja yang benar

```powershell
# 1. Jalankan KOREKTNESS dulu (menghasilkan bukti test per commit)
ze-research run --experiment correctness-gtest-suite
ze-research ingest --run-id <RUN_ID>

# 2. Eksperimen performa sekarang lolos gate: butuh run test VERIFIED di commit sama
ze-research run --experiment scanner-width-comparison
ze-research ingest --run-id <RUN_ID> --strict

# 3. Angka + jejaknya
ze-research report --experiment scanner-width-comparison --pair scanner8-library:scanner64-library
ze-research export --experiment scanner-width-comparison    # untuk notebook
```

Urutan ini bukan formalitas. `scanner-width-comparison` mendeklarasikan
`correctness_gate` terhadap `correctness-gtest-suite`, sehingga run performa
**ditolak** bila tidak ada run test VERIFIED pada commit yang sama. Itu
mencegah klaim "lewat 20%" pada library yang suite test-nya gagah.

Penting: commit yang dibandingkan adalah commit **repo induk**, yang ikut
berubah ketika pointer submodule `research/` bergerak. Jadi setiap perubahan
kode atau definisi eksperimen membutuhkan run korektness baru. Susun semua
definisi dan perbaikan dulu, baru jalankan korektness, lalu performa.

`--strict` menolak ingest bila ada baris hasil yang tidak terpetakan ke tepat
satu varian. Lebih baik gagal ingest daripada punya angka yang salah label.

## Kontrak yang dicatat (dan yang tidak)

`protocol.design` di definisi berkata `same_process_sequential`. Semua varian
memang berjalan dalam satu proses, tetapi Google Benchmark menjalankan
registrasi **berurutan**, bukan meng-interleave iterasi antarvarian. Laporan
hanya boleh mengklaim perbandingan varian pada workload + kontrak ukur yang
sama; klaim yang butuh interleaved per-iterasi butuh harness lain.

Berbeda dengan itu, `benchmarks/span_iso_benchmark` memang mendaftarkan
pasangan bersebelahan (`BM_SpanReal_Hybrid_*` vs `BM_SpanReal_PreHybrid_*`) —
itu kuat secara urutan registrasi, tetap bukan interleave per-iterasi.

## Menambah eksperimen baru

1. Salin `research/research/experiments/benchmark/scanner-width.yaml`.
2. Isi `benchmark_name_pattern` dengan pola `…_{dataset}` dan beri
   `benchmark_token` pada **setiap** workload (mis. `GithubEvents`). Tanpa
   token, validasi menolak: pola liar bisa salah melabeli workload.
3. Isi `dataset_sha256` dari
   `python scripts/research/dataset-digests.py` (repo induk).
4. Pakai `execution.steps` bila perlu lebih dari configure/build/run —
   beri `id` unik per langkah; runner mencatat langkah mana yang menghasilkan
   file mana. Escape brace literal dengan `{{`/`}}`.
5. Untuk baseline JS, pakai `mode: script` (lihat
   [`benchmarks/json_baseline/README.md`](../../benchmarks/json_baseline/README.md));
   versi interpreter dicatat menggantikan hash binary.
6. Tetap `draft` sampai direview; baru set `approved_for_run`.

## Catatan build

`configure_command` pada definisi mereplikasi preset `native-release`
(clang, C++23, toolchain vcpkg) tetapi dengan `-B` menunjuk build tree
**eksternal** di bawah `ZE_JSON_RESEARCH_HOME`. Ini perlu karena preset
proyek menaruh `binaryDir` di dalam repo — yang dilarang aturan research, dan
juga akan mengotori working tree sehingga gerbang clean-worktree menolak run.

## Tiga penghalang sebelum run pertama

Ketiganya bukan warning: run ditolak sebelum satu perintah pun dijalankan.

1. **Working tree harus bersih.** `git status --porcelain --untracked-files=all`
   harus kosong. Repo Git bersarang di dalam checkout (mis. folder `thesis/`
   yang punya `.git` sendiri) muncul sebagai untracked dan memblokir run —
   abaikan lewat `.gitignore` atau pindahkan keluar checkout.
2. **Dataset harus ada dan sesuai digest.** Setiap `dataset_path` harus hash-nya
   sama dengan `dataset_sha256` yang tercatat. Submodule yang ter-checkout pada
   commit berbeda dianggap drift.
3. **Gate korektness harus terpenuhi.** Eksperimen performa menolak jalan bila
   belum ada run VERIFIED dari eksperimen korektness pada commit sama — dan
   pencariannya butuh database yang sudah ada (`ze-research init`).

Pastikan `VCPKG_ROOT` juga sudah diset; definisi memakai placeholder
`{vcpkg_root}` dan run ditolak bila kosong, bukan membangun toolchain rusak.
