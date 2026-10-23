// ============================================================================
//  js/benchmark/sizes.mjs — BENCHMARK JS: JSON.parse vs ze_json (wasm64)
//  pada SAPUAN UKURAN BERTAHAP (payload sintetis deterministik).
// ============================================================================
//  Pemakaian:
//      node benchmark/sizes.mjs             # jalankan + tabel ringkasan
//      node benchmark/sizes.mjs --save      # + tulis hasil ke out/results/
//
//  Motivasi: pada payload mungil (~71 B) ze_json KALAH dari JSON.parse —
//  itu rezim overhead-tetap (panggilan wasm + tulis payload ke region +
//  scan + tape) yang belum teramortisasi. Benchmark ini memetakan kurva
//  biaya lintas ukuran dan menemukan TITIK IMPAS terhadap V8.
//
//  ISOLASI METRIK (blok tak relevan dikeluarkan dari pengukuran):
//    - Payload dibangun SEKALI per ukuran sebelum pengukuran; bentuk dokumen
//      KONSTAN ({"id","name","score","active","items":[{id,tag,ok}...]}) —
//      hanya LEBAR array items yang menskalakan. Ukuran 64 B memakai items
//      kosong → baris itu murni karakterisasi overhead-tetap.
//    - Tiap iterasi mengolah SATU payload penuh, hasil dituang ke SINK
//      module-level (mengalahkan dead-code-elimination JIT), dicetak di akhir.
//    - Semua workload berbagi SATU proses + satu run (perbandingan
//      bersebelahan; jam lintas-proses mesin ini bergetar 15–45%).
//    - Pasangan JSON/ze diukur dengan minTime identik, urutan tetap.
//
//  Enam workload per ukuran (semantik setara antar sisi):
//    1) JSON.parse        — parse murni (materialisasi pohon + GC-nya).
//    2) ze scan           — openDocument: String→UTF-8 ke region + arena
//                           reset + scan SWAR + tape + Session (end-to-end
//                           pemakai JS; region dipakai ulang antar iterasi —
//                           itu desain library, bukan kecurangan).
//    3) JSON.parse + baca — parse + baca 2 field skalar (score, name).
//    4) ze baca           — openDocument + intAt("score") + stringAt("name")
//                           (2 path query = 2 panggilan wasm).
//    5) JSON.parse + iter — parse + jumlahkan items[*].id (loop penuh).
//    6) ze iter           — openDocument + find("items") + arraySize + N ×
//                           intAt("items[K].id") — pola akses acak lewat
//                           indeks lazy (Tahap 2 + 4); pembangunan string
//                           path per item dihitung (itu biaya nyata sisi JS).
//
//  Catatan jujur (wajib untuk interpretasi):
//    - JSON.parse membangun pohon JS BARU tiap iterasi (alokasi + GC);
//      ze_json memindai ke region tetap tanpa alokasi JS — perbedaan
//      desain, bukan bias pengukuran.
//    - ze unggul secara alami pada pola "baca SEBAGIAN field" (on-demand);
//      workload 5/6 adalah batas konsumsi-penuh — di sana per-field ABI
//      kalah dari akses properti V8 langsung. Keduanya dilaporkan apa adanya.
// ============================================================================

import Benchmark from "benchmark";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { ZeJson } from "../ze_json.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..", "..");
const OUT_DIR = join(REPO, "out", "results", "benchmark");
const SAVE = process.argv.includes("--save");

// SINK module-level — mencegah JIT membuang hasil kerja (anti-DCE).
let SINK_OBJ = null;
let SINK_DOC = null;
let SINK_NUM = 0;
let SINK_STR = "";

// ---------------------------------------------------------------------------
//  PAYLOAD SINTETIS — bentuk konstan, lebar items menskalakan ukuran.
// ---------------------------------------------------------------------------
// Batas atas sapuan: kontrak region tape binding JS (131.072 entry, lihat
// js/ze_json.mjs) — JSON realistis ~0,3-0,5 entry/byte, sehingga payload
// sintetis berbentuk ini aman sampai ~256 KiB. (Payload ~1 MiB ber-entri
// padat melebihi region tape — itu batas binding, bukan bug scanner.)
const TARGETS = [64, 256, 1024, 4096, 16384, 65536, 262144];

