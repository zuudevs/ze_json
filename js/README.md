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

## Benchmark

`JSON.parse` vs ze_json (benchmark.js · Node 24 · win32 — mesh.json 335 KB korpus simdjson):

| Teknik | ops/sec |
|---|---|
| `JSON.parse` | 651 ops/s |
| ze_json scan+find | ≈ 1,69 kops/s (≈ **2,6×** lebih cepat) |

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
