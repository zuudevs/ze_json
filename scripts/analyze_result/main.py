# %% [markdown]
# # Analisis hasil benchmark dan test `ze_json`
#
# Notebook ini membaca artefak lokal dari `out/results/benchmark/` dan
# `out/results/test/`. Ia mendukung Google Benchmark JSON, ringkasan benchmark,
# metrik JavaScript, laporan Google Test individual, dan ringkasan lintas preset.
#
# **Catatan metodologis:** grafik tren lintas file bersifat deskriptif. Dokumen
# `docs/development/benchmarking.md` di repo memperingatkan agar angka absolut
# lintas proses tidak diperlakukan sebagai bukti kausal. Untuk perbandingan,
# notebook menyediakan: pasangan eksperimen APAPUN dari file/run yang sama
# (pair engine §5, bukan hanya Scanner8/64), lantai noise yang DIUKUR dari
# artefak run-ulang (§3b), dan perbandingan antar-revisi berstatus deskriptif
# (§6b). Angka JS `ops/s` dan `µs/op` juga tidak dicampur.

# %%
import json
import re
import math
from pathlib import Path
from collections import Counter

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from IPython.display import display, Markdown

pd.set_option("display.max_columns", 100)
pd.set_option("display.max_colwidth", 140)
pd.set_option("display.width", 220)

# %% [markdown]
# ## 1. Konfigurasi path
#
# Jalankan notebook dari root repo `ze_json` atau letakkan notebook di bawah
# root tersebut. Jika perlu, isi `REPO_ROOT_OVERRIDE` secara manual.

# %%
REPO_ROOT_OVERRIDE = Path(__file__).resolve().parents[2]


def find_repo_root(start: Path | None = None) -> Path:
    if REPO_ROOT_OVERRIDE:
        candidate = Path(REPO_ROOT_OVERRIDE).expanduser().resolve()
        if not (candidate / "out" / "results").is_dir():
            raise FileNotFoundError(f"REPO_ROOT_OVERRIDE tidak memiliki out/results: {candidate}")
        return candidate

    start = (start or Path.cwd()).resolve()
    for candidate in (start, *start.parents):
        if (candidate / "out" / "results" / "benchmark").is_dir() and (candidate / "out" / "results" / "test").is_dir():
            return candidate

    raise FileNotFoundError(
        "Root repo tidak ditemukan. Buka Jupyter dari root ze_json atau isi REPO_ROOT_OVERRIDE."
    )


REPO_ROOT = find_repo_root()
BENCH_DIR = REPO_ROOT / "out" / "results" / "benchmark"
TEST_DIR = REPO_ROOT / "out" / "results" / "test"

print(f"Repo       : {REPO_ROOT}")
print(f"Benchmark  : {BENCH_DIR} | exists={BENCH_DIR.is_dir()}")
print(f"Test       : {TEST_DIR} | exists={TEST_DIR.is_dir()}")

# %% [markdown]
# ## 2. Inventaris file dan deteksi skema
#
# Ringkasan (`summary_*.json`) disimpan terpisah dari laporan mentah agar data
# yang sama tidak terhitung dua kali ketika menghitung distribusi atau grafik.

# %%
UNIT_TO_NS = {
    "ns": 1.0,
    "us": 1_000.0,
    "µs": 1_000.0,
    "μs": 1_000.0,
    "ms": 1_000_000.0,
    "s": 1_000_000_000.0,
}


def classify_json(path: Path, obj: object) -> str:
    if not isinstance(obj, dict):
        return "json_other"
    if path.name.startswith("summary_"):
        if isinstance(obj.get("groups"), list) and "scanner_speedups" in obj:
            return "benchmark_summary"
        if isinstance(obj.get("groups"), list) and isinstance(obj.get("totals"), dict):
            return "test_summary"
        return "summary_other"
    if isinstance(obj.get("context"), dict) and isinstance(obj.get("benchmarks"), list):
        return "google_benchmark"
    if isinstance(obj.get("testsuites"), list):
        return "gtest_report"
    if isinstance(obj.get("results"), list):
        rows = obj["results"]
        if rows and isinstance(rows[0], dict):
            if "bytes" in rows[0] and isinstance(rows[0].get("us"), dict):
                return "js_sizes"
            if any(k in rows[0] for k in ("jsonParse", "zeScan", "zeScanFind")):
                return "js_parse"
        return "js_results_other"
    return "json_other"


def relpath(path: Path) -> str:
    return path.resolve().relative_to(REPO_ROOT.resolve()).as_posix()


JSON_RECORDS = []
INVENTORY_ROWS = []
for base_dir in (BENCH_DIR, TEST_DIR):
    if not base_dir.is_dir():
        continue
    for file_path in sorted(base_dir.rglob("*.json")):
        try:
            with file_path.open("r", encoding="utf-8-sig") as handle:
                payload = json.load(handle)
            schema = classify_json(file_path, payload)
            parse_error = None
        except Exception as exc:
            payload = None
            schema = "invalid_json"
            parse_error = f"{type(exc).__name__}: {exc}"

        record = {
            "path": file_path,
            "source_file": relpath(file_path),
            "artifact_stem": file_path.stem,
            "data": payload,
            "schema": schema,
            "parse_error": parse_error,
        }
        JSON_RECORDS.append(record)
        INVENTORY_ROWS.append({
            "source_file": record["source_file"],
            "area": "benchmark" if BENCH_DIR in file_path.parents else "test",
            "schema": schema,
            "size_bytes": file_path.stat().st_size,
            "valid_json": parse_error is None,
            "parse_error": parse_error,
        })

inventory_df = pd.DataFrame(INVENTORY_ROWS)
print(f"Jumlah file JSON ditemukan: {len(inventory_df)}")
if not inventory_df.empty:
    display(inventory_df.groupby(["area", "schema"], dropna=False).agg(
        files=("source_file", "count"), total_size_bytes=("size_bytes", "sum")
    ).reset_index().sort_values(["area", "schema"]))
    display(inventory_df[inventory_df["valid_json"] == False])

# %% [markdown]
# ## 3. Parser metrik dan tabel benchmark mentah
#
# Semua waktu Google Benchmark dinormalisasi menjadi nanodetik per operasi.
# Baris `error_occurred`, waktu nol, atau iterasi nol tidak dipakai sebagai
# hasil performa yang valid—tetapi tetap disimpan agar masalah terlihat.

# %%
def to_float(value):
    try:
        if value is None or value == "":
            return np.nan
        return float(value)
    except (TypeError, ValueError):
        return np.nan


def safe_int(value, default=0):
    numeric = to_float(value)
    return int(numeric) if pd.notna(numeric) else default


def normalize_benchmark_name(name: str) -> str:
    """Hapus suffix konfigurasi Google Benchmark, tetapi pertahankan argumen seperti Idx:0."""
    name = str(name or "")
    return re.sub(r"/(?:min_time|min_warmup_time|repeats|iterations|threads):[^/]+", "", name)


def revision_from(stem: str, payload: dict | None = None):
    if isinstance(payload, dict):
        value = payload.get("rev") or payload.get("hash")
        if value:
            return str(value)
    match = re.search(r"_([0-9a-fA-F]{7,40})$", stem)
    return match.group(1) if match else None


def family_from_name(name: str) -> str:
    return str(name).split("/", 1)[0]


BENCH_ROWS = []
for rec in JSON_RECORDS:
    if rec["schema"] != "google_benchmark":
        continue

    payload = rec["data"]
    context = payload.get("context") or {}
    for bench in payload.get("benchmarks", []):
        # Raw reports dapat memuat baris agregat; simpan hanya iterasi sebenarnya.
        run_type = bench.get("run_type")
        if run_type not in (None, "iteration"):
            continue

        unit = str(bench.get("time_unit") or "ns")
        factor = UNIT_TO_NS.get(unit, 1.0)
        name = normalize_benchmark_name(bench.get("name", ""))
        real_time = to_float(bench.get("real_time"))
        cpu_time = to_float(bench.get("cpu_time"))
        time_ns = real_time * factor if pd.notna(real_time) else np.nan
        cpu_ns = cpu_time * factor if pd.notna(cpu_time) else np.nan
        bps = to_float(bench.get("bytes_per_second"))
        iterations = to_float(bench.get("iterations"))
        error_flag = bool(bench.get("error_occurred", False))
        error_message = bench.get("error_message")
        valid = (not error_flag) and pd.notna(time_ns) and time_ns > 0 and pd.notna(iterations) and iterations > 0

        scanner_match = re.search(r"Scanner(8|64)", name, flags=re.IGNORECASE)
        dataset_match = re.match(r"BM_Scanner(?:8|64)_([^/]+)", name, flags=re.IGNORECASE)
        date_value = context.get("date")
        BENCH_ROWS.append({
            "source_file": rec["source_file"],
            "artifact_stem": rec["artifact_stem"],
            "revision": revision_from(rec["artifact_stem"], payload),
            "run_date": date_value,
            "host_name": context.get("host_name"),
            "num_cpus": to_float(context.get("num_cpus")),
            "mhz_per_cpu": to_float(context.get("mhz_per_cpu")),
            "library_version": context.get("library_version"),
            "build_type": context.get("library_build_type"),
            "benchmark_name_raw": bench.get("name"),
            "benchmark_name": name,
            "benchmark_family": family_from_name(name),
            "iterations": iterations,
            "threads": bench.get("threads"),
            "time_unit_raw": unit,
            "time_ns": time_ns,
            "cpu_ns": cpu_ns,
            "bytes_per_second": bps,
            "throughput_MB_s": bps / 1e6 if pd.notna(bps) else np.nan,
            "throughput_GiB_s": bps / (1024 ** 3) if pd.notna(bps) else np.nan,
            "error_occurred": error_flag,
            "error_message": error_message,
            "valid_measurement": valid,
            "scanner_variant": scanner_match.group(1) if scanner_match else None,
            "scanner_pair_key": re.sub(r"Scanner(?:8|64)", "ScannerX", name, flags=re.IGNORECASE) if scanner_match else None,
            "scanner_dataset": dataset_match.group(1) if dataset_match else None,
        })

