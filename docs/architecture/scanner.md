# Arsitektur: Scanner & Tape

> Bagian dari [docs/architecture/](.) — kembali ke
> [overview.md](overview.md).

## Peran scanner

Scanner adalah satu-satunya komponen yang menyentuh seluruh payload. Tugasnya:

1. **Memvalidasi** payload: kutip string (termasuk pasangan escape),
   keseimbangan bracket, dan **UTF-8** (DFA Höhrmann, sejak Tahap 4
   terintegrasi langsung ke jalan pemindaian — bukan pass terpisah).
2. **Merekam tape**: daftar posisi byte setiap karakter struktural
   (`{ } [ ] , :` dan tanda kutip string), sebagai offset `uint64` dari
   awal payload.

Scanner **tidak membangun tree** dan **tidak mem-parse nilai** — angka,
`true/false/null` dilewati begitu saja; nilainya dihitung kemudian oleh
lapisan on-demand dari teks di antara entry tape.

## Tiga varian, satu format tape

| Varian | File | Strategi UTF-8 | Kedalaman bracket |
|---|---|---|---|
| `scanner8` | `scanner/scalar.cpp` | **Pass validator TERPISAH** (SWAR blok 8-byte) — loop skalar tidak menghasilkan apa-apa dari DFA inline | Stack-bit bertipe |
| `scanner64` | `scanner/parallel.cpp` | **Terintegrasi** ke jalan pemindaian (blok SWAR; DFA hanya saat non-ASCII) | Stack-bit bertipe |
| `scanner32` | `scanner/parallel.cpp` | Pass validator terpisah (fallback 32-bit, tak teruji di rig ini) | Stack-bit bertipe |

Pemilihan di `create_session` via makro `__LITTLE_ENDIAN__`/`__IS_64_BIT__`;
varian eksplisit via `create_session_with_scanner` (v0.1.5+, gate
`ZE_JSON_VERSION_NUMBER >= 2026000105`). **Format tape ketiganya
identik** — dijamin oleh uji kesetaraan `tests/scanner_test/`.

**Mengapa dua strategi UTF-8?** Loop skalar step-by-step tidak
mendapat keuntungan dari cek DFA inline — setiap byte membayar uji
`utf8 != 0 || high-bit` yang tidak pernah teramortisasi (terukur
**−27..43%** pada korpus, replika interleaved
`scanner_separation_proof.json` menunjukkan pemulihan **×1,9**).
Sebaliknya, scanner64 memproses string per blok 8-byte: satu uji
high-bit per blok cukup, dan DFA hanya menyala pada blok non-ASCII
— integrasi MENANG (−42% pada payload UTF-8-padat Tahap 4, ASCII
nyaris gratis). Filosofi Langdale & Lemire 2019 / branchfree.org:
kerja blok-reguler dan kerja langkah-demi-langkah adalah DUA
algoritma — jangan digabungkan.

## Format tape

| Elemen JSON | Entri tape | Catatan |
|---|---|---|
| Karakter struktural (`{ } [ ] , :`) | **1** | posisi byte karakter |
| String | **2** | posisi kutip pembuka & penutup |
| `true` / `false` / `null` | **1** | posisi byte awal literal |
| Angka | **0** | teks angka hidup di *gap* antara dua entri — kursor elemen angka justru berada di terminatornya (`,` atau `]`/`}`) |

Konsekuensi angka 0-entri inilah sumber seluruh kehalusan kursor (lihat
[session.md](session.md)): `[]` dan `[1]` menghasilkan tape yang serupa dan
dibedakan lewat *gap* teks (satu-satunya konten non-whitespace berarti
ada elemen angka).

## Validasi UTF-8 terintegrasi (Tahap 4)

Dulu `utils::Utf8Validator` berjalan sebagai pass penuh TERPISAH sebelum
pemindaian — payload diwalk dua kali pada setiap `create_session`. Sejak
Tahap 4, langkah DFA menumpang jalan scanner:

