/**
 * differential.mjs — corpus-wide differential gate for the wasm64 binding
 * (TASKS P3-5): for every corpus document and every json_checker case, the
 * wasm scanner8, the wasm reference_scan (same module, C++ reference) and the
 * plain JS reference must agree on outcome, count and indices.
 *
 * @cite 10.48550_arXiv.1902.08318 simdjson staging contract
 * @cite 10.14778_3115404.3115416 Mison structural indexing
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { ZeJson, ZeDocument } from '../ze_json.mjs';
import { referenceScan } from './reference.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WORKSPACE = path.resolve(HERE, '../../../..'); // skripsi/v5
const BENCH_ROOT = path.join(WORKSPACE, 'corpus', 'benchmarks');
const CHECKER_DIR = path.join(WORKSPACE, 'corpus', 'test', 'json_checker');

let failures = 0;
let checked = 0;

function compareDoc(label, bytes) {
  checked += 1;
  const doc = new ZeDocument(binding, bytes);
  // Copy indices immediately after each scan: both scans share one dst buffer.
  const fast = doc.scan8();
  const fastIdx = fast.ok ? doc.indices(fast) : [];
  const cppRef = doc.referenceScan();
  const cppIdx = cppRef.ok ? doc.indices(cppRef) : [];
  const jsRef = referenceScan(bytes);

  const sameShape = (a, aIdx, b, bIdx) =>
    a.ok === b.ok
    && (a.ok ? a.count === b.count && JSON.stringify(aIdx) === JSON.stringify(bIdx)
             : a.error === b.error);

  if (!sameShape(fast, fastIdx, cppRef, cppIdx)) {
    failures += 1;
    console.error(`FAIL scanner8 vs wasm-reference: ${label} — `
      + `fast=${JSON.stringify({ ...fast, indices: fastIdx })} `
      + `cppRef=${JSON.stringify({ ...cppRef, indices: cppIdx })}`);
  }
  if (!sameShape(fast, fastIdx, jsRef, jsRef.indices)) {
    failures += 1;
    console.error(`FAIL scanner8 vs js-reference: ${label} — `
      + `fast=${JSON.stringify({ ...fast, indices: fastIdx })} `
      + `jsRef=${JSON.stringify(jsRef)}`);
  }
}

const binding = ZeJson.load();

// --- benchmark corpora (P0-8 manifests) -------------------------------------
for (const corpusDir of fs.readdirSync(BENCH_ROOT)) {
  const full = path.join(BENCH_ROOT, corpusDir);
  if (!fs.statSync(full).isDirectory()) {
    continue;
  }
  for (const file of fs.readdirSync(full)) {
    if (!file.endsWith('.json') || file === 'MANIFEST.json') {
      continue;
    }
    compareDoc(`${corpusDir}/${file}`, fs.readFileSync(path.join(full, file)));
  }
}

// --- json_checker: pass + fail must merely AGREE (grammar errors are the
// parser's job; the scanner contract only rejects unterminated strings).
for (const file of fs.readdirSync(CHECKER_DIR)) {
  if (!file.endsWith('.json')) {
    continue;
  }
  compareDoc(`json_checker/${file}`, fs.readFileSync(path.join(CHECKER_DIR, file)));
}

console.log(`[differential] checked ${checked} document(s): `
  + `${failures === 0 ? 'all three implementations agree' : `${failures} failure(s)`}`);
process.exit(failures === 0 ? 0 : 1);