function makePayload(targetBytes) {
  const scalars = '{"id":7,"name":"ze_json","score":3.5,"active":true';
  // Ukuran mungil: items KOSONG (field tetap ada — kedua sisi mengiterasi
  // 0 kali secara bersih; baris ini murni karakterisasi overhead-tetap).
  const empty = scalars + ',"items":[]}';
  if (Buffer.byteLength(empty) >= targetBytes) {
    return empty;
  }
  let s = scalars + ',"items":[';
  for (let i = 0; ; ++i) {
    if (i > 0) s += ",";
    s += `{"id":${i},"tag":"item-${i}","ok":${i % 2 === 0}}`;
    if (Buffer.byteLength(s + "]}") >= targetBytes) {
      return s + "]}";
    }
  }
}

function fmtSize(n) {
  return n >= 1024 * 1024
    ? `${(n / 1024 / 1024).toFixed(2)} MB`
    : n >= 1024
      ? `${Math.round(n / 1024)} KiB`
      : `${n} B`;
}

function us(hz) {
  return hz > 0 ? (1e6 / hz) : NaN;
}

const SIZES = TARGETS.map((t) => {
  const text = makePayload(t);
  return { label: fmtSize(Buffer.byteLength(text)), bytes: Buffer.byteLength(text), text };
});

// ---------------------------------------------------------------------------
//  INSTANSI WRAPPER + warm-up (di luar pengukuran).
// ---------------------------------------------------------------------------
const ze = await ZeJson.load();
for (const b of SIZES) {
  for (let w = 0; w < 10; w++) {
    SINK_OBJ = JSON.parse(b.text);
    const d = ze.openDocument(b.text);
    d.intAt("score");
    d.find("items");
    d.arraySize();
    d.intAt("items[0].id");
  }
}
console.log(`ze_json wasm ✓ · node ${process.version} · ${SIZES.length} ukuran\n`);

// ---------------------------------------------------------------------------
//  SUITE — benchmark.js: 6 workload per ukuran, sekuensial (queued).
// ---------------------------------------------------------------------------
const MODES = [
  "JSON.parse",
  "ze scan",
  "JSON+baca",
  "ze baca",
  "JSON+iter",
  "ze iter",
];
const results = []; // { label, bytes, hz per mode }
const IS_RESULT = new Map();

const suite = new Benchmark.Suite(
  "JSON.parse vs ze_json — sapuan ukuran",
);

function addBench(si, mode, fn) {
  const name = `${SIZES[si].label} || ${mode}`;
  IS_RESULT.set(name, { si, mode });
  suite.add(name, {
    fn,
    onError: (e) => console.error("BENCH ERROR:", e.target.error),
  });
}

SIZES.forEach((b, si) => {
  results.push({ label: b.label, bytes: b.bytes, hz: {} });

  addBench(si, "JSON.parse", () => {
    SINK_OBJ = JSON.parse(b.text);
  });

  addBench(si, "ze scan", () => {
    SINK_DOC = ze.openDocument(b.text);
  });

  addBench(si, "JSON+baca", () => {
    const o = JSON.parse(b.text);
    SINK_NUM = o.score;
    SINK_STR = o.name;
  });

  addBench(si, "ze baca", () => {
    const d = ze.openDocument(b.text);
    SINK_NUM = d.intAt("score").value;
    SINK_STR = d.stringAt("name").value;
  });

  addBench(si, "JSON+iter", () => {
    const o = JSON.parse(b.text);
    let sum = 0;
    for (const it of o.items) sum += it.id;
    SINK_NUM = sum;
  });

  addBench(si, "ze iter", () => {
    const d = ze.openDocument(b.text);
    d.find("items");
    const n = d.arraySize();
    let sum = 0;
    for (let k = 0; k < n; ++k) {
      sum += d.intAt(`items[${k}].id`).value;
    }
    SINK_NUM = sum;
  });
});

suite.on("cycle", (event) => {
  const meta = IS_RESULT.get(event.target.name);
  if (!meta) return;
  results[meta.si].hz[meta.mode] = event.target.hz;
  const [sz, mode] = event.target.name.split(" || ");
  console.log(
    `  • ${sz.padEnd(10)} ${mode.padEnd(11)}` +
      `${us(event.target.hz).toFixed(2).padStart(10)} µs/op` +
      `  ±${event.target.stats.rme.toFixed(1)}%`,
  );
});

suite.on("complete", printSummary);

console.log("Menjalankan benchmark.js (6 workload × 7 ukuran)…\n");
suite.run({ async: true, queued: true });