bench_df = pd.DataFrame(BENCH_ROWS)
if not bench_df.empty:
    bench_df["run_date"] = pd.to_datetime(bench_df["run_date"], errors="coerce", utc=True)
    print(f"Baris benchmark mentah: {len(bench_df):,}")
    print(f"Baris dengan pengukuran valid: {int(bench_df['valid_measurement'].sum()):,}")
    print(f"Baris error/tidak layak untuk analisis performa: {int((~bench_df['valid_measurement']).sum()):,}")
    display(bench_df.head(10))
else:
    print("Tidak ada laporan Google Benchmark mentah yang berhasil dibaca.")

# %% [markdown]
# ## 3b. Lantai noise dari artefak run-ulang
#
# Getaran jam lintas-proses (terdokumentasi 15–45% pada mesin pengembang,
# docs/development/benchmarking.md) bisa DIUKUR, bukan sekadar diklaim, dari
# artefak yang direkam berulang dalam konteks eksperimen yang sama:
# `*_r2`, `*_run2`, atau duplikat eksperimen seperti `tahap5_typedepth_guard2`.
# Rentang (max−min)/median lintas file replika pada nama benchmark yang sama
# menjadi ambang "di atas noise" yang dipakai sel perbandingan revisi (§6b)
# dan penanda pada tabel before/after (§6). Ini mengukur disiplin pengukuran,
# BUKAN membandingkan kode yang berbeda.

# %%
def replica_group_key(stem: str, all_stems: set) -> str:
    """Kelompokkan artefak replika: `x_r2`/`x_run2` mengikuti `x`; `x2`
    mengikuti `x` hanya bila `x` sendiri adalah artefak yang ada dan tidak
    berakhiran digit (agar hash revisi seperti `..._05d2180` tidak ikut
    terpotong)."""
    stripped = re.sub(r"_(?:run|r)\d+$", "", stem, flags=re.IGNORECASE)
    if stripped != stem:
        return stripped
    match = re.match(r"^(?P<base>.+?)(?P<num>\d+)$", stem)
    if match and match.group("base") in all_stems and not match.group("base")[-1].isdigit():
        return match.group("base")
    return stem


noise_floor_rows = []
noise_band_by_family = {}
if not bench_df.empty:
    bench_stems = set(bench_df["artifact_stem"])
    bench_valid_noise = bench_df[bench_df["valid_measurement"]].copy()
    bench_valid_noise["replica_group"] = bench_valid_noise["artifact_stem"].apply(
        lambda s: replica_group_key(s, bench_stems))

    for (replica_group, name), sub in bench_valid_noise.groupby(["replica_group", "benchmark_name"]):
        file_medians = sub.groupby("source_file")["time_ns"].median()
        if len(file_medians) < 2:
            continue  # satu file saja: tidak mengukur antar-run apa pun
        med = float(file_medians.median())
        if med <= 0:
            continue
        noise_floor_rows.append({
            "replica_group": replica_group,
            "benchmark_name": name,
            "family": family_from_name(name),
            "files": "; ".join(sorted(Path(p).name for p in file_medians.index)),
            "file_count": len(file_medians),
            "median_time_ns": med,
            "min_time_ns": float(file_medians.min()),
            "max_time_ns": float(file_medians.max()),
            # (max-min)/median — proksi getaran antar-run pada konteks sama
            "spread_pct": (file_medians.max() - file_medians.min()) / med * 100.0,
            "cv_pct": (float(file_medians.std(ddof=1)) / med * 100.0) if len(file_medians) > 1 else 0.0,
        })

noise_floor_df = pd.DataFrame(noise_floor_rows)
if not noise_floor_df.empty:
    band = noise_floor_df.groupby("family")["spread_pct"].median()
    noise_band_by_family = {str(k): float(v) for k, v in band.items() if pd.notna(v) and float(v) > 0}
    print(f"Kelompok replika run-ulang (>=2 file): {noise_floor_df['replica_group'].nunique()}")
    display(noise_floor_df.sort_values(["replica_group", "benchmark_name"]))
    print("Ambang noise per keluarga benchmark (median spread % — dipakai §6 dan §6b):")
    display(pd.DataFrame(sorted(noise_band_by_family.items()), columns=["family", "spread_pct_median"]))
else:
    print("Belum ada artefak run-ulang (>=2 file per konteks eksperimen) untuk mengukur lantai noise.")

# %% [markdown]
# ## 4. Ringkasan benchmark dan metrik JavaScript
#
# `summary_*.json` dianalisis sebagai sumber terpisah—jangan digabungkan begitu
# saja dengan benchmark mentah, sebab ringkasan tersebut berasal dari laporan
# benchmark yang sama. Laporan `js_parse` menyimpan operasi per detik (ops/s),
# sementara `js_sizes` menyimpan mikrodetik per operasi (µs/op).

# %%
SUMMARY_BENCH_ROWS = []
SUMMARY_SCANNER_ROWS = []
JS_SIZE_ROWS = []
JS_PARSE_ROWS = []

for rec in JSON_RECORDS:
    payload = rec["data"]
    if not isinstance(payload, dict):
        continue

    if rec["schema"] == "benchmark_summary":
        summary_hash = payload.get("hash") or revision_from(rec["artifact_stem"], payload)
        generated_at = payload.get("generated_at")
        for group in payload.get("groups", []):
            for bench in group.get("benchmarks", []):
                SUMMARY_BENCH_ROWS.append({
                    "summary_file": rec["source_file"],
                    "summary_hash": summary_hash,
                    "generated_at": generated_at,
                    "group": group.get("group"),
                    "build_type": group.get("build_type"),
                    "host_name": group.get("host_name"),
                    "context_date": group.get("context_date"),
                    "report_file": group.get("report"),
                    "benchmark_name": normalize_benchmark_name(bench.get("name", "")),
                    "time_ns": to_float(bench.get("time_ns")),
                    "cpu_ns": to_float(bench.get("cpu_ns")),
                    "iterations": to_float(bench.get("iterations")),
                    "bytes_per_second": to_float(bench.get("bytes_per_second")),
                    "items_per_second": to_float(bench.get("items_per_second")),
                })
        for row in payload.get("scanner_speedups", []):
            SUMMARY_SCANNER_ROWS.append({
                "summary_file": rec["source_file"],
                "summary_hash": summary_hash,
                "generated_at": generated_at,
                **row,
            })

    elif rec["schema"] == "js_sizes":
        for result in payload.get("results", []):
            for metric, value in (result.get("us") or {}).items():
                JS_SIZE_ROWS.append({
                    "source_file": rec["source_file"],
                    "revision": payload.get("rev") or revision_from(rec["artifact_stem"], payload),
                    "timestamp": payload.get("timestamp"),
                    "node": payload.get("node"),
                    "platform": payload.get("platform"),
                    "dataset_label": result.get("label"),
                    "payload_bytes": to_float(result.get("bytes")),
                    "metric": metric,
                    "time_us": to_float(value),
                })

    elif rec["schema"] == "js_parse":
        for result in payload.get("results", []):
            for metric in ("jsonParse", "zeScan", "zeScanFind"):
                ops_s = to_float(result.get(metric))
                JS_PARSE_ROWS.append({
                    "source_file": rec["source_file"],
                    "revision": payload.get("rev") or revision_from(rec["artifact_stem"], payload),
                    "timestamp": payload.get("timestamp"),
                    "node": payload.get("node"),
                    "platform": payload.get("platform"),
                    "dataset": result.get("dataset"),
                    "metric": metric,
                    "ops_per_second": ops_s,
                    "time_us_per_op": 1e6 / ops_s if pd.notna(ops_s) and ops_s > 0 else np.nan,
                })

summary_bench_df = pd.DataFrame(SUMMARY_BENCH_ROWS)
summary_scanner_df = pd.DataFrame(SUMMARY_SCANNER_ROWS)
js_sizes_df = pd.DataFrame(JS_SIZE_ROWS)
js_parse_df = pd.DataFrame(JS_PARSE_ROWS)

print("Ringkasan benchmark:", len(summary_bench_df), "baris")
print("Ringkasan pasangan scanner:", len(summary_scanner_df), "baris")
print("JS sizes:", len(js_sizes_df), "baris metrik panjang")
print("JS parse:", len(js_parse_df), "baris metrik panjang")
if not js_sizes_df.empty:
    display(js_sizes_df.head(12))
if not js_parse_df.empty:
    display(js_parse_df.head(9))

