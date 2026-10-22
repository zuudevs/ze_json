// ============================================================================
//  js/benchmark/parse.mjs — BENCHMARK JS: JSON.parse (V8) vs ze_json (wasm64)
// ============================================================================
//  Menggunakan paket benchmark.js (satu keluarga dengan google-benchmark).
//
//  Pemakaian:
//      node benchmark/parse.mjs             # jalankan + tabel ringkasan
//      node benchmark/parse.mjs --save      # + tulis hasil ke out/results/
//
//  Semantik workload yang FAIR — setiap iterasi memproses SATU payload:
//    1) JSON.parse (penuh) — V8 materialisasi seluruh pohon JS object.
//    2) ze_json scan       — writeText (UTF-8) + create_session (SWAR/tape).
//    3) ze_json scan+find  — scan + find_field (akses 1 field, on-demand).
//
//  Catatan jujur (wajib untuk interpretasi):
//    - ze_json REUSE region memori antar-iterasi (arena di-reset pada tiap
//      openDocument) — itu desain library; JSON.parse membangun pohon baru
//      setiap hit.
//    - Biaya konversi String→UTF-8 (TextEncoder) masuk angka ze_json —
//      itulah end-to-end bagi pemakai JS (payload datang sebagai String).
//    - Dataset ber-root ARRAY (mis. github_events) tidak didukung ABI
//      ze_json (1 Session = 1 dokumen ber-root OBJECT) dan di-skip.
// ============================================================================

import Benchmark from "benchmark";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { ZeJson } from "../ze_json.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..", "..");
const DATASET = join(REPO, "dataset");
const OUT_DIR = join(REPO, "out", "results", "benchmark");
const SAVE = process.argv.includes("--save");

function fmtSize(n) {
  return n >= 1024 * 1024
    ? `${(n / 1024 / 1024).toFixed(2)} MB`
    : `${Math.max(1, Math.round(n / 1024))} KB`;
}

function hzLabel(hz) {
  return hz >= 1e6
    ? `${(hz / 1e6).toFixed(2)} Mops/s`
    : hz >= 1e3
      ? `${(hz / 1e3).toFixed(2)} kops/s`
      : `${hz.toFixed(0)} ops/s`;
}

// ---------------------------------------------------------------------------
//  DAFTAR DATASET
// ---------------------------------------------------------------------------
const BENCHES = [];
const TXT_TINY = '{"username":"zuudevs","age":21,"active":true,"status":null' +
  ',"colors":["white","black"],"position":{"x":24,"y":48}}';
BENCHES.push({ label: "tiny (163 B)", text: TXT_TINY, key: "colors" });

//  Korpus simdjson: dataset kecil-menengah (PAYLOAD_CAP wrapper = 1 MiB).
//  * citm_catalog (1,7 MB) & jeopardy (53 MB) di-skip; file hilang → skip.
for (const [file, key] of [
  ["mesh.json", "vertices"],           // ASCII murni (tanpa byte > 0x7F)
  ["twitter.json", "search_metadata"], // ada UTF-8 nyata; find terjauh
]) {
  let raw;
  try {
    raw = readFileSync(join(DATASET, file), "utf8");
  } catch {
    console.warn(`(skip) dataset/${file} tidak ada`);
    continue;
  }
  const root = JSON.parse(raw); // SEKALI di startup — di luar pengukuran
  if (Array.isArray(root)) {
    console.warn(`(skip) ${file}: root ARRAY tidak didukung ABI ze_json`);
    continue;
  }
  if (!(key in root)) {
    console.warn(`(skip) ${file}: key '${key}' tak ada di root`);
    continue;
  }
  BENCHES.push({
    label: `${file} (${fmtSize(Buffer.byteLength(raw))})`,
    text: raw,
    key,
  });
}

// ---------------------------------------------------------------------------
//  INSTANSI WRAPPER + warm-up (di luar pengukuran)
// ---------------------------------------------------------------------------
const ze = await ZeJson.load();
for (const b of BENCHES) {
  for (let w = 0; w < 10; w++) {
    JSON.parse(b.text);
    const d = ze.openDocument(b.text); // scan + tape — isi ulang SEKALI
    d.find(b.key);                     // tunjuk field (kursor values)
  }
}
console.log(`ze_json wasm ✓ · node ${process.version} · ${BENCHES.length} dataset\n`);

// ---------------------------------------------------------------------------
//  SUITE — benchmark.js: 3 workload per dataset, sekuensial
// ---------------------------------------------------------------------------
const IS_RESULT = new Map(); // "nama benchmark" → { dataset : @BBBB refs }
const results = [];          // { dataset, jsonParse, zeScan, zeScanFind }

