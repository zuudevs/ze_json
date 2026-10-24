# Referensi API

> Bagian dari [`docs/`](.) — arsitektur di
> [docs/architecture/](architecture/overview.md), changelog di
> [changelog.md](changelog.md).

Semua fungsi publik adalah C ABI murni (`NOEXCEPT`, konvensi pointer+panjang,
tanpa alokasi tersembunyi) dan tereksport ke wasm lewat `EXPORT_NAME`.
Sumber kebenaran: [`include/ze_json/ze_json.hpp`](../include/ze_json/ze_json.hpp).

## Status & ValueType

`Status` (int32, ABI-stabil):

| Kode | Nama | Arti |
|---:|---|---|
| 0 | `OK` | sukses |
| 1 | `EMPTY_JSON` | payload kosong |
| 2 | `INVALID_JSON` | struktur rusak / bracket tak seimbang / input kosong |
| 3 | `UNEXPECTED_QUOTE` | kutip tak tertutup / escape di ujung payload |
| 4 | `INVALID_QUOTE` | kutip di posisi ilegal |
| 5 | `INSUFFICIENT_BUFFER` | buffer tape/arena kurang |
| 6 | `INVALID_UTF8` | payload bukan UTF-8 valid |
| 7 | `NOT_FOUND` | jalur path tak sampai / field tak ada |
| 8 | `TYPE_MISMATCH` | nilai ditemukan tetapi tipe tak cocok dengan getter |
| 9 | `INVALID_PATH` | sintaks path salah |

`ValueType` (int32): `0 Array · 1 Object · 2 String · 3 Number · 4 Boolean ·
5 Null`. Nilai angka dideteksi lewat *gap* terminator (angka tidak
direkam tape).

## C ABI

### Arena

```c
Arena* create_arena(void* memory_ptr, size_t total_capacity);
void   reset_arena(Arena* arena);        // kembalikan seluruh blok ke nol
void   destroy_arena(Arena* arena);
size_t get_arena_used_space(Arena* arena);
```

### Scanner (langkah 0)

```c
void scanner8 (const char* src, size_t src_size,
               size_t* dst, size_t dst_size,
               size_t& result, int32_t& error);   // ok → result = jumlah entri
// scanner64: LE + 64-bit · scanner32: LE + 32-bit (tanda tangan sama)
```

### Session & pembacaan per-langkah

```c
Session* create_session(const char* src, size_t src_size,
                        uint64_t* tape, size_t tape_entries,
                        Arena* arena, int32_t& error);

int32_t  find_field(Session*, const char* key, size_t key_len); // 1 = ketemu
int32_t  value_type(Session*);                    // → ValueType

int64_t  get_int(Session*);
double   get_double(Session*);
int32_t  get_bool(Session*);                     // 1/0
void     get_string(Session*, const char*& out_data, size_t& out_len);

size_t   array_size(Session*);                    // kontrak counted (kursor di '[')
int32_t  array_next(Session*);                    // 1 = elemen tersedia, 0 = habis

void     destroy_session(Session*);               // no-op (arena-managed)
```

### Path query (satu panggilan: navigasi + cek tipe + baca)

```c
int32_t  find_path(Session*, const char* path, size_t path_len);   // → Status

int64_t  get_int_at   (Session*, const char* path, size_t len, int32_t& err);
double   get_double_at(Session*, const char* path, size_t len, int32_t& err);
int32_t  get_bool_at  (Session*, const char* path, size_t len, int32_t& err);
void     get_string_at(Session*, const char* path, size_t len,
                       const char*& out_data, size_t& out_len, int32_t& err);
```

Sintaks path: `key`, `[index]`, pemisah `.` — dapat dirantai:
`position.x`, `colors[1]`, `items[3].name`, `mix[4][1]`.

## Contoh C++

```cpp
using namespace zuu;

memory::Arena* arena = create_arena(buffer, sizeof(buffer));

types::size_t tape[512];
api::Status st = api::OK;
api::Session* s = create_session(payload, len, tape, 512, arena, st);

// Alur per-langkah (kursor forward-only)
find_field(s, "colors", 6);
if (value_type(s) == int32_t(api::ValueType::Array)) {
    while (array_next(s) == 1) { /* value_type(s) → get_*(s) */ }
}

// Path query — kursor selalu di-rewind dari root
int32_t err = api::OK;
types::int64_t x = get_int_at(s, "position.x", 10, err);

// Lapis on-demand mentah (tanpa Session)
ondemand::Document doc(payload, tape, tape_len, arena);
auto found = doc.get_root().get_object().find_field({"colors", 6});

destroy_arena(arena);
```

Kontrak kursor (drain array, konsumsi `null`, rewind setelah miss)
didokumentasikan lewat uji — baca komentar di
[`tests/session_test/`](../tests/session_test/) dan
[`tests/array_test/`](../tests/array_test/); narasi lengkap di
[architecture/session.md](architecture/session.md).

## JavaScript (`@zuudevs/ze-json`)

```js
import { ZeJson } from "@zuudevs/ze-json";

const ze  = await ZeJson.load();            // opsi: { path } / { binary }
const doc = ze.openDocument(jsonText);      // melempar bila payload rusak
ze.close();                                 // rilis arena di akhir siklus
```

### `Doc` — per-field (kursor bergerak maju)

| Metode | Hasil | Catatan |
|---|---|---|
| `find(key)` | `{ found, type }` | `type` = nama `ValueType` atau `undefined` saat tak ketemu |
| `typeOf()` | nama tipe | `"Array" "Object" "String" "Number" "Boolean" "Null"` |
| `asString()` / `asInt()` / `asDouble()` / `asBoolean()` | nilai (`String`/`BigInt`/`number`/`boolean`) | **melempar** `STATUS=TYPE_MISMATCH` bila tipe kursor tak cocok (v0.1.2) |
| `arraySize()` | `Number` | kontrak counted — kursor wajib di `'['` |
| `arrayNext()` | `boolean` | streaming tanpa `arraySize` pun sah; **wajib drain sampai `false`** |
| `consumeNull()` | `null` | majukan kursor melintas literal `null` |

### `Doc` — path query (rewind sendiri, tidak melempar)

| Metode | Hasil |
|---|---|
| `intAt(path)` / `doubleAt(path)` / `boolAt(path)` | `{ status, value }` |
| `stringAt(path)` | `{ status, value }` (zero-copy — konsumsi sebelum dokumen berikutnya) |
| `typeAt(path)` | `{ status, type }` |

```js
doc.intAt("position.x");        // → { status: "OK", value: 24 }
doc.stringAt("colors[1]");      // → { status: "OK", value: "black" }
doc.intAt("colors[9]");        // → { status: "NOT_FOUND", value: 0 }
doc.intAt("colors[0]");        // → { status: "TYPE_MISMATCH", value: 0 }
```

## Konstanta binding

`STATUS` (nama status urut kode), `VALUE_TYPE` (nama tipe urut kode), dan
konstanta layout region (`KEY_BASE`, `PAYLOAD_BASE`, `TAPE_BASE`,
`ARENA_BASE`, dst.) diekspor dari
[`js/ze_json.mjs`](../js/ze_json.mjs) — detail layout di
[architecture/js-binding.md](architecture/js-binding.md).
