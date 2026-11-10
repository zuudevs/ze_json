/**
 * differential.mjs — corpus-wide differential gate for the wasm64 binding
 * (TASKS P3-5, extended in R2 and P7-5): for every corpus document and
 * every json_checker case,
 *   1. EVERY available wasm scanner mode (structural, scanner8, scanner64,
 *      scanner128, reference), the wasm reference_scan (same module, C++
 *      reference) and the plain JS reference must agree on outcome, count
 *      and indices;
 *   2. the on-demand parser through the v3 surface must agree with the
 *      V8 `JSON.parse` baseline on the VERDICT (both sides accept or both
 *      reject) and — for accepted documents without surrogate-escape
 *      pass-through divergence — on the materialized value tree
 *      (canonical JSON.stringify on both sides; -0 sign parity is pinned
 *      separately by main.mjs with Object.is).
 *
 * @cite 10.48550_arXiv.1902.08318 simdjson staging contract
 * @cite 10.14778_3115404.3115416 Mison structural indexing
 * @cite 10.1002_spe.3313 on-demand JSON — verdict/tree parity of the
 *       replayed forward iterator against the DOM baseline.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { ZeJson, ZeDocument, ZeParser, ZeParserError, SCANNER_MODES } from '../ze_json.mjs';
import { referenceScan } from './reference.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WORKSPACE = path.resolve(HERE, '../../../..'); // skripsi/v5
const BENCH_ROOT = path.join(WORKSPACE, 'corpus', 'benchmarks');
const CHECKER_DIR = path.join(WORKSPACE, 'corpus', 'test', 'json_checker');

/** Any surrogate-range \uXXXX escape: wrapper strings carry such tokens
 *  verbatim (case-preserving), while V8 canonicalizes escapes — the
 *  canonical stringify comparison is skipped for these documents. */
const SURROGATE_ESCAPE = /\\u[dD][89abcdefABCDEF][0-9a-fA-F]{2}/;

let failures = 0;
let checked = 0;

function compareDoc(label, bytes) {
  checked += 1;
  const doc = new ZeDocument(binding, bytes);
  const cppRef = doc.referenceScan();
  const cppIdx = cppRef.ok ? doc.indices(cppRef) : [];
  const jsRef = referenceScan(bytes);

  const sameShape = (a, aIdx, b, bIdx) =>
    a.ok === b.ok
    && (a.ok ? a.count === b.count && JSON.stringify(aIdx) === JSON.stringify(bIdx)
             : a.error === b.error);

  for (const mode of Object.keys(SCANNER_MODES)) {
    if (!binding.hasScanner(mode)) {
      continue; // variant not compiled into this artifact
    }
    // Copy indices immediately after each scan: all scans share one dst buffer.
    const fast = doc.scan(mode);
    const fastIdx = fast.ok ? doc.indices(fast) : [];
    if (!sameShape(fast, fastIdx, cppRef, cppIdx)) {
      failures += 1;
      console.error(`FAIL ${mode} vs wasm-reference: ${label} — `
        + `fast=${JSON.stringify({ ...fast, indices: fastIdx })} `
        + `cppRef=${JSON.stringify({ ...cppRef, indices: cppIdx })}`);
    }
    if (!sameShape(fast, fastIdx, jsRef, jsRef.indices)) {
      failures += 1;
      console.error(`FAIL ${mode} vs js-reference: ${label} — `
        + `fast=${JSON.stringify({ ...fast, indices: fastIdx })} `
        + `jsRef=${JSON.stringify(jsRef)}`);
    }
  }

  compareParser(label, bytes);
  // The parser journey carves bump-heap bytes per document (no individual
  // free); rewinding between documents keeps the wasm64 measurement memory
  // classes inside the module's initial 64 MiB.
  binding.resetHeap();
}

/**
 * Parser verdict/tree parity (P7-5): the on-demand walk replayed through
 * the v3 binding must accept/reject exactly what V8 `JSON.parse` does, and
 * for accepted documents materialize the same value tree (canonical
 * stringify on both sides; surrogate-escape pass-through documents are
 * verdict-compare only — see the SURROGATE_ESCAPE note).
 */
function compareParser(label, bytes) {
  let v8Ok = true;
  let v8Value;
  try {
    v8Value = JSON.parse(bytes);
  } catch {
    v8Ok = false;
  }
  let zeOk = true;
  let zeValue;
  try {
    const parser = ZeParser.open(binding, bytes);
    try {
      zeValue = parser.parse();
    } finally {
      parser.close();
    }
  } catch (err) {
    if (!(err instanceof ZeParserError)) {
      throw err; // driver bug — not a parser verdict
    }
    zeOk = false;
  }

  if (v8Ok !== zeOk) {
    failures += 1;
    console.error(`FAIL parser verdict: ${label} — v8 accepted=${v8Ok}, `
      + `ze accepted=${zeOk}`);
    return;
  }
  if (!v8Ok || SURROGATE_ESCAPE.test(bytes.toString('latin1'))) {
    return; // verdict parity only (reject pair / documented divergence)
  }
  const zeTree = JSON.stringify(zeValue);
  const v8Tree = JSON.stringify(v8Value);
  if (zeTree !== v8Tree) {
    failures += 1;
    const preview = (s) => (s.length > 160 ? `${s.slice(0, 157)}...` : s);
    console.error(`FAIL parser tree: ${label} — ze=${preview(zeTree)} `
      + `v8=${preview(v8Tree)}`);
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

console.log(`[differential] checked ${checked} document(s) x `
  + `${Object.keys(SCANNER_MODES).length} scanner mode(s) + parser/tree `
  + `parity vs JSON.parse: ${failures === 0 ? 'all implementations agree'
    : `${failures} failure(s)`}`);
process.exit(failures === 0 ? 0 : 1);
