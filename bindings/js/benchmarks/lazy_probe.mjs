/**
 * lazy_probe.mjs — PRELIMINARY ad-hoc probe for the ND-4 lazy-access layer.
 *
 * NOT a measurement record: no schema, no metadata sidecar, no validator.
 * Validated ND-6 records own the thesis numbers (harness.mjs / CMake export
 * helpers). This probe exists ONLY to (a) confirm the lazy layer actually
 * skips work end-to-end (not just in unit tests) and (b) expose obvious
 * cost ceilings before ND-5 call-site integration. Single process, dev
 * device, sequential phases (no alternating), median of rounds — treat the
 * numbers as smoke-signal quality, never as evidence.
 *
 * Paths per corpus document:
 *   v8-parse              JSON.parse(bytes)                 (baseline)
 *   ze-parse              ZeParser.parse()                  (full materialize)
 *   ze-project-full       project(full shape)               (L2 everywhere)
 *   ze-project-selective  project(selective shape)          (L2 subset)
 *   ze-l1-ids             lazy id-scan / top-level touch    (L1 only)
 *
 * @cite 10.1002_spe.3313 on-demand JSON — selective consumption should pay
 *       only for visited fields; this probe is the cheap sanity check of
 *       that claim before the measured acceptance runs.
 */
import fs from 'node:fs';
import path from 'node:path';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'node:url';
import { ZeJson, ZeParser } from '../ze_json.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WORKSPACE = path.resolve(HERE, '../../../..');
const BENCH_ROOT = path.join(WORKSPACE, 'corpus', 'benchmarks');

const ROUNDS = 11; // timing rounds after warmup (median reported)
const WARMUP_REPS = 3;

const binding = ZeJson.load();
const close = (parser) => {
  parser.close();
  binding.resetHeap(); // bump heap rewind between iterations (batch contract)
};

const median = (xs) => {
  const s = [...xs].sort((a, b) => a - b);
  return s[s.length >> 1];
};

/** Same homogeneity rule as tests/lazy.mjs: object shapes only on plain
 *  objects — heterogeneous documents fall back to {$count:true}. */
const allPlainObjects = (arr) => arr.every((e) => e !== null
  && typeof e === 'object' && !Array.isArray(e));

function fullShape(v) {
  if (Array.isArray(v)) {
    return { $array: true };
  }
  if (v !== null && typeof v === 'object') {
    const shape = Object.create(null);
    for (const key of Object.keys(v)) {
      shape[key] = true;
    }
    return shape;
  }
  return true;
}

/** ND-5 consumer shape analog: root array -> ids of every item; root
 *  object -> scalars taken, arrays -> {$array:{id:true}} when homogeneous
 *  (else {$count:true}), nested objects -> {$count:true}. */
function selectiveShape(v) {
  if (Array.isArray(v)) {
    return allPlainObjects(v)
      && v.some((e) => Object.prototype.hasOwnProperty.call(e, 'id'))
      ? { $array: { id: true } }
      : { $count: true };
  }
  if (v !== null && typeof v === 'object') {
    const shape = Object.create(null);
    for (const key of Object.keys(v)) {
      const item = v[key];
      if (Array.isArray(item)) {
        shape[key] = allPlainObjects(item)
          && item.some((e) => Object.prototype.hasOwnProperty.call(e, 'id'))
          ? { $array: { id: true } }
          : { $count: true };
      } else if (item !== null && typeof item === 'object') {
        shape[key] = { $count: true };
      } else {
        shape[key] = true;
      }
    }
    return shape;
  }
  return true;
}

/** L1 phase: the closest analog of a SAQ read-only consumer — root arrays:
 *  one get('id') + one string decode per item; root objects: one get() +
 *  type() per top-level field (no decode). */
function l1Phase(parser, v8Value) {
  if (Array.isArray(v8Value)) {
    let ids = 0;
    for (const item of parser.lazy().items()) {
      if (item.type() !== 'object') {
        continue; // heterogeneous: keep walking, decode nothing else
      }
      const id = item.get('id');
      if (id !== undefined) {
        ids += id.asString().length;
      }
    }
    return ids;
  }
  if (v8Value !== null && typeof v8Value === 'object') {
    let touched = 0;
    for (const key of Object.keys(v8Value)) {
      const f = parser.get(key, parser.rootTs, 0);
      if (f !== undefined) {
        touched += parser.kind(f.ts);
      }
    }
    return touched;
  }
  return 0;
}

function runPhase(label, reps, fn) {
  for (let i = 0; i < WARMUP_REPS; i += 1) {
    fn(); // warmup (untimed)
  }
  const rounds = [];
  for (let r = 0; r < ROUNDS; r += 1) {
    const t0 = performance.now();
    for (let i = 0; i < reps; i += 1) {
      fn();
    }
    rounds.push((performance.now() - t0) / reps);
  }
  const perOpMs = median(rounds);
  return { label, perOpMs };
}

const fmt = (x) => x.toFixed(3).padStart(9);

for (const corpusDir of fs.readdirSync(BENCH_ROOT)) {
  const full = path.join(BENCH_ROOT, corpusDir);
  if (!fs.statSync(full).isDirectory()) {
    continue;
  }
  for (const file of fs.readdirSync(full)) {
    if (!file.endsWith('.json') || file === 'MANIFEST.json') {
      continue;
    }
    const bytes = fs.readFileSync(path.join(full, file));
    const text = bytes.toString('utf8');
    let v8Value;
    try {
      v8Value = JSON.parse(bytes);
    } catch {
      continue; // probe runs on accepted documents only
    }
    // Reps scaled so each phase costs roughly the same wall time per round.
    const reps = Math.max(1, Math.min(30, Math.floor(200_000 / bytes.length)));
    const rows = [runPhase('v8-parse', reps, () => JSON.parse(text))];
    const zePhases = [
      ['ze-parse', (p) => p.parse()],
      ['ze-project-full', (p) => p.project(fullShape(v8Value))],
      ['ze-project-selective', (p) => p.project(selectiveShape(v8Value))],
      ['ze-l1-ids', (p) => l1Phase(p, v8Value)],
    ];
    for (const [label, body] of zePhases) {
      rows.push(runPhase(label, reps, () => {
        const parser = ZeParser.open(binding, bytes);
        try {
          body(parser);
        } finally {
          close(parser);
        }
      }));
    }
    console.log(`[probe:${corpusDir}/${file}] ${bytes.length} bytes, reps/round=${reps}`);
    const base = rows[0].perOpMs;
    for (const row of rows) {
      const mbps = bytes.length / 1e6 / (row.perOpMs / 1000);
      const ratio = (base / row.perOpMs).toFixed(2).padStart(6);
      console.log(`  ${row.label.padEnd(22)} ${fmt(row.perOpMs)} ms/op  `
        + `${mbps.toFixed(1).padStart(8)} MB/s  speedup-vs-v8 ${ratio}x`);
    }
    if (global.gc) {
      global.gc();
    }
  }
}
console.log('[probe] PRELIMINARY ad-hoc numbers — not measurement records; '
  + 'validated acceptance runs are ND-6 scope.');
