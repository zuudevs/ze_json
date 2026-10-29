# Arsitektur: Validasi gramatikal tanpa pass berat — pemetaan Tahap-7

> Lanjutan rekam jejak goto automaton (di-revert 560a6ef, lihat
> [optimizations.md](optimizations.md)). Rancangan eksperimen C2
> (tape-walk grammar validator). Status: **TERUKUR NEGATIF — tidak
> diadopsi ke `src/`** (lihat §Hasil pengukuran di bawah).

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
  * korpus json.org (`samples/JSON_Checker`): pass → ACCEPT;
    fail → TOLAK kecuali daftar lazy/string&fail18 di atas.
  * seluruh `samples/*.json` → ACCEPT.
- Angka target yang layak adopsi `src/`: Δ in-run ≤ beberapa persen pada
  datasets token-padat; kalau lebih → hasil negatif terdokumentasi
  (Tahap-6 preseden).

## Hasil pengukuran (commit `97c053d`) — NEGATIF, tidak diadopsi

Gerbang kebenaran lolos penuh: 33 fail korpus json.org sesuai ekspektasi
(26 reject-target; fail15/17/18/25–28 accept-sengaja — string lazy +
kedalaman), 3 pass, 7 dataset nyata, payload sintetis → ACCEPT. Bug
`pop_state` (membaca elemen yang baru di-pop alih-alih kontainer
pembungkus) tertemukan lewat jalur diagnostik `validate_grammar_tapewalk_dbg`
dan diperbaiki sebelum pembekuan.

### Δ in-run pasangan terhimpit (8 dataset, `cpu_time`)

| dataset | Base (ns) | + Tapewalk (ns) | rasio | overhead vs base |
|---|---|---|---|---|
| github_events | 284.598 | 810.523 | 2,85× | +184,8% |
| update_center | 2.801.133 | 6.584.821 | 2,35× | +135,1% |
| canada | 2.921.439 | 7.301.779 | 2,50× | +149,9% |
| mesh | 444.974 | 1.013.288 | 2,28× | +127,7% |
| twitter | 629.957 | 1.333.207 | 2,12× | +111,6% |
| jeopardy | 56.935.975 | 126.879.699 | 2,23× | +122,8% |
| citm_catalog | 2.078.764 | 3.943.976 | 1,87× | +89,7% |
| Synthetic (token-dense) | 181.631 | 545.917 | 3,01× | +200,6% |

Artefak: `out/results/benchmark/grammar_walk_interleaved_97c053d.json`
+ aturan notebook `gram_base_vs_tapewalk`.

### Dekomposisi biaya: α per-entri dominan, bukan scan gap

Karakterisasi `samples/` (semantik tape: angka = gap tanpa entri;
literal = 1 entri + tail; string = 2 entri) memberi model linear yang
pas nyaris sempurna pada 7 overhead dataset nyata
(kuadrat terkecil, R² = 1,0000):

```
overhead_ns ≈ 7,14 × entri_tape + 0,64 × byte_ws + 1,44 × byte_angka
```

- **α ≈ 7,14 ns/entri** — dispatch mesin status per entri; dominan di
  `update_center` (568.803 entri, gap ≈ 0 → overhead +135% hampir murni
  α), `jeopardy` (9,54 juta entri) dan `github_events`.
- **β_num ≈ 1,44 ns/byte** — cek angka canonical `region_number`;
  mengunci `canada` (gap 1,98 MB hampir seluruhnya angka).
- **β_ws ≈ 0,64 ns/byte** — `region_ws_only`; mengunci
  `citm_catalog`/`twitter` (ws-gap 1,25 MB / 176 KB).

### Konfirmasi mikroarsitektur (AMD uProf `assess_ext`)

3 sesi sampling (Tapewalk/Base pada update_center, Tapewalk pada canada;
artefak `out/results/profiling/amduprof_tahap7_*.csv`). Atribusi via
address biner-sama: klaster `0x14001b*` hadir HANYA di run Tapewalk =
kode validator; klaster `0x140032*` (muncul di kedua run, word-load
misaligned 3,2–3,6 PTI) = jalur scanner64.

- Validator (update_center): **CPI ≈ 0,55 (IPC ≈ 1,8), mispredict cabang
  hanya 0,45–1,4%**, L1-access PTI ~570, nol misaligned-load → biaya α
  adalah **jumlah instruksi skalar per entri** (≈25–30 inst × CPI),
  bukan denda prediksi-cabang.
- Validator (canada): branch PTI naik ke 207–217 (vs 125) —
  `region_number` memang bercabang per byte; β_num terkonfirmasi.
- Catatan disiplin: PMC berjalan di bawah Hyper-V → angka absolut adalah
  ekstrapolasi; yang saih = rasio dalam-run + atribusi address
  lintas-run biner-sama.

### Floor: gerbang ±5–10% tak tercapai oleh pass kedua APA PUN

Proyeksi batas bawah dengan optimis maksimal (DFA sempurna
α → 2,5 ns/entri, SWAR ws β_ws → 0,15 ns/byte, β_num tetap):

| dataset | overhead kini | floor proyeksi |
|---|---|---|
| canada | 149,9% | 118,9% |
| mesh | 127,7% | 114,1% |
| github_events | 184,8% | 77,0% |
| update_center | 135,1% | 52,0% |
| jeopardy | 122,8% | 42,7% |
| citm_catalog | 89,7% | 35,8% |
| twitter | 111,6% | 34,8% |

Mencapai gerbang ≤10% menuntut validator < 0,5 ns/entri — di bawah
biaya satu lompatan kondisional prosesor mana pun. **Kesimpulan
struktural**: validasi gramatikal "gratis" tidak mungkin dicapai lewat
pass O(token) kedua di atas tape; satu-satunya rute adalah **fusi ke
Stage-1 scanner** (satu lintasan — ranah yang sudah terbukti berisiko
lewat preseden goto automaton −50%) atau tetap lazy di Stage-2.
Mengikuti hukum pertama, C2 tidak diadopsi ke `src/`.

## Berkas terkait

- `tests/JSON_checker_test/main.cpp` — kunci klasifikasi struktural
  (`structuralFails`) yang menjaga batas ini.
- `docs/architecture/scanner.md` §Format tape.
