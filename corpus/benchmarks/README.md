# Korpus benchmark (`corpus/benchmarks/`)

Objek ukur benchmark — bukan fixture unit test (fixture kebenaran parser tetap di
`corpus/test/json_checker`). Desain lengkap: `PLANS.md` §4; aturan provenance:
`AGENTS.md` §4. Seluruh korpus **deterministik dan timestamp-free** — regenerasi
yang tak mengubah sumber menghasilkan byte identik, jadi hash manifest stabil.

## Isi korpus

| Nama | Isi | Format | Peran pengukuran |
|---|---|---|---|
| `synthetic_1k`, `synthetic_10k`, `synthetic_100k`, `synthetic_1mb` | tepat 1 dokumen per korpus, byte exact pada 1/10/100/1024 KiB | compact | body-parse mirip `express.json()`; jenjang sensitivitas ukuran |
| `saq_payload_v1` | 6 dokumen state-store SAQ (assistants, courses, lecturers, schedules, reports, config_global), data **anonim pool** | pretty indent-2 persis `JSON.stringify(data, null, 2)` | store-read; rootKey per fisika `repositories/index.js` (reports = array root, config_global = object root) |
| `checker_batch` | 25 dokumen valid kecil: `pass1–3.json` kopi byte-exact + 22 hasil generator (unicode, escape, container kosong, float/exponent, >53-bit) | campuran (kasus diambil apa adanya) | batch whole-document parse |

## MANIFEST.json & `corpus.sha256`

Setiap korpus punya `MANIFEST.json` (nama file, `bytes`, `sha256`, `source`, `role`).
**`corpus.sha256` pada measurement record = sha256 dari file `MANIFEST.json` itu
sendiri** — satu angka merangkum seluruh isi korpus. Schema record menuntut itu;
lihat `schemas/README.md`.

Tabel agregat di dalam manifest (`document_count`, `total_bytes`,
`max_document_bytes`) adalah field yang diisikan langsung ke record — jangan
dikalkulasi ulang manual; validator `verify` mengeceknya.

## Perintah

```sh
# verifikasi byte + sha256 + validitas JSON semua korpus (wajib lulus sebelum angka dipakai)
python scripts/corpus/make_corpus.py verify --all

# regenerasi penuh (tetap membutuhkan --force bila sudah ada — anti-rewrite senyap)
python scripts/corpus/make_corpus.py build all --seed 20261108 --force
```

Guard `verify` juga mengecek ulang berkas berprovenance `copied:` terhadap
file sumbernya (mis. `checker_batch/pass1.json` vs `corpus/test/json_checker/pass1.json`).

## Byte-stability

`corpus/**` dipin `-text` di `.gitattributes` root: `core.autocrlf=true` tidak
boleh mengubah akhir baris korpus, kalau tidak hash manifest jadi mismatch saat
clone/checkout di mesin lain. Jangan pernah menyimpan file korpus melalui editor
yang menambahkan trailing newline — gunakan generator.
