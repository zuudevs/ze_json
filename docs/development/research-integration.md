# Development: Integrasi Research (benchmark & test berprovenance)

> Bagian dari [docs/development/](.) — lihat juga
> [benchmarking.md](benchmarking.md) · [testing.md](testing.md) ·
> [workflow.md](workflow.md).

Dokumen ini menjelaskan bagaimana `benchmarks/` dan `tests/` direkam melalui
repo [`research/`](../../research/) agar angka di laporan/skripsi dapat
dipertanggungjawabkan. Artefak lama di `out/results/` tetap berguna untuk
eksplorasi; jalur formal memakai repo research.

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
repok Git — build tree dan dataset 55 MB akan masuk Git.

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
mencegah klaim "lewat 20%" pada library yang suite test-nya gagal.

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
