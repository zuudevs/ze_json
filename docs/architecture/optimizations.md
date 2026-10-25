# Arsitektur: Optimisasi Bottleneck — 4 Tahap

> Bagian dari [docs/architecture/](.) — kembali ke
> [overview.md](overview.md).

## Metodologi (tahan noise)

Pengukuran performa di mesin pengembang ini hanya sah dengan dua disiplin:

1. **Perbandingan dalam satu proses (interleaved)** untuk pipeline — jam
   antar-proses bergetar 15–45%. Replika jalur lama (mis. pass validator
   terpisah) berjalan bersebelahan dengan jalur baru di run yang sama.
2. **Rasio dalam-run** untuk kueri berulang lintas iterasi — satu session
   dipakai lintas iterasi, rasio last/first yang dibandingkan.

Aturan lengkap, konvensi tooling, dan resep menambah benchmark ada di
[docs/development/benchmarking.md](../development/benchmarking.md).
Semua artefak before/after ter-commit di
[`out/results/benchmark/`](../../out/results/benchmark/).

## Empat tahap

| Tahap | Commit | Bukti utama (before → after) |
|---|---|---|
| **1** · scanner64 di `create_session` | `e408674` | E2E pipeline **×1,14–1,21** pada 512/64 KiB (satu proses, stabil dua run); scanner-only ×1,159 |
| **2** · indeks root lazy | `23a12ee` | kunci jauh objek lebar menjadi **O(1)**: 1024 field — tengah **6589 → 34 ns** (×195), akhir **12995 → 36 ns** (×366); objek ≤24 field tak tersentuh |
| **3** · streaming `array_next` | `47e9cb7` | pola baca satu-lintasan kini **berfungsi** (dulu `array_next` tanpa `array_size` selalu 0 — benchmark streaming skip semua); 1024 elemen **~0,83×** waktu pola counted; counted tak berubah |
| **4** · validasi UTF-8 terintegrasi + indeks elemen array | `8e0f3e3` | payload UTF-8 padat 64 KiB **−42,7%** E2E (DFA tak dijalankan dua kali; interleaved; ASCII tanpa regresi ~2%); path `items[K]` jauh: 1024 elemen — tengah **2594 → ~65 ns** (×40), akhir **5289 → ~71 ns** (×75) |

## Kebijakan indeks lazy (dipakai tahap 2 & 4)

Indeks TIDAK dibangun di `create_session` (itu akan membebani pipeline dan
kasus sekali-baca). Kebijakannya adaptif dan identik untuk indeks root
(hash-table di arena, verifikasi byte-exact) maupun indeks elemen array
(tabel dense posisi kursor):

```mermaid
flowchart TD
    A["kueri path berat<br/>(melewati kuota 24)"] --> B{"indeks untuk target ini<br/>sudah terbangun?"}
    B -- "ya" --> C["lompat O(1):<br/>seek ke posisi tersimpan + verifikasi"]
    B -- "belum" --> D{"kueri berat yang ke berapa?"}
    D -- "pertama" --> E["sekuensial murni —<br/>tanpa biaya indeks sama sekali"]
    D -- "kedua" --> F["bangun tabel sekali — O(lebar)"]
    F --> C
    E --> G["kembalikan hasil sekuensial"]
    C --> G
```

Jaminan semantik: hasil & status kueri identik di semua mode. Kunci
ber-escape dikecualikan dari indeks root (pencocokan sekuensial
membandingkan hasil unescape — teks mentah tidak ekuivalen) dan miss indeks
jatuh ke sekuensial penuh; duplikat kunci mempertahankan kemunculan
pertama; indeks elemen array terikat identitas array (tape idx `'['`) dan
dibangun ulang bila pindah target (blok arena dipakai ulang selama kapasitas
cukup).

## Sapuan ukuran JS: JSON.parse (V8) vs ze_json

`js/benchmark/sizes.mjs` — payload sintetis deterministik, satu proses,
isolasi metrik (payload & warm-up di luar pengukuran, SINK anti-DCE,
pasangan bersebelahan minTime identik). µs/op — **kecil = cepat**.
(angka di bawah PASCA optimisasi salin payload `encodeInto` — lihat
subbab berikutnya; artefak before di `js_sizes_ec34ee5.*`, after di
`js_sizes_073fc0f.*`)