# Perbandingan JS turunan pada workload yang paling dekat pasangannya.
# Rasio waktu > 1 berarti jalur ze_json memiliki latency lebih rendah.
js_sizes_wide = pd.DataFrame()
if not js_sizes_df.empty:
    js_sizes_wide = js_sizes_df.pivot_table(
        index=["source_file", "revision", "dataset_label", "payload_bytes"],
        columns="metric", values="time_us", aggfunc="median"
    ).reset_index()
    if {"JSON+baca", "ze baca"}.issubset(js_sizes_wide.columns):
        js_sizes_wide["speedup_ze_baca_vs_json_baca"] = js_sizes_wide["JSON+baca"] / js_sizes_wide["ze baca"]
    if {"JSON+iter", "ze iter"}.issubset(js_sizes_wide.columns):
        js_sizes_wide["speedup_ze_iter_vs_json_iter"] = js_sizes_wide["JSON+iter"] / js_sizes_wide["ze iter"]
    print("Pasangan JS sizes (rasio > 1 berarti waktu ze_json lebih rendah):")
    display(js_sizes_wide)

js_parse_wide = pd.DataFrame()
if not js_parse_df.empty:
    js_parse_wide = js_parse_df.pivot_table(
        index=["source_file", "revision", "dataset"],
        columns="metric", values="ops_per_second", aggfunc="median"
    ).reset_index()
    if {"jsonParse", "zeScan"}.issubset(js_parse_wide.columns):
        js_parse_wide["throughput_ratio_ze_scan_vs_json_parse"] = js_parse_wide["zeScan"] / js_parse_wide["jsonParse"]
    if {"jsonParse", "zeScanFind"}.issubset(js_parse_wide.columns):
        js_parse_wide["throughput_ratio_ze_scan_find_vs_json_parse"] = js_parse_wide["zeScanFind"] / js_parse_wide["jsonParse"]
    print("Pasangan JS parse (rasio throughput > 1 berarti ops/s ze_json lebih tinggi; workload tidak identik sepenuhnya):")
    display(js_parse_wide)

# %% [markdown]
# ## 4b. Sapuan ukuran JS: titik berimbang dan pasangan revisi
#
# Break-even = ukuran payload terkecil di mana ze_json mulai menang atas
# `JSON.parse` per workload — informasi yang disembunyikan oleh tabel mentah.
# Rasio antar revisi (`js_sizes_*` beda hash) HANYA deskriptif: mesin, versi
# Node, dan jam bisa berbeda; hukum pertama docs/development/benchmarking.md
# tetap berlaku (angka absolut lintas proses bukan bukti kausal).

# %%
js_breakeven_rows = []
if not js_sizes_wide.empty and "payload_bytes" in js_sizes_wide.columns:
    js_workload_pairs = [
        ("scan murni", "JSON.parse", "ze scan"),
        ("baca 2 field", "JSON+baca", "ze baca"),
        ("iterasi penuh", "JSON+iter", "ze iter"),
    ]
    for source_file, sub in js_sizes_wide.groupby("source_file"):
        sub = sub.sort_values("payload_bytes")
        for label, json_col, ze_col in js_workload_pairs:
            if json_col not in sub.columns or ze_col not in sub.columns:
                continue
            good = sub.dropna(subset=[json_col, ze_col])
            if good.empty:
                continue
            speedup = good[json_col] / good[ze_col]
            winning = good.loc[speedup > 1, "payload_bytes"]
            js_breakeven_rows.append({
                "source_file": source_file,
                "workload": label,
                "sizes_tested": len(good),
                "min_bytes": int(good["payload_bytes"].min()),
                "max_bytes": int(good["payload_bytes"].max()),
                "break_even_bytes": int(winning.min()) if not winning.empty else None,
                "ze_wins_at_largest": bool(speedup.iloc[-1] > 1),
                "speedup_min": float(speedup.min()),
                "speedup_max": float(speedup.max()),
            })

js_breakeven_df = pd.DataFrame(js_breakeven_rows)
if not js_breakeven_df.empty:
    print("Titik berimbang per workload (ukuran terkecil ze lebih cepat dari JSON.parse):")
    display(js_breakeven_df)
else:
    print("Break-even JS tidak dapat dihitung (kolom workload pasangan tidak lengkap).")


js_sizes_crossrev_df = pd.DataFrame()
if not js_sizes_df.empty and js_sizes_df["revision"].notna().any():
    js_sizes_with_rev = js_sizes_df[js_sizes_df["revision"].notna()]
    revision_order = (
        js_sizes_with_rev.sort_values("timestamp")
        .drop_duplicates("revision")["revision"].tolist()
    )
    if len(revision_order) >= 2:
        rev_a, rev_b = revision_order[0], revision_order[-1]
        pivot_rev = js_sizes_with_rev.pivot_table(
            index=["metric", "payload_bytes"], columns="revision",
            values="time_us", aggfunc="median",
        )[[rev_a, rev_b]]
        js_sizes_crossrev_df = pivot_rev.dropna().reset_index()
        js_sizes_crossrev_df["ratio_rev_a_over_rev_b"] = (
            js_sizes_crossrev_df[rev_a] / js_sizes_crossrev_df[rev_b]
        )
        print(f"Perbandingan sapuan lintas revisi ({rev_a} vs {rev_b}) — deskriptif:")
        display(js_sizes_crossrev_df)
    else:
        print("Belum ada dua revisi js_sizes berbeda untuk dibandingkan.")

# %% [markdown]
# ## 5. Pasangan dalam-run — pair engine terneralisasi
#
# Semua pasangan dibandingkan HANYA dari file/laporan mentah yang sama
# (hukum pertama). Aturan pasangan dideklarasikan di `PAIR_RULES`; setiap
# aturan memetakan nama benchmark → `(pair_key, varian)`, dan baris pada
# file yang sama dipivot per pasangan. Arti rasio: `ratio_a_over_b =
# waktu_a / waktu_b` — `> 1` berarti varian B lebih cepat, `time_reduction_pct`
# positif berarti varian B lebih hemat waktu.
#
# Aturan terpasang (bisa ditambah tanpa menyentuh logika engine):
#
# | Aturan | Varian A ↔ B | Interpretasi |
# |---|---|---|
# | `typed_vs_untyped_class` | typed ↔ untyped | Klasifikasi bracket bertipe vs tanpa tipe (Scanner64) |
# | `utf8_integrated_vs_separate_on_scanner8` | separate Utf8 ↔ integrated | Validasi UTF-8 terpisah vs terintegrasi di jalur skalar |
# | `utf8_integrated_vs_separate_pipeline` | separate Utf8 ↔ integrated | Pass validasi terpisah vs satu lintasan terintegrasi (Tahap 4) |
# | `scanner8_vs_64` | scanner8 ↔ scanner64 | SWAR blok 8 byte vs word 64-bit (Tahap 1) |
# | `array_counted_vs_streaming` | counted ↔ streaming | Pola kerja BERBEDA — rasio deskriptif, bukan klaim kecepatan |
# | `spaniso_byte_vs_swarset` | byte_loop ↔ swar_set | A1 — span non-string: loop byte vs SWAR set terminator (eksperimen spaniso) |
# | `spaniso_byte_vs_swaradvance` | byte_loop ↔ swar_advance | A1 — span non-string: loop byte vs SWAR fast-path digit/spasi |
#
# Cara menyesuaikan: tambahkan/mengubah fungsi matcher di `PAIR_RULES`; engine
# hanya memasangkan varian yang sebenarnya hadir pada file yang sama, dan
# menolak pasangan yang kolomnya mencampur banyak keluarga benchmark.

# %%
def _match_scanner_untyped_replica(name: str):
    """BM_Scanner64_X (typed) vs BM_Scanner64_UntypedReplica_X."""
    if "UntypedReplica" in name:
        return name.replace("UntypedReplica_", ""), "untyped"
    if name.startswith("BM_Scanner64_") and "Replica" not in name:
        return name, "typed"
    return None


def _match_scanner_integrated_replica(name: str):
    """BM_Scanner8_X (validasi UTF-8 terpisah) vs BM_Scanner8_IntegratedReplica_X."""
    if "IntegratedReplica" in name:
        return name.replace("IntegratedReplica_", ""), "integrated"
    if name.startswith("BM_Scanner8_") and "Replica" not in name:
        return name, "separate_utf8_pass"
    return None


def _match_separate_validator(name: str):
    """BM_Pipeline(_Utf8)_SeparateValidator vs jalur terintegrasi CreateSession."""
    if "SeparateValidator" in name:
        regime = "utf8" if "Utf8" in name else "pipeline"
        return f"{regime}|{name.rsplit('/', 1)[-1]}", "separate"
    if re.match(r"BM_CreateSession_(?:Pipeline_Scanner(?:32|64|8)|Utf8)/", name):
        regime = "utf8" if "Utf8" in name else "pipeline"
        return f"{regime}|{name.rsplit('/', 1)[-1]}", "integrated"
    return None


def _match_scanner_variant(name: str):
    """Scanner8 vs Scanner64 apapun keluarganya (BM_Scanner*_* dan
    BM_CreateSession_Pipeline_Scanner*/Idx:n). Nama ber-Replica dikecualikan —
    sudah diurus aturan replika di atas."""
    if "Replica" in name:
        return None
    match = re.search(r"Scanner(8|64)", name)
    if not match:
        return None
    return (
        re.sub(r"Scanner(?:8|64)", "ScannerX", name, flags=re.IGNORECASE),
        f"scanner{match.group(1)}",
    )


def _match_array_read(name: str):
    """BM_ArrayRead_Counted vs BM_ArrayRead_Streaming — pola kerja berbeda."""
    match = re.match(r"BM_ArrayRead_(Counted|Streaming)(/.*)?$", name)
    if not match:
        return None
    return f"BM_ArrayRead{match.group(2) or ''}", match.group(1).lower()