```mermaid
flowchart TD
    A["byte payload berikutnya"] --> B{"high-bit menyala<br/>atau DFA di tengah sekuens?"}
    B -- "tidak — ASCII murni" --> C["jalur cepat:<br/>blok SWAR 8 byte / tabel struktural"]
    B -- "ya" --> D["DFA UTF-8 per byte<br/>(tabel kUtf8Dfa)"]
    D --> E{"state = REJECT?"}
    E -- "ya" --> F["tolak payload: INVALID_UTF8"]
    E -- "tidak" --> C
    C --> G["lanjut pemindaian / rekam tape"]
    G --> A
```

Aturan integrasi:

- Byte ASCII dengan state DFA **ACCEPT** tidak menyentuh DFA sama sekali
  (transisi no-op) — biaya validasi murni ASCII nyaris nol.
- Blok 8-byte murni-ASCII + state ACCEPT tetap memakai jalur SWAR lama;
  blok yang memuat high-bit (atau saat DFA di tengah sekuens) diproses
  per-byte sehingga **sekuens multibyte aman terbelah di batas blok**.
- Byte struktural dan pasangan escape juga masuk DFA — kutip penutup di
  tengah sekuens wajib ditolak, identik dengan validator lama.
- Sekuens terpotong di akhir payload → `INVALID_UTF8`.

**Perubahan perilaku yang disengaja**: untuk payload yang sakit ganda
(struktural DAN UTF-8), galat yang dilaporkan kini bisa berbeda jenis dari
versi validator-terpisah (galit yang lebih dulu dijangkau jalan pemindaian
yang menang; dulu UTF-8 selalu menang karena divalidasi lebih dulu).
Keduanya tetap ditolak — hanya prioritas statusnya yang bisa berbeda.

## Validasi scope bertipe: STACK-BIT (v0.1.5)

Dahulu kedalaman bracket memakai satu counter agregat: jumlah `{}[]`
yang seimbang dianggap cukup — `["mismatch"}` (penutup jenis salah,
jumlah seimbang) lolos. Kini **stack-bit**: satu `uint64_t` menumpuk
tipe bracket per level (0 = `[`, 1 = `{`):

```text
buka  '[' : stack = (stack << 1);     depth++
buka  '{' : stack = (stack << 1)|1;  depth++
tutup ']' : reject bila depth==0 atau bit teratas ≠ 0; stack >>= 1
tutup '}' : reject bila depth==0 atau bit teratas ≠ 1; stack >>= 1
```

Ini **branchless** di jalur struktural (satu shift + XOR per bracket)
dan menangkap SALING-SILANG `[{]}` juga — bit teratas wajib cocok,
bukan hanya jumlah per jenis. Biaya: ~9% pada payload bracket-padat
(synth), ~0% pada payload string-heavy (Jeopardy) — terukur interleaved
`scanner_separation_proof.json`.

## Kontrak galat scanner

| Kondisi | Status |
|---|---|
| Pointer null / ukuran kosong | `INVALID_JSON` |
| Buffer tape kurang | `INSUFFICIENT_BUFFER` |
| Bracket tak seimbang (terpotong / tutup berlebih) | `INVALID_JSON` |
| Bracket TAK COCOK JENISNYA (`{` ditutup `]` dst., v0.1.5) | `INVALID_JSON` |
| Kutip tanpa penutup / escape di ujung payload | `UNEXPECTED_QUOTE` |
| UTF-8 rusak (DFA REJECT / sekuens terpotong di akhir) | `INVALID_UTF8` |
| Lolos semua | jumlah entri tape ditulis ke `result`, status `OK` |

## Berkas terkait

- Implementasi: [`src/scanner/scalar.cpp`](../../src/scanner/scalar.cpp)
  + [`src/scanner/parallel.cpp`](../../src/scanner/parallel.cpp)
- Tabel struktural & DFA: [`include/ze_json/lookups/`](../../include/ze_json/lookups/)
- Util SWAR: [`include/ze_json/utils/swar.hpp`](../../include/ze_json/utils/swar.hpp)
- Uji: [`tests/scanner_test/`](../../tests/scanner_test/) (termasuk sekuens
  multibyte terbelah batas blok 8 byte, kesetaraan scanner8/64, dan
  kesetaraan penolakan)