// ---------------------------------------------------------------------------
//  RINGKASAN + analisis titik impas.
// ---------------------------------------------------------------------------
function printSummary() {
  console.log("═".repeat(86));
  console.log("RINGKASAN — µs/op (lebih kecil = lebih cepat)");
  console.log("═".repeat(86) + "\n");

  const head =
    "ukuran".padEnd(10) +
    "parse".padStart(9) +
    "ze scan".padStart(10) +
    "baca JSON".padStart(11) +
    "baca ze".padStart(10) +
    "iter JSON".padStart(11) +
    "iter ze".padStart(10);
  console.log(head);
  console.log("-".repeat(86));

  for (const r of results) {
    console.log(
      r.label.padEnd(10) +
        us(r.hz["JSON.parse"]).toFixed(2).padStart(9) +
        us(r.hz["ze scan"]).toFixed(2).padStart(10) +
        us(r.hz["JSON+baca"]).toFixed(2).padStart(11) +
        us(r.hz["ze baca"]).toFixed(2).padStart(10) +
        us(r.hz["JSON+iter"]).toFixed(2).padStart(11) +
        us(r.hz["ze iter"]).toFixed(2).padStart(10),
    );
  }

  console.log("-".repeat(86));
  console.log("MB/s (scan/parse murni):");
  for (const r of results) {
    const mb = r.bytes / 1e6;
    console.log(
      r.label.padEnd(10) +
        (mb / (us(r.hz["JSON.parse"]) / 1e6)).toFixed(0).padStart(9) +
        (mb / (us(r.hz["ze scan"]) / 1e6)).toFixed(0).padStart(10),
    );
  }

  // Titik impas pola "baca" (pemakaian praktis on-demand).
  const crossoverBaca = results.find(
    (r) => us(r.hz["ze baca"]) < us(r.hz["JSON+baca"]),
  );
  const crossoverScan = results.find(
    (r) => us(r.hz["ze scan"]) < us(r.hz["JSON.parse"]),
  );
  console.log("");
  if (crossoverScan) {
    console.log(
      `* Titik impas parse-vs-scan murni : ${crossoverScan.label} (ze mulai lebih cepat)`,
    );
  } else {
    console.log("* Parse murni: ze scan belum menyusul JSON.parse pada rentang ini.");
  }
  if (crossoverBaca) {
    console.log(
      `* Titik impas pola "baca 2 field"  : ${crossoverBaca.label} (ze mulai lebih cepat)`,
    );
  } else {
    console.log('* Pola "baca": ze belum menyusul pada rentang ini.');
  }
  console.log(
    `\n* SINK akhir (anti-DCE): ${SINK_NUM} · '${SINK_STR}' · doc=${SINK_DOC ? "hidup" : "-"}\n`,
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
  const base = `js_sizes_${rev}`;

  const final = {
    timestamp: new Date().toISOString(),
    node: process.version,
    platform: process.platform,
    rev,
    unit: "µs/op (turunan dari hz benchmark.js)",
    results: results.map((r) => ({
      label: r.label,
      bytes: r.bytes,
      us: Object.fromEntries(
        MODES.map((m) => [m, Number(us(r.hz[m]).toFixed(3))]),
      ),
    })),
  };
  writeFileSync(
    join(OUT_DIR, `${base}.json`),
    JSON.stringify(final, null, 2),
  );

  let md =
    `# Benchmark JS sapuan ukuran: JSON.parse vs ze_json — rev ${rev}\n\n` +
    `Node ${process.version}, ${process.platform}, ${new Date().toISOString()}` +
    ` (benchmark.js v2)\n\n` +
    `Bentuk dokumen konstan; hanya lebar array items menskalakan. µs/op — ` +
    `lebih kecil = lebih cepat.\n\n` +
    `| ukuran | parse | ze scan | baca JSON | baca ze | iter JSON | iter ze |\n` +
    `|---|---|---|---|---|---|---|\n`;
  for (const r of final.results) {
    md += `| ${r.label} | ${r.us["JSON.parse"]} | ${r.us["ze scan"]} | ` +
      `${r.us["JSON+baca"]} | ${r.us["ze baca"]} | ${r.us["JSON+iter"]} | ` +
      `${r.us["ze iter"]} |\n`;
  }
  writeFileSync(join(OUT_DIR, `${base}.md`), md);
  console.log(
    `✓ disimpan:\n  - out/results/benchmark/${base}.json\n  - out/results/benchmark/${base}.md`,
  );
}