def _match_span_iso(name: str):
    """Eksperimen A1 span_iso_benchmark. pasangan per (Fill, Pad):
    BM_SpanIso_<Variant>_Fill{WS,Num}/Pad:<n> -> (FillWS|Pad:8, varian)."""
    match = re.match(
        r"BM_SpanIso_(Scanner64|ByteLoopReplica|SWARSetReplica|SWARAdvanceReplica)"
        r"_Fill(WS|Num)/Pad:(\d+)$", name)
    if not match:
        return None
    variant = {
        "Scanner64": "scanner64",
        "ByteLoopReplica": "byte_loop",
        "SWARSetReplica": "swar_set",
        "SWARAdvanceReplica": "swar_advance",
    }[match.group(1)]
    return f"Fill{match.group(2)}|Pad:{match.group(3)}", variant


PAIR_RULES = [
    {
        "name": "typed_vs_untyped_class",
        "match": _match_scanner_untyped_replica,
        "var_a": "typed",
        "var_b": "untyped",
        "note": "Klasifikasi bracket bertipe vs tanpa tipe (Scanner64)",
    },
    {
        "name": "utf8_integrated_vs_separate_on_scanner8",
        "match": _match_scanner_integrated_replica,
        "var_a": "separate_utf8_pass",
        "var_b": "integrated",
        "note": "Validasi UTF-8 terpisah vs terintegrasi pada jalur skalar",
    },
    {
        "name": "utf8_integrated_vs_separate_pipeline",
        "match": _match_separate_validator,
        "var_a": "separate",
        "var_b": "integrated",
        "note": "Pipeline pass validasi terpisah vs satu lintasan terintegrasi",
    },
    {
        "name": "scanner8_vs_64",
        "match": _match_scanner_variant,
        "var_a": "scanner8",
        "var_b": "scanner64",
        "note": "Scanner SWAR blok 8 byte vs word 64-bit",
    },
    {
        "name": "array_counted_vs_streaming",
        "match": _match_array_read,
        "var_a": "counted",
        "var_b": "streaming",
        "note": "Pola kerja BERBEDA — rasio deskriptif, bukan klaim kecepatan",
    },
    {
        "name": "spaniso_byte_vs_swarset",
        "match": _match_span_iso,
        "var_a": "byte_loop",
        "var_b": "swar_set",
        "note": "A1 · span non-string: loop byte (gwibim) vs SWAR set-terminator",
    },
    {
        "name": "spaniso_byte_vs_swaradvance",
        "match": _match_span_iso,
        "var_a": "byte_loop",
        "var_b": "swar_advance",
        "note": "A1 · span non-string: loop byte vs SWAR fast-path digit/spasi",
    },
]


def build_named_pairs(valid_bench: pd.DataFrame, rules):
    """Bangun pasangan per aturan HANYA dari baris file yang sama.
    Setiap nama benchmark boleh ikut beberapa aturan sekaligus (mis. nama
    jalur terintegrasi ikut aturan SeparateValidator dan aturan scanner8/64).
    Pasangan dengan kolom varian yang mencampur banyak keluarga benchmark
    ditolak sebagai pengaman ambiguitas."""
    pair_frames = []
    dropped_notes = []
    for rule in rules:
        recs = []
        for _, row in valid_bench.iterrows():
            matched = rule["match"](row["benchmark_name"])
            if not matched:
                continue
            pair_key, variant = matched
            recs.append({
                "source_file": row["source_file"],
                "run_date": row["run_date"],
                "benchmark_name": row["benchmark_name"],
                "pair_key": pair_key,
                "variant": variant,
                "time_ns": row["time_ns"],
                "throughput_MB_s": row["throughput_MB_s"],
            })
        if not recs:
            continue
        rdf = pd.DataFrame(recs)

        # Pengaman: satu (file, key, varian) tidak boleh campuran keluarga.
        family_counts = rdf.groupby(["source_file", "pair_key", "variant"])[
            "benchmark_name"].nunique()
        mixed = family_counts[family_counts > 1]
        for (src, key, variant) in mixed.index.tolist():
            dropped_notes.append(
                f"{Path(src).name} | {rule['name']} | {key} | {variant} "
                "(mencampur beberapa keluarga benchmark)")
        rdf = rdf.merge(
            family_counts.rename("family_count").reset_index(),
            on=["source_file", "pair_key", "variant"])
        rdf = rdf[rdf["family_count"] == 1].drop(columns="family_count")
        if rdf.empty:
            continue

        var_a, var_b = rule["var_a"], rule["var_b"]
        pivot = rdf.pivot_table(
            index=["source_file", "run_date", "pair_key"],
            columns="variant",
            values=["time_ns", "throughput_MB_s"],
            aggfunc="median",
        )
        pivot.columns = [f"{metric}_{column}" for metric, column in pivot.columns]
        pivot = pivot.reset_index()
        time_cols = [f"time_ns_{var_a}", f"time_ns_{var_b}"]
        if not all(col in pivot.columns for col in time_cols):
            continue

        part = pivot.dropna(subset=time_cols).copy()
        part.rename(columns={
            f"time_ns_{var_a}": "time_a_ns",
            f"time_ns_{var_b}": "time_b_ns",
            f"throughput_MB_s_{var_a}": "throughput_a_MB_s",
            f"throughput_MB_s_{var_b}": "throughput_b_MB_s",
        }, inplace=True)
        part["rule"] = rule["name"]
        part["variant_a"] = var_a
        part["variant_b"] = var_b
        part["interpretation"] = rule["note"]
        part["ratio_a_over_b"] = part["time_a_ns"] / part["time_b_ns"]  # >1: B lebih cepat
        part["time_reduction_pct"] = (1.0 - part["time_b_ns"] / part["time_a_ns"]) * 100.0
        pair_frames.append(part)

    if not pair_frames:
        return pd.DataFrame(), dropped_notes
    return pd.concat(pair_frames, ignore_index=True, sort=False), dropped_notes


pairs_all_df = pd.DataFrame()
pair_engine_notes = []
if not bench_df.empty:
    pairs_all_df, pair_engine_notes = build_named_pairs(
        bench_df[bench_df["valid_measurement"]], PAIR_RULES)

if pair_engine_notes:
    print("Kandidat pasangan DIBUANG karena ambigu:")
    for note in pair_engine_notes:
        print(f"  - {note}")

if pairs_all_df.empty:
    print("Tidak ada pasangan dalam-run yang valid per aturan PAIR_RULES.")
else:
    print(f"Total pasangan dalam-run: {len(pairs_all_df)} — rincian per aturan:")
    display(pairs_all_df.groupby("rule").agg(
        pairs=("pair_key", "count"), files=("source_file", "nunique"),
        interpretation=("interpretation", "first"),
    ).reset_index())
    display(pairs_all_df.sort_values(["rule", "source_file", "pair_key"]))

# Subset kompatibel dengan tabel lama (Scanner8 vs Scanner64).
scanner_pairs_df = pd.DataFrame()
if not pairs_all_df.empty:
    scanner_pairs_df = pairs_all_df[pairs_all_df["rule"] == "scanner8_vs_64"].copy()
    if not scanner_pairs_df.empty:
        scanner_pairs_df["scanner_pair_key"] = scanner_pairs_df["pair_key"]
        scanner_pairs_df["speedup_64_vs_8"] = (
            scanner_pairs_df["time_a_ns"] / scanner_pairs_df["time_b_ns"])
        scanner_pairs_df["time_reduction_pct"] = (
            1.0 - scanner_pairs_df["time_b_ns"] / scanner_pairs_df["time_a_ns"]) * 100.0

# %% [markdown]
# ## 5b. Slope eksperimen A1 (span iso) — uji hipotesis H1/H2 per fill
#
# `spaniso_interleaved*` menyintesis payload dengan jumlah token struktural
# KONSTAN (4096) dan filler yang berkembang (Pad = 0/8/64/256; pad byte
# utilisasi space/space digit). Memeriksa perubahan per byte:
#
# - **slope Scanner64** — slope "real" (span + tulis tape + bookkeeping)
# - **slope ByteLoopReplica** — isolasi murni loop span non-string (H1: dominasi)
# - **slope SWARSetReplica / SWARAdvanceReplica** — kandidat Tahap-6 (H2)
#
# Ukuran payload presisi dari konstruksi generator: 4096·Pad + 8199 byte.
# Slope = kemiringan regresi linear median-time vs ukuran tersebut; dibanding
# hanya antar-varian pada artefact/run yang SAMA (hukum pertama). Proyeksi
# gain E2E ≈ fraksi_span_byte × (1 − slope_SWAR/slope_asli).