| ukuran | JSON.parse | ze scan | baca 2 field: JSON | ze | iterasi penuh: JSON | ze |
|---|---|---|---|---|---|---|
| 95 B | 1,0 | **0,6** | **1,1** | 2,4 | **1,2** | 2,3 |
| 268 B | 3,4 | **1,1** | **2,6** | 2,9 | **3,2** | 9,2 |
| 1 KiB | 11,3 | **3,1** | 9,4 | **5,7** | **8,7** | 36,5 |
| 4 KiB | 34,7 | **9,7** | 31,4 | **11,3** | **31,3** | 108,3 |
| 16 KiB | 127,6 | **36,8** | 118,2 | **38,7** | **137,8** | 561,5 |
| 64 KiB | 714,5 | **184,7** | 529,9 | **163,4** | **534,0** | 1851,4 |
| 256 KiB | 2026,7 | **595,8** | 2340,0 | **599,3** | **2431,7** | 6722,0 |

Pembacaan hasil:

- **Scan murni menang di semua ukuran teruji** — termasuk rezim mungil
  (95 B: 0,57 vs 1,02 µs) sejak salin payload memakai `encodeInto`.
- **Pola "baca 2 field"**: rezim mungil (≲300 B) masih kalah — dua path
  query per op membawa overhead panggilan wasm; menang mulai **1 KiB**;
  pada 256 KiB **~3,9× lebih cepat**.
- **Konsumsi penuh** (baca SEMUA item): `JSON.parse` menang di semua
  ukuran — ze_json parser on-demand; materialisasi penuh lewat path
  query per-item bukan medannya.

## Optimisasi lanjutan: salin payload `encodeInto` (binding JS)

Di luar empat tahap C++, pengukuran sapuan mengungkap salinan payload JS
mendominasi E2E: `writeText` dulu memakai `TextEncoder.encode()`
(mengalokasi `Uint8Array` sementara) lalu `u8.set()` ke memori wasm —
**dua salinan + satu alokasi per dokumen**. `TextEncoder.encodeInto()`
menulis langsung ke view memori wasm (V8 punya jalur cepat khusus untuk
tujuan wasm memory) — satu salinan, nol alokasi sementara.

Bukti interleaved satu proses (`js/benchmark/copy.mjs` → artefak
`js_copy_*.json`; kedua strategi memuat biaya view + byte pengaman + cek
kontrak):

| teks | encode+set | encodeInto | rasio |
|---|---:|---:|---:|
| 11 B (skala key) | 0,84 µs | 0,20 µs | **×0,24** |
| 4 KiB | 4,68 µs | 0,28 µs | **×0,06** |
| 64 KiB | 53,55 µs | 2,25 µs | **×0,04** (24× lebih cepat) |
| 256 KiB | 147,74 µs | 11,43 µs | **×0,08** |
| 64 KiB UTF-8 padat | 239,08 µs | 174,07 µs | ×0,73 (encoding multibyte dominan) |

Efek E2E (sizes.mjs): ze scan 256 KiB **944 → 596 µs**, throughput
**~280 → ~440 MB/s**; titik impas scan murni turun ke **di bawah 95 B**
(dahulu 268 B); pola baca 2 field di 256 KiB naik ke **~3,9×** (dahulu
~2,7×). Regresi: smoke JS 40/40 (termasuk payload 66 KB ber-UTF-8 yang
kini lewat jalur `encodeInto`).

## Regresi menjaga tiap tahap

Setiap tahap diiringi uji regresi: **51 test native per preset** (8 grup —
scanner 10, document 2, number 3, object 5, session 4, array 7, path 14)
dan **40 asersi smoke JS**. Tahap 2: +4 aseri path; tahap 3: +4 aseri
array; tahap 4: +2 scanner (sekuens multibyte terbelah batas blok) +4 path
(indeks elemen array). Angka di pesan commit v0.1.2 sempat tertulis 53;
hitungan aktual 45 per preset — changelog memakai angka yang benar.

## Berkas terkait

- Benchmark native: [`benchmarks/session_benchmark/`](../../benchmarks/session_benchmark/)
- Benchmark JS: [`js/benchmark/sizes.mjs`](../../js/benchmark/sizes.mjs),
  [`js/benchmark/parse.mjs`](../../js/benchmark/parse.mjs)
- Artefak: [`out/results/benchmark/`](../../out/results/benchmark/)
  (mis. `arrayread_before.json`, `rootwide_pathquery_*`,
  `tahap4_interleaved.json`, `js_sizes_*.md`)
