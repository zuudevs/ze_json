# @zuudevs/ze-json

Binding JavaScript **Raw WebAssembly (memory64)** untuk [ze_json](https://github.com/zuudevs/ze_json) — parser JSON *on-demand* berbasis scanner SWAR + tape, tanpa Emscripten, tanpa glue runtime.

## Kenapa ze_json?

- **On-demand, bukan materialisasi penuh.** `JSON.parse` membangun seluruh pohon object; ze_json memindai payload sekali (SWAR) lalu Anda *menarik* field yang benar-benar Anda pakai — hemat alokasi & GC untuk dokumen besar.
- **Memori dapat dipakai ulang.** Arena & tape di-reset tiap `openDocument` — region tetap, tanpa churn heap JS.
- **Raw wasm64.** Pointer & size berupa `BigInt` (konsekuensi ABI 64-bit); memory64 sudah aktif bawaan di Node.js ≥ 24.

## Persyaratan

| Kebutuhan | Nilai |
|---|---|
| Node.js | ≥ 24 (memory64 aktif bawaan) |
| Payload maksimum | 1 MiB (region payload tetap di wasm) |
| Root dokumen | wajib **object** (root array belum didukung ABI) |

Untuk Node 23: jalankan dengan `node --experimental-wasm-memory64`.

## Instalasi

```sh
npm i @zuudevs/ze-json
```

## Pemakaian

```js
import { ZeJson } from "@zuudevs/ze-json";

const ze = await ZeJson.load(); // opsi: { path } atau { binary } untuk wasm kustom

const doc = ze.openDocument('{"id":21,"position":{"x":24,"y":48},"colors":["white","black"]}');

doc.find("id");        // → { found: true, type: "Number" }
doc.intAt("id").value; // → 21 (satu panggilan wasm)
doc.intAt("position.x").value;     // → 24 (path bertingkat)
doc.stringAt("colors[1]").value;   // → "black" (indeks array)
doc.typeAt("position").type;       // → "Object"

// String besar — posisikan kursor via find dahulu:
// doc.find("s"); const s = doc.asString();
```

Akses gagal **tidak melempar** — ia mengembalikan `status` ABI:

```js
doc.intAt("colors[9]").status;   // "NOT_FOUND"
doc.intAt("colors[0]").status;   // "TYPE_MISMATCH"
doc.intAt(".position").status;   // "INVALID_PATH"
```

Payload rusak melempar `Error` dengan kode status ABI (mis. `INVALID_JSON`, `UNEXPECTED_QUOTE`, `INVALID_UTF8`) — di-normalisasi sebagai pesan `scan gagal: STATUS=...`.

Getter per-field (`asString`/`asInt`/`asDouble`/`asBoolean`) kini **menolak tipe salah** dengan `Error` berstatus `TYPE_MISMATCH` (sejak v0.1.2) — sebelumnya `asString` pada kursor habis melempar `RangeError` kabur dari `TextDecoder`, dan getter numerik memulangkan nilai sampah diam-diam. Gunakan `typeOf()`/`find()` untuk memastikan tipe sebelum membaca; atau pakai path query (`intAt` dll.) yang mengembalikan `status` tanpa melempar.

Traversal array mengenal dua pola sejak v0.1.2 — kontrak **counted** lama (`arraySize()` sekali lalu loop `arrayNext()`) dan **streaming** baru (langsung `while (doc.arrayNext())` tanpa `arraySize()` — satu lintasan, hasil identik): lihat docstring `ze_json.mjs`.

## Benchmark

**Sapuan ukuran bertahap** — `js/benchmark/sizes.mjs` (payload sintetis
deterministik, satu proses, 6 workload × 7 ukuran; Node 24 · win32 ·
wasm64; artefak di `out/results/benchmark/js_sizes_*.md`). µs/op —
**lebih kecil = lebih cepat**:

| ukuran | JSON.parse | ze scan | baca 2 field: JSON | ze | iterasi penuh: JSON | ze |
|---|---|---|---|---|---|---|
| 95 B | 1,7 | 2,4 | 1,6 | 6,9 | 1,6 | 5,7 |
| 268 B | 3,8 | 2,9 | 3,6 | 8,2 | 4,3 | 21,1 |
| 1 KiB | 20,1 | 7,9 | 13,2 | **12,2** | 15,0 | 88,9 |
| 4 KiB | 38,6 | 14,2 | 31,9 | **16,7** | 36,4 | 204,8 |
| 16 KiB | 141,8 | 48,7 | 148,5 | **69,8** | 168,0 | 1217,3 |
| 64 KiB | 726,1 | 251,2 | 592,5 | **257,2** | 632,9 | 3955,6 |
| 256 KiB | 2640,4 | 944,4 | 2574,0 | **961,8** | 2703,1 | 15606,1 |

Cara membaca:

- **Rezim mungil (≲300 B): ze_json kalah** — overhead tetap (panggilan
  wasm + salin payload ke region memori tetap) belum teramortisasi;
  `JSON.parse` V8 memang tercepat di kelas ini.
- **Titik impas terukur**: scan murni menang mulai **268 B**; pola
  "parse + baca 2 field" menang mulai **1 KiB**, dan pada 256 KiB ze_json
  **~2,7× lebih cepat** (0,96 ms vs 2,57 ms).
- **Konsumsi penuh** (baca SEMUA item): `JSON.parse` menang di semua
  ukuran — ze_json parser *on-demand*; memmaterialisasi seluruh dokumen
  lewat path query per-item bukan medannya.

Benchmark korpus simdjson (mesh.json) tetap tersedia lewat `npm run bench`.

## Kontrak memori (untuk pemain wasm mentah)

| Region | Rentang |
|---|---|
| Scratch + keys | `0x000000..0x000400` |
| Payload (cap 1 MiB) | `0x000400..0x100400` |
| Tape (131.072 entries) | `0x100400..0x200400` |
| Arena | `0x200400..0x300400` |
| Stack + `.rodata` | ≥ `0x302000` (`--global-base=3153920`) |

Payload **harus** tak menyentuh di atas `0x100400` — region stack/konstanta sudah dipindah ke atas sana sejak v0.1.1 (payload > ~63,9 KB gagal `INVALID_UTF8` palsu pada v0.1.0).

## Tautan

- [Dokumentasi repositori](https://github.com/zuudevs/ze_json#readme)
- [Changelog](https://github.com/zuudevs/ze_json/releases)

## Lisensi

MIT © Rafi Indra Pramudhito Zuhayr