# %%
def span_iso_slope_cell():
    if bench_df.empty:
        return pd.DataFrame()
    run = bench_df[
        bench_df["artifact_stem"].str.startswith("spaniso_interleaved")
        & ~bench_df["artifact_stem"].str.endswith(("_r2", "_run2"))
    ].copy()
    run = run[run["valid_measurement"]]  # run utama saja, bukan replika noise
    if run.empty:
        print("Artefak spaniso_interleaved belum ada — sel slope dilewati.")
        return pd.DataFrame()

    parts = run["benchmark_name"].str.extract(
        r"BM_SpanIso_(?P<variant>Scanner64|ByteLoopReplica|SWARSetReplica"
        r"|SWARAdvanceReplica)_Fill(?P<fill>WS|Num)/Pad:(?P<pad>\d+)$")
    run = pd.concat([run, parts], axis=1).dropna(
        subset=["variant", "fill", "pad"])
    run["pad"] = run["pad"].astype(int)
    # ukuran payload presisi dari konstruksi generator: 4096*Pad + 8199.
    run["payload_bytes"] = 4096 * run["pad"] + 8199

    med_time = run.groupby(
        ["source_file", "variant", "fill", "pad"], as_index=False).agg(
            time_ns=("time_ns", "median"),
            payload_bytes=("payload_bytes", "first"),
    )

    slope_rows = []
    for (src, variant, fill), sub in med_time.groupby(
            ["source_file", "variant", "fill"]):
        sub = sub.sort_values("pad")
        slope, intercept = np.polyfit(
            sub["payload_bytes"].to_numpy(dtype=float),
            sub["time_ns"].to_numpy(dtype=float), 1)
        r = np.corrcoef(
            sub["payload_bytes"].to_numpy(dtype=float),
            sub["time_ns"].to_numpy(dtype=float))[0, 1]
        slope_rows.append({
            "source_file": Path(src).name,
            "artifact_stem": Path(src).stem,
            "variant": variant,
            "fill": fill,
            "pads": "; ".join(map(str, sub["pad"])),
            "slope_ns_per_byte": float(slope),
            "intercept_ns": float(intercept),
            "R2": float(r * r),
        })
    slope_df = pd.DataFrame(slope_rows).sort_values(
        ["source_file", "fill", "variant"])
    display(slope_df)

    print("\nH1 — seberapa besar kontribusi loop span terhadap slope scanner64:")
    for (src, fill), sub in slope_df.groupby(["source_file", "fill"]):
        by_variant = sub.set_index("variant")["slope_ns_per_byte"]
        if "scanner64" in by_variant and "byte_loop" in by_variant:
            sc = float(by_variant["scanner64"])
            bl = float(by_variant["byte_loop"])
            print(
                f"  {Path(src).stem} | {fill}: scanner64 = {sc:.4f} ns/B, "
                f"byte_loop = {bl:.4f} ns/B "
                f"({(bl / max(sc, 1e-9)) * 100:.1f}% dari slope scanner64)")
    print("\nH2 — slope kandidat SWAR relatif terhadap byte_loop:")
    for (src, fill), sub in slope_df.groupby(["source_file", "fill"]):
        by_variant = sub.set_index("variant")["slope_ns_per_byte"]
        if "byte_loop" not in by_variant:
            continue
        bl = max(float(by_variant["byte_loop"]), 1e-9)
        for candidate in ("swar_set", "swar_advance"):
            if candidate in by_variant:
                cv = float(by_variant[candidate])
                print(
                    f"  {Path(src).stem} | {fill}: {candidate} = {cv:.4f} "
                    f"ns/B ({(cv / bl) * 100:.1f}% dari byte_loop)")
    return slope_df

spaniso_slope_df = span_iso_slope_cell()

# %% [markdown]
# ## 6. Perbandingan file before/baseline vs after (eksploratif)
#
# Tabel ini hanya memasangkan **nama benchmark yang identik** di artefak dengan
# penanda `before`/`baseline` dan `after`. Hasil lintas file bukan bukti kausal:
# bisa ada drift clock, beban sistem, perubahan build, atau perubahan workload.
# Utamakan eksperimen interleaved jika ingin menyimpulkan efek optimasi.

# %%
def artifact_role_and_family(stem: str):
    clean = re.sub(r"_(?:run|r)\d+$", "", stem, flags=re.IGNORECASE)
    if re.search(r"(?:^|_)before(?:_|$)", clean, flags=re.IGNORECASE):
        role = "before"
    elif re.search(r"(?:^|_)baseline(?:_|$)", clean, flags=re.IGNORECASE):
        role = "before"
    elif re.search(r"(?:^|_)after(?:_|$)", clean, flags=re.IGNORECASE):
        role = "after"
    else:
        return None, None
    family = re.sub(r"_(?:before|baseline|after)(?:_.*)?$", "", clean, flags=re.IGNORECASE)
    return role, family


before_after_df = pd.DataFrame()
if not bench_df.empty:
    paired_source = bench_df[bench_df["valid_measurement"]].copy()
    role_family = paired_source["artifact_stem"].apply(artifact_role_and_family)
    paired_source["artifact_role"] = role_family.apply(lambda item: item[0])
    paired_source["experiment_family"] = role_family.apply(lambda item: item[1])
    paired_source = paired_source[paired_source["artifact_role"].notna()]

    if not paired_source.empty:
        medians = paired_source.groupby(
            ["experiment_family", "artifact_role", "benchmark_name"], dropna=False
        ).agg(
            median_time_ns=("time_ns", "median"),
            artifacts=("source_file", lambda s: "; ".join(sorted(set(s)))),
            files_count=("source_file", "nunique"),
        ).reset_index()
        before = medians[medians["artifact_role"] == "before"].drop(columns="artifact_role")
        after = medians[medians["artifact_role"] == "after"].drop(columns="artifact_role")
        before = before.rename(columns={
            "median_time_ns": "before_time_ns",
            "artifacts": "before_files",
            "files_count": "before_file_count",
        })
        after = after.rename(columns={
            "median_time_ns": "after_time_ns",
            "artifacts": "after_files",
            "files_count": "after_file_count",
        })
        before_after_df = before.merge(after, on=["experiment_family", "benchmark_name"], how="inner")
        if not before_after_df.empty:
            before_after_df["before_over_after"] = before_after_df["before_time_ns"] / before_after_df["after_time_ns"]
            before_after_df["time_change_pct"] = (
                before_after_df["after_time_ns"] / before_after_df["before_time_ns"] - 1
            ) * 100

if before_after_df.empty:
    print("Tidak ada benchmark bernama sama yang tersedia pada artefak before/baseline dan after.")
else:
    display(before_after_df.sort_values(["experiment_family", "benchmark_name"]))

# Ampelas bacaan: tandai delta yang masuk rentang noise run-ulang (§3b) supaya
# pembaca tidak menafsirkan perbedaan kecil sebagai efek nyata.
if not before_after_df.empty:
    before_after_df["family"] = before_after_df["benchmark_name"].map(family_from_name)
    before_after_df["noise_band_pct"] = before_after_df["family"].map(noise_band_by_family)
    before_after_df["signal"] = np.where(
        before_after_df["noise_band_pct"].isna(),
        "tanpa ukuran noise",
        np.where(
            before_after_df["time_change_pct"].abs() > before_after_df["noise_band_pct"],
            "di atas noise",
            "dalam noise",
        ),
    )
    before_after_df.drop(columns="family", inplace=True)
    print("Kolom signal menilai |time_change_pct| terhadap lantai noise run-ulang (§3b).")

# %% [markdown]
# ## 6b. Perbandingan dua revisi (hash A vs B) — STATUS DESKRIPTIF
#
# HUKUM PERTAMA: angka absolut lintas-proses di mesin ini bergetar 15–45%.
# Tabel di bawah HANYA deskriptif; keputusan optimasi tetap harus dari rasio
# dalam-run (§5) atau replika bersebelahan. Nilai tambah sel ini: rasio A/B
# per benchmark bernama sama + penanda apakah delta melebihi lantai noise
# run-ulang dari §3b (pertolongan dangkal pertama sebelum menulis benchmark
# interleaved baru). Isi `REV_*_OVERRIDE` untuk memilih pasangan tertentu.

# %%
REV_A_OVERRIDE = None  # hash "baseline", mis. "e408674"
REV_B_OVERRIDE = None  # hash "kandidat", mis. "05d2180"

revision_compare_df = pd.DataFrame()
revision_pair_note = ""
if not bench_df.empty and bench_df["revision"].notna().any():
    rev_stats = (
        bench_df[bench_df["revision"].notna()]
        .groupby("revision")
        .agg(latest_date=("run_date", "max"), files=("source_file", "nunique"))
    )
    ordered = rev_stats.sort_values(
        ["latest_date"], na_position="first").index.tolist()
    if len(ordered) >= 2:
        rev_a = str(REV_A_OVERRIDE or ordered[-2])
        rev_b = str(REV_B_OVERRIDE or ordered[-1])
        valid_rev = bench_df[bench_df["valid_measurement"] & bench_df["revision"].notna()]
        med_a = valid_rev[valid_rev["revision"] == rev_a].groupby("benchmark_name")["time_ns"].median()
        med_b = valid_rev[valid_rev["revision"] == rev_b].groupby("benchmark_name")["time_ns"].median()
        common = med_a.index.intersection(med_b.index)
        if len(common):
            revision_compare_df = pd.DataFrame({
                "time_a_ns": med_a[common], "time_b_ns": med_b[common],
            })
            revision_compare_df["ratio_a_over_b"] = (
                revision_compare_df["time_a_ns"] / revision_compare_df["time_b_ns"])
            revision_compare_df["delta_pct_b_vs_a"] = (
                revision_compare_df["time_b_ns"] / revision_compare_df["time_a_ns"] - 1) * 100
            revision_compare_df["family"] = revision_compare_df.index.map(family_from_name)
            revision_compare_df["noise_band_pct"] = revision_compare_df["family"].map(noise_band_by_family)
            revision_compare_df["signal"] = np.where(
                revision_compare_df["noise_band_pct"].isna(),
                "tanpa ukuran noise",
                np.where(
                    revision_compare_df["delta_pct_b_vs_a"].abs() > revision_compare_df["noise_band_pct"],
                    "di atas noise",
                    "dalam noise",
                ),
            )
            revision_pair_note = f"{rev_a} vs {rev_b}"
            print(
                f"Revisi A: {rev_a} (terakhir: {rev_stats.loc[rev_a, 'latest_date']}, "
                f"{int(rev_stats.loc[rev_a, 'files'])} file) vs "
                f"Revisi B: {rev_b} (terakhir: {rev_stats.loc[rev_b, 'latest_date']}, "
                f"{int(rev_stats.loc[rev_b, 'files'])} file); "
                f"{len(common)} benchmark bernama sama.")
            display(revision_compare_df.sort_index())
        else:
            print("Tidak ada nama benchmark yang sama antara dua revisi terbaru.")
    else:
        print("Belum cukup revisi berlabel hash untuk perbandingan (perlu >=2).")

