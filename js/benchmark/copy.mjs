// ============================================================================
//  js/benchmark/copy.mjs — BENCHMARK JS: strategi salin payload ke wasm.
// ============================================================================
//  Pertanyaan: writeText saat ini memakai TextEncoder.encode() (mengalokasi
//  Uint8Array sementara) lalu u8.set() ke memori wasm (DUA salinan + 1
//  alokasi per dokumen). Apakah TextEncoder.encodeInto() — menulis langsung
//  ke view memori wasm (jalur cepat khusus V8 untuk tujuan wasm memory) —
//  lebih cepat?
//
//  Metodologi: interleaved SATU proses (pola wajib mesin ini — jam
//  lintas-proses bergetar 15–45%): kedua strategi berjalan bersebelahan
//  pada teks yang sama, memori wasm yang sama, dengan BIAYA KONTRAK PENUH
//  (pembuatan view per panggilan + byte pengaman nol + cek muat).
//
//  Pemakaian:
//      node benchmark/copy.mjs             # jalankan + tabel ringkasan
//      node benchmark/copy.mjs --save      # + artefak ke out/results/
// ============================================================================

import Benchmark from "benchmark";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { ZeJson, PAYLOAD_BASE, PAYLOAD_CAP } from "../ze_json.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..", "..");
const OUT_DIR = join(REPO, "out", "results", "benchmark");
const SAVE = process.argv.includes("--save");

const encoder = new TextEncoder();
const ze = await ZeJson.load();

// SINK module-level — anti dead-code-elimination JIT.
let SINK_N = 0;

// ---------------------------------------------------------------------------
//  Teks uji — deterministik; skala key (path query) sampai payload penuh.
// ---------------------------------------------------------------------------
function makeAscii(target) {
  let s = '{"items":[';
  if (target < 64) return '{"id":7,"name":"ze","score":3.5}';
  for (let i = 0; ; ++i) {
    if (i > 0) s += ",";
    s += `{"id":${i},"tag":"item-${i}","ok":${i % 2 === 0}}`;
    if (Buffer.byteLength(s + "]}") >= target) return s + "]}";
  }
}

function makeUtf8(target) {
  // ~50% byte multibyte — regime encoding non-ASCII.
  let s = '{"labels":[';
  for (let i = 0; ; ++i) {
    if (i > 0) s += ",";
    s += `{"n":${i},"t":"éàüöñ 中文 テスト ${i}"}`;
    if (Buffer.byteLength(s + "]}") >= target) return s + "]}";
  }
}

const TEXTS = [
  { label: "11 B (skala key)", text: "position.x" },
  { label: "63 B", text: makeAscii(64) },
  { label: "4 KiB", text: makeAscii(4096) },
  { label: "64 KiB", text: makeAscii(65536) },
  { label: "256 KiB", text: makeAscii(262144) },
  { label: "64 KiB UTF-8", text: makeUtf8(65536) },
];

// ---------------------------------------------------------------------------
//  Dua strategi — replika persis jalur binding masing-masing.
// ---------------------------------------------------------------------------
const BASE = Number(PAYLOAD_BASE);
const CAP = Number(PAYLOAD_CAP);

// (A) REPLIKA JALUR SAAT INI — encoder.encode() + u8.set():
//     alokasi buffer sementara + salin kecil → besar.
function copyEncodeSet(text) {
  const tmp = encoder.encode(text); // alokasi + salinan #1
  const u8 = new Uint8Array(ze.buffer()); // view seluruh memori (pola u8())
  u8.set(tmp, BASE); // salinan #2 → wasm
  u8[BASE + tmp.length] = 0; // byte pengaman
  SINK_N += tmp.length;
  return tmp.length;
}

// (B) USULAN — encodeInto() langsung ke view region wasm:
//     tanpa alokasi sementara, satu salinan, jalur cepat V8 utk wasm.
function copyEncodeInto(text) {
  const view = new Uint8Array(ze.buffer(), BASE, CAP); // view region
  const { read, written } = encoder.encodeInto(text, view); // salinan satu-satunya
  if (read !== text.length || written + 1 > CAP) {
    throw new Error("muat gagal"); // kontrak cek — sama seperti binding
  }
  view[written] = 0; // byte pengaman
  SINK_N += written;
  return written;
}

