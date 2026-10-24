# %% [markdown]
# # Analisis hasil benchmark dan test `ze_json`
#
# Notebook ini membaca artefak lokal dari `out/results/benchmark/` dan
# `out/results/test/`. Ia mendukung Google Benchmark JSON, ringkasan benchmark,
# metrik JavaScript, laporan Google Test individual, dan ringkasan lintas preset.
#
# **Catatan metodologis:** grafik tren lintas file bersifat deskriptif. Dokumen
# `docs/development/benchmarking.md` di repo memperingatkan agar angka absolut
# lintas proses tidak diperlakukan sebagai bukti kausal. Untuk perbandingan
# scanner, notebook memprioritaskan pasangan Scanner8/Scanner64 yang berada di
# file/run benchmark yang sama. Angka JS `ops/s` dan `µs/op` juga tidak dicampur.

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
REPO_ROOT_OVERRIDE = Path(__file__).resolve().parents[1]


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
# ## 5. Analisis scanner yang dipasangkan dalam satu file/run
#
# Rasio didefinisikan sebagai `waktu Scanner8 / waktu Scanner64`:
# - `> 1`: Scanner64 membutuhkan waktu lebih sedikit pada pasangan itu.
# - `= 1`: waktu sama.
# - `< 1`: Scanner64 membutuhkan waktu lebih banyak.
#
# Pasangan dibuat hanya dari file laporan mentah yang sama dan baris yang valid.

# %%
scanner_pairs_df = pd.DataFrame()
if not bench_df.empty:
    scanner_rows = bench_df[
        bench_df["valid_measurement"] & bench_df["scanner_variant"].notna()
    ].copy()
    if not scanner_rows.empty:
        pivot = scanner_rows.pivot_table(
            index=["source_file", "run_date", "scanner_pair_key"],
            columns="scanner_variant",
            values=["time_ns", "throughput_MB_s", "iterations"],
            aggfunc="median",
        )
        pivot.columns = [f"{metric}_scanner{variant}" for metric, variant in pivot.columns]
        pivot = pivot.reset_index()
        required = ["time_ns_scanner8", "time_ns_scanner64"]
        if all(col in pivot.columns for col in required):
            scanner_pairs_df = pivot.dropna(subset=required).copy()
            scanner_pairs_df["speedup_64_vs_8"] = (
                scanner_pairs_df["time_ns_scanner8"] / scanner_pairs_df["time_ns_scanner64"]
            )
            scanner_pairs_df["time_reduction_pct"] = (
                1 - scanner_pairs_df["time_ns_scanner64"] / scanner_pairs_df["time_ns_scanner8"]
            ) * 100
            if "throughput_MB_s_scanner8" in scanner_pairs_df and "throughput_MB_s_scanner64" in scanner_pairs_df:
                scanner_pairs_df["throughput_ratio_64_vs_8"] = (
                    scanner_pairs_df["throughput_MB_s_scanner64"] / scanner_pairs_df["throughput_MB_s_scanner8"]
                )

if scanner_pairs_df.empty:
    print("Tidak ditemukan pasangan Scanner8/Scanner64 valid dalam file mentah yang sama.")
else:
    print(f"Pasangan Scanner8/Scanner64 yang dapat dibandingkan dalam file yang sama: {len(scanner_pairs_df)}")
    display(scanner_pairs_df.sort_values(["source_file", "scanner_pair_key"]))

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
if not scanner_pairs_df.empty:
    plot_scanner = scanner_pairs_df.sort_values(["source_file", "scanner_pair_key"]).copy()
    plot_scanner["label"] = plot_scanner["source_file"].map(lambda x: Path(x).name) + " | " + plot_scanner["scanner_pair_key"].astype(str)
    fig, ax = plt.subplots(figsize=(14, max(5, len(plot_scanner) * 0.35)))
    ax.barh(plot_scanner["label"], plot_scanner["speedup_64_vs_8"])
    ax.axvline(1.0, linestyle="--", linewidth=1)
    ax.set_title("Scanner64 dibanding Scanner8 dalam file/run yang sama")
    ax.set_xlabel("Speedup = waktu Scanner8 / waktu Scanner64 (lebih besar = Scanner64 lebih cepat)")
    ax.set_ylabel("File | pasangan benchmark")
    ax.grid(True, axis="x", alpha=0.25)
    plt.tight_layout()
    plt.show()
else:
    print("Grafik scanner dilewati karena pasangan valid tidak tersedia.")

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
        "scanner_pairs_same_run.csv": scanner_pairs_df,
        "benchmark_before_after_exploratory.csv": before_after_df,
        "js_sizes_long.csv": js_sizes_df,
        "js_sizes_comparison.csv": js_sizes_wide,
        "js_parse_long.csv": js_parse_df,
        "js_parse_comparison.csv": js_parse_wide,
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