# %% [markdown]
# ## 7. Analisis hasil Google Test
#
# Dibentuk dua lapisan data yang terpisah:
# 1. ringkasan `summary_*.json` (per kelompok dan preset);
# 2. laporan Google Test per file (suite, test case, dan pesan assertion gagal).
#
# Pemisahan ini mencegah satu run dihitung sekaligus dari ringkasan dan laporan
# individual. Nama status kasus test diturunkan dari `failures` dan metadata,
# karena `status=RUN` atau `result=COMPLETED` saja tidak menjamin assertion lulus.

# %%
def duration_to_ms(value) -> float:
    if value is None:
        return 0.0
    if isinstance(value, (int, float)):
        return float(value) * 1000.0  # angka Google Test pada skema ini umumnya detik
    text = str(value).strip()
    if not text:
        return 0.0
    try:
        if text.endswith("ms"):
            return float(text[:-2])
        if text.endswith("us") or text.endswith("µs") or text.endswith("μs"):
            return float(re.sub(r"(?:us|µs|μs)$", "", text)) / 1000.0
        if text.endswith("s"):
            return float(text[:-1]) * 1000.0
        return float(text) * 1000.0
    except ValueError:
        return np.nan


def test_run_status(original_status, tests, failures, errors):
    status = str(original_status or "").upper()
    if status in {"RUN_FAILED", "REPORT_MISSING", "FAILED_TO_RUN"}:
        return status
    failure_count = to_float(failures)
    error_count = to_float(errors)
    test_count = to_float(tests)
    if (pd.notna(failure_count) and failure_count > 0) or (pd.notna(error_count) and error_count > 0):
        return "FAILED"
    if pd.isna(test_count) or test_count <= 0:
        return "NO_TESTS"
    return "PASSED"


TEST_SUMMARY_ROWS = []
SUMMARY_TEST_DETAIL_ROWS = []
RAW_TEST_RUN_ROWS = []
RAW_TEST_CASE_ROWS = []
RAW_FAILURE_ROWS = []

for rec in JSON_RECORDS:
    payload = rec["data"]
    if not isinstance(payload, dict):
        continue

    if rec["schema"] == "test_summary":
        summary_hash = payload.get("hash") or revision_from(rec["artifact_stem"], payload)
        generated_at = payload.get("generated_at")
        for group in payload.get("groups", []):
            tests = safe_int(group.get("tests"))
            failures = safe_int(group.get("failures"))
            disabled = safe_int(group.get("disabled"))
            errors = safe_int(group.get("errors"))
            row = {
                "summary_file": rec["source_file"],
                "summary_hash": summary_hash,
                "generated_at": generated_at,
                "group": group.get("group"),
                "preset": group.get("preset"),
                "status_reported": group.get("status"),
                "status_derived": test_run_status(group.get("status"), tests, failures, errors),
                "tests": tests,
                "failures": failures,
                "disabled": disabled,
                "errors": errors,
                "passed_estimate": max(0, tests - failures - disabled - errors),
                "time_ms": to_float(group.get("time_ms")),
                "report_file": str(group.get("report") or "").replace("\\", "/"),
                "command": group.get("command"),
            }
            TEST_SUMMARY_ROWS.append(row)
            for detail in group.get("detail", []) or []:
                SUMMARY_TEST_DETAIL_ROWS.append({
                    "summary_file": rec["source_file"],
                    "summary_hash": summary_hash,
                    "group": group.get("group"),
                    "preset": group.get("preset"),
                    "suite": detail.get("suite"),
                    "test_name": detail.get("test"),
                    "reported_status": detail.get("status"),
                    "reported_result": detail.get("result"),
                    "time_ms": to_float(detail.get("time_ms")),
                })

    elif rec["schema"] == "gtest_report":
        match = re.match(r"(.+?)_(native-debug|native-release)_([0-9a-fA-F]{7,40})$", rec["artifact_stem"])
        if match:
            group_name, preset, rev = match.group(1), match.group(2), match.group(3)
        else:
            match = re.match(r"(.+?)_([0-9a-fA-F]{7,40})$", rec["artifact_stem"])
            if match:
                group_name, rev = match.group(1), match.group(2)
            else:
                group_name, rev = rec["artifact_stem"], None
            preset = None

        total_tests = safe_int(payload.get("tests"))
        total_failures = safe_int(payload.get("failures"))
        total_disabled = safe_int(payload.get("disabled"))
        total_errors = safe_int(payload.get("errors"))
        RAW_TEST_RUN_ROWS.append({
            "source_file": rec["source_file"],
            "group": group_name,
            "preset": preset,
            "revision": rev,
            "timestamp": payload.get("timestamp"),
            "tests": total_tests,
            "failures": total_failures,
            "disabled": total_disabled,
            "errors": total_errors,
            "passed_estimate": max(0, total_tests - total_failures - total_disabled - total_errors),
            "time_ms": duration_to_ms(payload.get("time")),
            "status": test_run_status(None, total_tests, total_failures, total_errors),
        })

        for suite in payload.get("testsuites", []) or []:
            suite_name = suite.get("name")
            for case in suite.get("testsuite", []) or []:
                case_failures = case.get("failures", []) or []
                if not isinstance(case_failures, list):
                    case_failures = [case_failures]
                failure_text = "\n\n".join(
                    str(item.get("failure", item)) if isinstance(item, dict) else str(item)
                    for item in case_failures
                )
                case_status_text = str(case.get("status") or "").upper()
                case_result_text = str(case.get("result") or "").upper()
                if case_failures or case_result_text in {"FAILURE", "FAILED"}:
                    case_status = "FAILED"
                elif "DISABLED" in case_status_text or "DISABLED" in str(case.get("name", "")).upper():
                    case_status = "DISABLED"
                elif case_result_text == "COMPLETED" or case_status_text == "RUN":
                    case_status = "PASSED"
                else:
                    case_status = "UNKNOWN"

                case_row = {
                    "source_file": rec["source_file"],
                    "group": group_name,
                    "preset": preset,
                    "revision": rev,
                    "suite": suite_name,
                    "test_name": case.get("name"),
                    "status": case_status,
                    "reported_status": case.get("status"),
                    "reported_result": case.get("result"),
                    "time_ms": duration_to_ms(case.get("time")),
                    "failure_count": len(case_failures),
                    "failure_text": failure_text,
                    "file": case.get("file"),
                    "line": case.get("line"),
                }
                RAW_TEST_CASE_ROWS.append(case_row)
                if case_status == "FAILED":
                    RAW_FAILURE_ROWS.append(case_row.copy())


test_summary_df = pd.DataFrame(TEST_SUMMARY_ROWS)
summary_test_detail_df = pd.DataFrame(SUMMARY_TEST_DETAIL_ROWS)
test_runs_df = pd.DataFrame(RAW_TEST_RUN_ROWS)
test_cases_df = pd.DataFrame(RAW_TEST_CASE_ROWS)
test_failures_df = pd.DataFrame(RAW_FAILURE_ROWS)

print("Ringkasan test per grup/preset:", len(test_summary_df))
print("Laporan Google Test individual:", len(test_runs_df))
print("Kasus test dari laporan individual:", len(test_cases_df))
print("Kasus gagal yang terdeteksi dari laporan individual:", len(test_failures_df))
if not test_summary_df.empty:
    display(test_summary_df.sort_values(["generated_at", "preset", "group"], na_position="first").tail(30))
if not test_failures_df.empty:
    display(test_failures_df[["source_file", "group", "preset", "suite", "test_name", "time_ms", "failure_text"]])

# %% [markdown]
# ## 8. Audit konsistensi ringkasan test terhadap laporan individual
#
# Bila `report_file` pada ringkasan tersedia di kumpulan file, bandingkan jumlah
# test dan kegagalan. Ketidakcocokan bisa terjadi karena artefak berasal dari
# revisi berbeda atau laporan individualnya tidak ikut tersimpan.