// Warm-up di luar pengukuran.
for (const b of TEXTS) {
  for (let w = 0; w < 20; w++) {
    copyEncodeSet(b.text);
    copyEncodeInto(b.text);
  }
}

// ---------------------------------------------------------------------------
//  SUITE
// ---------------------------------------------------------------------------
const results = [];
const IS_RESULT = new Map();
const suite = new Benchmark.Suite("Strategi salin payload → wasm");

function addBench(ti, mode, fn) {
  const name = `${TEXTS[ti].label} || ${mode}`;
  IS_RESULT.set(name, { ti, mode });
  suite.add(name, { fn: () => fn(TEXTS[ti].text), onError: (e) => console.error("BENCH ERROR:", e.target.error) });
}

TEXTS.forEach((b, ti) => {
  results.push({ label: b.label, bytes: Buffer.byteLength(b.text), encodeSet: 0, encodeInto: 0 });
  addBench(ti, "encode+set (saat ini)", copyEncodeSet);
  addBench(ti, "encodeInto (usulan)", copyEncodeInto);
});

function us(hz) { return hz > 0 ? 1e6 / hz : NaN; }

suite.on("cycle", (event) => {
  const meta = IS_RESULT.get(event.target.name);
  if (!meta) return;
  results[meta.ti][meta.mode.startsWith("encode+") ? "encodeSet" : "encodeInto"] = event.target.hz;
  const [sz, mode] = event.target.name.split(" || ");
  console.log(`  • ${sz.padEnd(18)} ${mode.padEnd(22)} ${us(event.target.hz).toFixed(2).padStart(9)} µs/op  ±${event.target.stats.rme.toFixed(1)}%`);
});

suite.on("complete", () => {
  console.log("═".repeat(74));
  console.log("RINGKASAN — µs/op (kecil = cepat) · rasio = encodeInto vs encode+set");
  console.log("═".repeat(74) + "\n");
  console.log("teks".padEnd(18) + "byte".padStart(8) + "encode+set".padStart(13) + "encodeInto".padStart(13) + "rasio".padStart(9) + "MB/s into".padStart(12));
  console.log("-".repeat(74));
  for (const r of results) {
    const a = us(r.encodeSet);
    const b = us(r.encodeInto);
    const mbps = (r.bytes / 1e6) / (b / 1e6);
    console.log(
      r.label.padEnd(18) +
        String(r.bytes).padStart(8) +
        a.toFixed(2).padStart(13) +
        b.toFixed(2).padStart(13) +
        (b / a).toFixed(2).padStart(9) +
        mbps.toFixed(0).padStart(12),
    );
  }
  console.log(`\n* SINK anti-DCE: ${SINK_N}\n`);
  if (SAVE) saveAll();
});

function saveAll() {
  let rev = "";
  try {
    rev = execFileSync("git", ["rev-parse", "--short", "HEAD"], { cwd: REPO, encoding: "utf8" }).trim();
  } catch { rev = "uncommitted"; }
  mkdirSync(OUT_DIR, { recursive: true });
  const base = `js_copy_${rev}`;
  const final = {
    timestamp: new Date().toISOString(),
    node: process.version,
    platform: process.platform,
    rev,
    note: "interleaved satu proses; kedua strategi memuat biaya view + byte pengaman + cek kontrak",
    results: results.map((r) => ({
      label: r.label, bytes: r.bytes,
      us_encode_set: Number(us(r.encodeSet).toFixed(3)),
      us_encode_into: Number(us(r.encodeInto).toFixed(3)),
      ratio: Number((us(r.encodeInto) / us(r.encodeSet)).toFixed(3)),
    })),
  };
  writeFileSync(join(OUT_DIR, `${base}.json`), JSON.stringify(final, null, 2));
  console.log(`✓ disimpan: out/results/benchmark/${base}.json`);
}

console.log(`ze_json wasm ✓ · node ${process.version} · ${TEXTS.length} teks × 2 strategi\n`);
suite.run({ async: true, queued: true });
