# Arsitektur: Validasi gramatikal tanpa pass berat — pemetaan Tahap-7

> Lanjutan rekam jejak goto automaton (di-revert 560a6ef, lihat
> [optimizations.md](optimizations.md)). Rancangan eksperimen C2
> (tape-walk grammar validator). Status: **eksperimen — belum adopsi
> `src/`** sampai terukur.

## Konteks

goto automaton (PR #2) memvalidasi gramatika penuh di Stage-1 scanner dan
kalah −50% throughput. Rencana Tahap-7: **bukan di Stage-1** — memakai
DATA yang sudah produksi gratis oleh scanner64 skutuhnya: entri tape.

## Chain of trust yang sudah ditemukan (dibaca langsung dari kode)

1. `lookups/structural.hpp`: `kStructural['t'|'f'|'n'] = SCAN_NORMAL +
   bit-type` — **awalan literal adalah TERMINATOR span**: scanner
   merekam 1 entri tape di posisi literal
   (konfirmasi: `docs/architecture/scanner.md` §Format tape).
2. Format tape: struktural 1 entri, string **2 entri berurutan** (kutip
   buka & tutup — string tak dapat ber-nesting), literal 1 entri,
   **angka 0 entri** (teks angka = *gap* antara dua entri).
3. Kelas token tiap entri DIDAPAT DARI BYTE di posisi entri itu sendiri
   (`kStructural[byte]`): container/komma/colon/quote/literal-start.
4. `create_session` = scan → Session, TANPA pass pasca-scan; Stage-2
   on-demand (find/find_field/skip_child) mengasumsikan format, bukan
   gramatika (lamely menerima `"::"` dsb.).

## Pemetaan korpus json.org fail*.json (33 file)

Sudah tertolak scanner (struktural, gratis — lama): fail2 (unclosed
array), fail8 (extra close), fail32 (unclosed object), fail33
(`["mismatch"}`).

Sisa 29 file — semuanya TERTOLAK oleh tahap-walk validator, DENGAN
PENGECUALIAN yang sah (diluar ranah interpreter tape):

| Kelompok | File fail | Mekanisme tertolak-tape | Ya/Tidak |
|---|---|---|Y|
| Tipe akar bukan kontainer | fail1 | root-check (`"..."` root string) | Ya |
| Sampah pasct-close | fail7, fail10 | `,`/token lain pada mode DONE | Ya |
| Pemisah koma/kolon lego | fail4, 5, 6, 9 | `,` pada mode VALUE; `{` no-key | Ya |
| Ketidakhadiran Mahmoud rejim:: | fail19–22 | `n`-literal/`:`/`,` pada mode COLON/VALUE | Ya |
| Untuk extra koma | fail9/… | `,` mengikuti literal → memakai yang setelahnya | Ya |
| key takberkutip | fail3 | gap non-ws di mode KEY | Ya |
| bare identifier/ascii | fail16, fail23 | gap/literal assert (truth) | Ya |
| angka ilegal | fail13, 14, 29, 30, 31 | gap grammar number (`013`, `0x14`, `0e`…) | Ya |
| ekspresi JS | fail11, 12 | sisa gap bukan angka/`+ 2`, `alert()`, `truth` | Ya |
| escape liar (string) | fail15, 17, 25–28 | string opak di scan — **dilazy-kan** ke read | Tidak (on-demand) |
| kedalaman >20 | fail18 | kebijakan depth ref bukan ranah validator | Sengaja valid JSON |

Perkiraan cakupan validator: **25/29 gramtika** tertolak di
`create_session`, ditambah 4 struktural scanner → **29/33** rekor json.org
tertolak penuh, SAMPAI. string-lazy (6 file) & fail18 (sengaja) tetap.

## Rancangan validator (C2 — tape-walk satu lintasan)

- Input: `(src, tape, n)` — TIDAK menyusul payload byte-by-byte kedua;
  hanya melompat gap.
- Mesin status: `ROOT_VALUE → (ROOT DONE) | KEY_OR_CLOSE | VALUE_OR_CLOSE
  | VALUE | KEY | COLON | COMMA | DONE`; stack-bit tipe kontainer
  (mirror scanner) + root flag.
- Literal entry ('t'/'f'/'n'): uji cocok persis `true/false/null` +
  bukan diikuti alnum (menangkap `truth`, `truehappy`).
- Gap pada mode VALUE: harus kosong-ws ATAU satu angka-resmi
  (leading-zero/hex/exp-incomplete ditolak) — `1 + 2` tertolak karena
  sisa gap `+ 2` bukan ws.
- String: konsumsi 2 entri — kutip buka harus disusul kutip tutup
  berurutan.
- Root: wajib `[`/`{`; setelah nilai akar → token apa pun = sampah →
  tolak.
- Sisa payload pasca-entri-terakhir: ws-only (sudah diikat mode DONE).

## Metodologi pengukuran (REPLIKA dulu — tanpa sentuhan `src/`)

- `benchmarks/session_benchmark/replica_grammar_tapewalk.hpp` — validator
  beku (replika pola A1/Tahap-6).
- Pasangan bersebelahan dalam satu proses per dataset nyata:
  `BM_GRAM_Base_<ds>` (create_session murni) vs
  `BM_GRAM_Tapewalk_<ds>` (create_session + validator). Δ = biaya
  validator — TANPA klaim eksponen informasi apa-apa dari les
  antar-proses (hukum pertama).
- GERBANG kebenaran (sekali jalankan di start-up, суда gagal = semua
  benchmark `SkipWithError`):
  * korpus json.org (`tests/JSON_checker_test/corpus`): pass → ACCEPT;
    fail → TOLAK kecuali daftar lazy/string&fail18 di atas.
  * seluruh `samples/*.json` → ACCEPT.
- Angka target yang layak adopsi `src/`: Δ in-run ≤ beberapa persen pada
  datasets token-padat; kalau lebih → hasil negatif terdokumentasi
  (Tahap-6 preseden).

## Berkas terkait

- `tests/JSON_checker_test/main.cpp` — kunci klasifikasi struktural
  (`structuralFails`) yang menjaga batas ini.
- `docs/architecture/scanner.md` §Format tape.