# %%
TEST_AUDIT_ROWS = []
if not test_summary_df.empty:
    raw_run_lookup = {
        row["source_file"]: row
        for row in RAW_TEST_RUN_ROWS
    }
    for _, summary_row in test_summary_df.iterrows():
        report_path = str(summary_row.get("report_file") or "").replace("\\", "/")
        raw = raw_run_lookup.get(report_path)
        if raw is None:
            TEST_AUDIT_ROWS.append({
                "summary_file": summary_row["summary_file"],
                "group": summary_row["group"],
                "preset": summary_row["preset"],
                "report_file": report_path,
                "raw_report_found": False,
                "tests_match": None,
                "failures_match": None,
                "note": "Laporan individual tidak tersedia pada kumpulan file yang dipindai.",
            })
        else:
            tests_match = int(summary_row["tests"]) == int(raw["tests"])
            failures_match = int(summary_row["failures"]) == int(raw["failures"])
            TEST_AUDIT_ROWS.append({
                "summary_file": summary_row["summary_file"],
                "group": summary_row["group"],
                "preset": summary_row["preset"],
                "report_file": report_path,
                "raw_report_found": True,
                "tests_match": tests_match,
                "failures_match": failures_match,
                "note": "OK" if tests_match and failures_match else "Perlu periksa revisi/sumber artefak.",
            })

test_audit_df = pd.DataFrame(TEST_AUDIT_ROWS)
if not test_audit_df.empty:
    display(test_audit_df)
else:
    print("Tidak ada ringkasan test untuk diaudit.")

# %% [markdown]
# ## 8b. Audit bukti artefak tahap optimasi
#
# Disiplin proyek: "benchmark tanpa artefak dianggap tidak terjadi"
# (docs/development/benchmarking.md). Tabel ini memeriksa keberadaan artefak
# bukti per tahap di `out/results/benchmark/` — pemeriksaan keberadaan saja,
# bukan pengukuran ulang.

# %%
STAGE_EVIDENCE = [
    ("1 · scanner64 di create_session", "e408674", [
        "session_baseline_scanner8", "session_after_scanner64",
        "session_pipeline_interleaved",
    ]),
    ("2 · indeks root lazy", "23a12ee", [
        "rootwide_pathquery_baseline", "rootwide_pathquery_after_index",
    ]),
    ("3 · streaming array_next", "47e9cb7", [
        "arrayread_before", "arrayread_after_streaming",
    ]),
    ("4 · validasi UTF-8 terintegrasi + indeks elemen array", "8e0f3e3", [
        "tahap4_before", "tahap4_after", "tahap4_interleaved",
    ]),
    ("tahap5 · typedepth guard (eksperimen lanjutan)", None, [
        "tahap5_typedepth_guard",
    ]),
    ("eksperimen goto automaton (regresi, di-revert 560a6ef)", None, [
        "scanner_goto_interleaved", "scanner_revert_goto", "scanner_goto_final",
    ]),
]

stage_audit_rows = []
benchmark_stems = set(bench_df["artifact_stem"]) if not bench_df.empty else set()
for stage, commit, patterns in STAGE_EVIDENCE:
    for pattern in patterns:
        found = sorted(stem for stem in benchmark_stems if stem.startswith(pattern))
        stage_audit_rows.append({
            "stage": stage,
            "commit": commit,
            "artifact_pattern": pattern,
            "found": bool(found),
            "files": "; ".join(found) if found else None,
        })

stage_audit_df = pd.DataFrame(stage_audit_rows)
if not stage_audit_df.empty:
    display(stage_audit_df)
    missing = stage_audit_df[~stage_audit_df["found"]]
    if not missing.empty:
        print("Pola artefak TIDAK ditemukan:")
        display(missing)
else:
    print("Audit tahap dilewati (tidak ada benchmark mentah untuk dipetakan).")

# %% [markdown]
# ## 9. Tabel ringkasan dan pemeriksaan data

# %%
print("### Metadata benchmark per file")
if not bench_df.empty:
    benchmark_run_metadata = bench_df.groupby(
        ["source_file", "revision", "run_date", "host_name", "num_cpus", "mhz_per_cpu", "build_type", "library_version"],
        dropna=False,
    ).agg(
        benchmark_rows=("benchmark_name", "size"),
        valid_rows=("valid_measurement", "sum"),
        invalid_rows=("valid_measurement", lambda s: int((~s).sum())),
    ).reset_index().sort_values("run_date", na_position="first")
    display(benchmark_run_metadata)

    print("### Benchmark dengan throughput paling tinggi (hanya yang memiliki bytes/s)")
    throughput_top = bench_df[
        bench_df["valid_measurement"] & bench_df["throughput_MB_s"].notna()
    ].sort_values("throughput_MB_s", ascending=False).head(15)
    display(throughput_top[["source_file", "benchmark_name", "time_ns", "throughput_MB_s", "throughput_GiB_s", "iterations"]])

    print("### Error dari benchmark")
    benchmark_errors = bench_df[~bench_df["valid_measurement"]]
    if benchmark_errors.empty:
        print("Tidak ada baris invalid/error yang terdeteksi.")
    else:
        display(benchmark_errors[["source_file", "benchmark_name_raw", "iterations", "time_ns", "error_occurred", "error_message"]])

print("### Ringkasan test historis")
if not test_summary_df.empty:
    display(test_summary_df.groupby(["summary_hash", "preset", "status_derived"], dropna=False).agg(
        groups=("group", "nunique"),
        tests=("tests", "sum"),
        failures=("failures", "sum"),
        disabled=("disabled", "sum"),
        errors=("errors", "sum"),
        time_ms=("time_ms", "sum"),
    ).reset_index().sort_values(["summary_hash", "preset"]))

# %% [markdown]
# ## 10. Grafik tren benchmark mentah
#
# Grafik ini membantu menemukan pola dan anomali lintas artefak; ia bukan uji
# statistik dan bukan klaim optimasi. Hanya nama benchmark yang muncul di
# setidaknya dua artefak yang ditampilkan untuk mengurangi kepadatan grafik.

# %%
if not bench_df.empty:
    valid_for_trend = bench_df[bench_df["valid_measurement"] & bench_df["run_date"].notna()].copy()
    counts = valid_for_trend.groupby("benchmark_name")["source_file"].nunique().sort_values(ascending=False)
    recurring_names = counts[counts >= 2].head(8).index.tolist()

    if recurring_names:
        fig, ax = plt.subplots(figsize=(14, 7))
        for name in recurring_names:
            sub = valid_for_trend[valid_for_trend["benchmark_name"] == name].sort_values(["run_date", "source_file"])
            # Sumbu X adalah tanggal run; nama artefak lengkap tetap ada di tabel benchmark_run_metadata.
            x = sub["run_date"].dt.tz_convert(None)
            ax.plot(x, sub["time_ns"], marker="o", label=name)
        ax.set_yscale("log")
        ax.set_title("Waktu benchmark pada nama test yang berulang (deskriptif)")
        ax.set_xlabel("Pengamatan per benchmark (urutan data mengikuti tanggal run)")
        ax.set_ylabel("Waktu real per operasi (ns, skala log)")
        ax.grid(True, which="both", alpha=0.25)
        ax.legend(loc="best", fontsize=8)
        plt.tight_layout()
        plt.show()
    else:
        print("Belum ada nama benchmark yang berulang pada dua artefak atau lebih.")

# %% [markdown]
# ## 11. Grafik pasangan scanner dan perbandingan JavaScript

# %%
if not pairs_all_df.empty:
    for rule_name, rule_pairs in pairs_all_df.sort_values(
            ["source_file", "pair_key"]).groupby("rule"):
        plot_label = (
            rule_pairs["source_file"].map(lambda p: Path(p).name)
            + " | " + rule_pairs["pair_key"].astype(str))
        fig, ax = plt.subplots(figsize=(14, max(4, len(rule_pairs) * 0.35)))
        ax.barh(plot_label, rule_pairs["ratio_a_over_b"])
        ax.axvline(1.0, linestyle="--", linewidth=1)
        title_pair = (
            f"{rule_pairs['variant_a'].iloc[0]} vs {rule_pairs['variant_b'].iloc[0]}"
            f" ({rule_name})")
        ax.set_title(
            f"{title_pair} — rasio = waktu_a / waktu_b (>1 = varian B lebih cepat)")
        ax.set_xlabel(f"Rasio (skala log; interpretasi: {rule_pairs['interpretation'].iloc[0]})")
        ax.set_ylabel("File | pasangan benchmark")
        ax.set_xscale("log")
        ax.grid(True, axis="x", alpha=0.25)
        plt.tight_layout()
        plt.show()
else:
    print("Grafik pasangan dilewati karena tidak ada pasangan dalam-run yang valid.")

if not js_sizes_df.empty:
    fig, ax = plt.subplots(figsize=(12, 7))
    for metric, sub in js_sizes_df.groupby("metric"):
        sub = sub.dropna(subset=["payload_bytes", "time_us"]).sort_values("payload_bytes")
        if not sub.empty:
            ax.plot(sub["payload_bytes"], sub["time_us"], marker="o", label=metric)
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_title("Benchmark JS berdasarkan ukuran payload")
    ax.set_xlabel("Payload (byte, skala log)")
    ax.set_ylabel("Waktu per operasi (µs/op, skala log; lebih rendah = lebih cepat)")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(loc="best", fontsize=8)
    plt.tight_layout()
    plt.show()
else:
    print("Tidak ada artefak js_sizes untuk digrafikkan.")

if not js_parse_df.empty:
    fig, ax = plt.subplots(figsize=(12, 6))
    # Grafik memakai latency turunan dari ops/s; ops/s lebih tinggi berarti waktu per operasi lebih rendah.
    for metric, sub in js_parse_df.groupby("metric"):
        sub = sub.dropna(subset=["time_us_per_op"]).sort_values("dataset")
        if not sub.empty:
            ax.plot(sub["dataset"], sub["time_us_per_op"], marker="o", label=metric)
    ax.set_yscale("log")
    ax.set_title("JSON.parse vs ze_json WASM: latency turunan dari ops/s")
    ax.set_xlabel("Dataset")
    ax.set_ylabel("Waktu turunan (µs/op, skala log; lebih rendah = lebih cepat)")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(loc="best")
    plt.xticks(rotation=20, ha="right")
    plt.tight_layout()
    plt.show()