function addBench(dsIndex, mode, fn) {
  const ds = BENCHES[dsIndex];
  const name = `${ds.label} || ${mode}`;
  IS_RESULT.set(name, { dataset: dsIndex, mode });
  suite.add(name, { fn, onError: handle });
}

const suite = new Benchmark.Suite("JSON.parse vs ze_json (wasm64)");

const handle = (e) => console.error("BENCH ERROR:", e.target.error);

suite
  .on("cycle", (event) => {
    const meta = IS_RESULT.get(event.target.name);
    if (!meta) return;
    const hz = event.target.hz;
    const rec = results[meta.dataset];
    if (meta.mode === "JSON.parse (penuh)") rec.jsonParse = hz;
    if (meta.mode === "ze scan") rec.zeScan = hz;
    if (meta.mode === "ze scan+find") rec.zeScanFind = hz;
    const bullet = event.target.name.split(" || ")[1];
    console.log(
      `  • ${bullet.padEnd(22)} ${hzLabel(hz).padEnd(12)}` +
        ` ±${event.target.stats.rme.toFixed(1)}%`,
    );
  })
  .on("complete", () => {
    console.log("");
  });

BENCHES.forEach((b, i) => {
  results.push({ dataset: b.label, jsonParse: 0, zeScan: 0, zeScanFind: 0 });

  addBench(i, "JSON.parse (penuh)", () => JSON.parse(b.text));
  addBench(i, "ze scan", () => ze.openDocument(b.text));
  addBench(i, "ze scan+find", () => ze.openDocument(b.text).find(b.key));
});

// ---------------------------------------------------------------------------
//  RUNNING + summary
// ---------------------------------------------------------------------------
console.log("Menjalankan benchmark.js (minTime default 5s per op)…\n");

suite.run({ async: true, queued: true }); // sekuensial per benchmark

// Ditangani setelah complete — pakai satu listener tambahan
suite.on("complete", printSummary);

function printSummary() {
  console.log("═".repeat(70));
  console.log("RINGKASAN — ops/sec (V8 JSON.parse vs ze_json wasm64)");
  console.log("═".repeat(70) + "\n");

  const header =
    "dataset".padEnd(24) +
    "JSON.parse".padEnd(16) +
    "ze scan".padEnd(16) +
    "ze scan+find".padEnd(16);
  console.log(header);
  console.log("-".repeat(70));

  for (const r of results) {
    console.log(
      r.dataset.padEnd(24) +
        hzLabel(r.jsonParse).padEnd(16) +
        hzLabel(r.zeScan).padEnd(16) +
        hzLabel(r.zeScanFind).padEnd(16),
    );
  }
  console.log(
    "\n* ze_json lebih CEPAT bila angkanya LEBIH BESAR dari JSON.parse.\n",
  );

  if (SAVE) saveAll();
}

function saveAll() {
  let rev = "";
  try {
    rev = execFileSync("git", ["rev-parse", "--short", "HEAD"], {
      cwd: REPO,
      encoding: "utf8",
    }).trim();
  } catch {
    rev = "uncommitted";
  }
  mkdirSync(OUT_DIR, { recursive: true });
  const base = `js_parse_${rev}`;
  const final = {
    timestamp: new Date().toISOString(),
    node: process.version,
    platform: process.platform,
    rev,
    results,
  };
  writeFileSync(join(OUT_DIR, `${base}.json`), JSON.stringify(final, null, 2));

  // ringkasan md — sama-pola dengan benchmark_metrics.js (summary_<rev>.md)
  let md =
    `# Benchmark JS: JSON.parse (V8) vs ze_json (wasm64) — rev ${rev}\n\n` +
    `Node ${process.version}, ${process.platform}, ${new Date().toISOString()}` +
    ` (benchmark.js v2, minTime 5s)\n\n` +
    `| dataset | JSON.parse | ze scan | ze scan+find | ze/JSON.parse |\n` +
    `|---|---|---|---|---|\n`;
  for (const r of results) {
    const ratio = r.jsonParse > 0 ? r.zeScanFind / r.jsonParse : 0;
    md += `| ${r.dataset} | ${hzLabel(r.jsonParse)} | ${hzLabel(r.zeScan)} | ` +
      `${hzLabel(r.zeScanFind)} | ×${ratio.toFixed(2)} |\n`;
  }
  md +=
    `\n**Cara membaca:** ratio ×N = ze_json (scan+find) N kali lebih CEPAT ` +
    `daripada JSON.parse (N lebih besar = lebih baik). Ambil sebaik-baiknya ` +
    `dengan mempertimbangkan catatan jujur di header berkas.\n`;
  writeFileSync(join(OUT_DIR, `${base}.md`), md);
  console.log(`✓ disimpan:\n  - out/results/benchmark/${base}.json\n  - out/results/benchmark/${base}.md`);
}