else:
    print("Tidak ada artefak js_parse untuk digrafikkan.")

# %% [markdown]
# ## 12. Grafik hasil test

# %%
if not test_summary_df.empty:
    # Gunakan setiap summary hash sebagai satu run historis; jangan gabungkan dengan laporan mentah.
    test_chart = test_summary_df.groupby(["summary_hash", "preset"], dropna=False).agg(
        passed=("passed_estimate", "sum"),
        failures=("failures", "sum"),
        disabled=("disabled", "sum"),
        errors=("errors", "sum"),
        total_tests=("tests", "sum"),
        time_ms=("time_ms", "sum"),
    ).reset_index()
    display(test_chart.sort_values(["summary_hash", "preset"]))

    latest_hash = None
    if "generated_at" in test_summary_df.columns:
        generated = pd.to_datetime(test_summary_df["generated_at"], errors="coerce", utc=True)
        if generated.notna().any():
            latest_hash = test_summary_df.loc[generated.idxmax(), "summary_hash"]
    latest_test_chart = test_chart[test_chart["summary_hash"] == latest_hash] if latest_hash else test_chart.tail(10)

    if not latest_test_chart.empty:
        fig, ax = plt.subplots(figsize=(10, 5))
        latest_test_chart.set_index("preset")[["passed", "failures", "disabled", "errors"]].plot(
            kind="bar", stacked=True, ax=ax
        )
        ax.set_title(f"Hasil test menurut preset — summary {latest_hash or 'terpilih'}")
        ax.set_xlabel("Preset")
        ax.set_ylabel("Jumlah test")
        ax.grid(True, axis="y", alpha=0.25)
        plt.xticks(rotation=0)
        plt.tight_layout()
        plt.show()

        fig, ax = plt.subplots(figsize=(8, 4))
        ax.bar(latest_test_chart["preset"].astype(str), latest_test_chart["time_ms"])
        ax.set_title(f"Total durasi laporan test — summary {latest_hash or 'terpilih'}")
        ax.set_xlabel("Preset")
        ax.set_ylabel("Waktu total tercatat (ms)")
        ax.grid(True, axis="y", alpha=0.25)
        plt.tight_layout()
        plt.show()
else:
    print("Tidak ada summary test JSON untuk dibuat grafik.")

# %% [markdown]
# ## 13. Ringkasan otomatis dan peringatan interpretasi

# %%
print("=" * 84)
print("RINGKASAN ANALISIS ze_json")
print("=" * 84)
print(f"Repo: {REPO_ROOT}")
print(f"File JSON yang dipindai: {len(inventory_df)}")
if not inventory_df.empty:
    print(f"File JSON tidak valid: {int((~inventory_df['valid_json']).sum())}")
if not bench_df.empty:
    print(f"Artefak benchmark mentah: {bench_df['source_file'].nunique()}")
    print(f"Pengukuran valid: {int(bench_df['valid_measurement'].sum())} / {len(bench_df)}")
    if not scanner_pairs_df.empty:
        faster = int((scanner_pairs_df["speedup_64_vs_8"] > 1).sum())
        slower = int((scanner_pairs_df["speedup_64_vs_8"] < 1).sum())
        equal = int(np.isclose(scanner_pairs_df["speedup_64_vs_8"], 1).sum())
        print(f"Pasangan Scanner8/64 dalam run sama: {len(scanner_pairs_df)}")
        print(f"  Scanner64 lebih cepat: {faster}; lebih lambat: {slower}; setara kira-kira: {equal}")
    if not pairs_all_df.empty:
        print("Pasangan dalam-run per aturan (varian B senderannya):")
        for rule_name, rule_sub in pairs_all_df.groupby("rule"):
            b_faster = int((rule_sub["ratio_a_over_b"] > 1).sum())
            print(f"  {rule_name}: {len(rule_sub)} pasangan (B lebih cepat: {b_faster})")
    if not noise_floor_df.empty:
        worst = noise_floor_df.nlargest(3, "spread_pct")
        median_spread = float(noise_floor_df["spread_pct"].median())
        print(f"Lantai noise run-ulang: median spread {median_spread:.1f}%; 3 getaran terburuk:")
        for _, noise_row in worst.iterrows():
            print(f"  {noise_row['benchmark_name']} ({Path(noise_row['files'].split(';')[0].strip()).name}): {noise_row['spread_pct']:.1f}%")
    if revision_pair_note:
        above_noise = int((revision_compare_df["signal"] == "di atas noise").sum())
        print(f"Perbandingan revisi (DESKRIPTIF): {revision_pair_note} — {len(revision_compare_df)} benchmark, {above_noise} delta di atas lantai noise run-ulang")
if not stage_audit_df.empty:
    n_missing = int((~stage_audit_df["found"]).sum())
    print(f"Audit bukti tahap 1–5: {int(stage_audit_df['found'].sum())}/{len(stage_audit_df)} pola artefak ditemukan" + (f" ({n_missing} hilang)" if n_missing else ""))
if not js_breakeven_df.empty:
    for _, breakeven_row in js_breakeven_df.iterrows():
        be = breakeven_row["break_even_bytes"]
        be_text = f"{be/1024:.1f} KiB" if be else "tidak menang pada ukuran teruji"
        print(f"JS break-even {breakeven_row['workload']} ({Path(breakeven_row['source_file']).name}): {be_text}")
if not test_summary_df.empty:
    print(f"Baris grup test dari summary: {len(test_summary_df)}")
    print(f"Jumlah test pada semua summary (setiap revisi/preset dihitung sebagai run tersendiri): {int(test_summary_df['tests'].sum())}")
    print(f"Jumlah kegagalan pada semua summary (bukan jumlah unik kasus): {int(test_summary_df['failures'].sum())}")
if not test_failures_df.empty:
    print(f"Kasus gagal historis dalam laporan individual: {len(test_failures_df)}")
    print("Periksa tabel test_failures_df di atas; file laporan lama dapat berasal dari revisi berbeda.")

print("\nPeringatan interpretasi:")
print("1. Jangan simpulkan sebab-akibat hanya dari perbandingan angka absolut lintas proses/file.")
print("2. Bandingkan Scanner8 dan Scanner64 terutama dari pasangan yang tercatat di file/run yang sama.")
print("3. Google Benchmark error atau 0 ns/op dikeluarkan dari analisis performa, tetapi tetap ditampilkan sebagai anomali.")
print("4. JS js_parse disimpan awalnya sebagai ops/s; notebook menghitung latency turunan 1e6 / ops/s untuk visualisasi.")
print("5. Laporan test dari commit/preset berbeda adalah riwayat, bukan gambaran tunggal status HEAD saat ini.")
print("6. Lantai noise §3b diukur DARI artefak run-ulang; delta di dalam band tidak boleh ditafsiri sebagai efek.")
print("7. Perbandingan revisi §6b deskriptif — keputusan optimasi tetap wajib rasio dalam-run atau benchmark interleaved.")
print("8. Pasangan pola kerja berbeda (mis. array_counted_vs_streaming) bukan kandidat kecepatan yang setara.")

# %% [markdown]
# ## 14. Ekspor tabel (opsional)
#
# Default tidak menulis apa pun ke repo. Ubah `EXPORT_TABLES = True` jika ingin
# menyimpan tabel hasil analisis ke `out/analysis/`. File input tidak diubah.

# %%
EXPORT_TABLES = True
EXPORT_DIR = REPO_ROOT / "out" / "analysis"

if EXPORT_TABLES:
    EXPORT_DIR.mkdir(parents=True, exist_ok=True)
    export_tables = {
        "file_inventory.csv": inventory_df,
        "benchmark_raw.csv": bench_df,
        "benchmark_summary_rows.csv": summary_bench_df,
        "benchmark_pairs_same_run.csv": pairs_all_df,
        "scanner_pairs_same_run.csv": scanner_pairs_df,
        "spaniso_slope.csv": spaniso_slope_df,
        "noise_floor_replicas.csv": noise_floor_df,
        "benchmark_before_after_exploratory.csv": before_after_df,
        "revision_compare.csv": revision_compare_df,
        "js_sizes_long.csv": js_sizes_df,
        "js_sizes_comparison.csv": js_sizes_wide,
        "js_sizes_breakeven.csv": js_breakeven_df,
        "js_sizes_cross_revision.csv": js_sizes_crossrev_df,
        "js_parse_long.csv": js_parse_df,
        "js_parse_comparison.csv": js_parse_wide,
        "stage_evidence_audit.csv": stage_audit_df,
        "test_summary_groups.csv": test_summary_df,
        "test_raw_runs.csv": test_runs_df,
        "test_raw_cases.csv": test_cases_df,
        "test_failures.csv": test_failures_df,
        "test_summary_audit.csv": test_audit_df,
    }
    for filename, frame in export_tables.items():
        frame.to_csv(EXPORT_DIR / filename, index=False, encoding="utf-8-sig")
    print(f"Tabel tersimpan di: {EXPORT_DIR}")
else:
    print("Ekspor nonaktif. Untuk menyimpan tabel, ubah EXPORT_TABLES = True pada sel terakhir.")

# %%
