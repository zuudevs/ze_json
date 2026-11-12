/**
 * lazy.mjs — L1/L2 lazy-access layer tests (ND-4).
 *
 * Design contract: workspace docs/architecture/lazy-access-api.md §10
 * (T1–T6); decisions D1 (get first-wins / project last-wins), D2 (lifetime
 * fail-loud), D3 (skip without a new ABI). T7 — existing suites stay green —
 * is `npm run smoke && npm run differential` (run in CI alongside this).
 *
 * @cite 10.1002_spe.3313 on-demand JSON — selective consumption without
 *       materializing unvisited subtrees is the phase this layer isolates.
 * @cite 10.14778_3115404.3115416 Mison — the structural-index walk that
 *       backs cursor re-entry and skip.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { ZeJson, ZeParser, ZeParserError, ZeLazyValue, NODE_KINDS } from '../ze_json.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WORKSPACE = path.resolve(HERE, '../../../..');
const BENCH_ROOT = path.join(WORKSPACE, 'corpus', 'benchmarks');
const CHECKER_DIR = path.join(WORKSPACE, 'corpus', 'test', 'json_checker');

let failures = 0;
let checks = 0;

function check(label, condition, detail = '') {
  checks += 1;
  if (condition) {
    console.log(`  ok   ${label}`);
  } else {
    failures += 1;
    console.error(`  FAIL ${label}${detail ? ` — ${detail}` : ''}`);
  }
}

const binding = ZeJson.load();
const bytesOf = (text) => Buffer.from(text, 'utf8');

/** Any surrogate-range \uXXXX escape: the P7-3 wrapper carries such tokens
 *  through verbatim while V8 canonicalizes them — canonical stringify
 *  comparison is skipped for these documents (same policy as the
 *  tests/differential.mjs SURROGATE_ESCAPE gate). */
const SURROGATE_ESCAPE = /\\u[dD][89abcdefABCDEF][0-9a-fA-F]{2}/;
let t1Escape = 0;

/** Strict homogeneity guard: only take the {$array:{id}} projection when
 *  EVERY element is a plain object (never a primitive/array/null) — the
 *  object shape on a non-object element fails loud, so heterogeneous
 *  checker documents must fall back to {$count:true}. */
const allPlainObjects = (arr) => arr.every((e) => e !== null
  && typeof e === 'object' && !Array.isArray(e));

/**
 * Shape that materializes everything (parity shape): arrays -> every
 * element taken, objects -> every listed key taken, scalars -> true.
 * Built FROM the V8 reference tree so the projection walk is checked
 * against an independent description of the same document.
 */
function fullShape(v) {
  if (Array.isArray(v)) {
    return { $array: true };
  }
  if (v !== null && typeof v === 'object') {
    // Null-proto shape builder: a plain `shape[key] = ...` would route a
    // JSON `"__proto__"` own key through the Object.prototype accessor
    // setter (silently dropped) — null-proto keeps it an own key.
    const shape = Object.create(null);
    for (const key of Object.keys(v)) {
      shape[key] = true;
    }
    return shape;
  }
  return true;
}

/** Selective shape: root arrays -> {$array: {id: true}} when items are
 *  objects carrying an id, else {$count: true}; root objects/containers ->
 *  {$count: true}; scalars -> true. Returns {shape, expected} where
 *  `expected` is the same subset picked from the V8 tree. */
function selectiveShape(v) {
  const expected = Object.create(null);
  const shape = Object.create(null);
  if (Array.isArray(v)) {
    if (allPlainObjects(v)
        && v.some((e) => Object.prototype.hasOwnProperty.call(e, 'id'))) {
      shape.$array = { id: true };
      return {
        shape,
        expected: v.map((e) => (Object.prototype.hasOwnProperty.call(e, 'id')
          ? { id: e.id }
          : {})),
      };
    }
    return { shape: { $count: true }, expected: v.length };
  }
  if (v !== null && typeof v === 'object') {
    for (const key of Object.keys(v)) {
      const item = v[key];
      if (Array.isArray(item)) {
        if (allPlainObjects(item)
            && item.some((e) => Object.prototype.hasOwnProperty.call(e, 'id'))) {
          shape[key] = { $array: { id: true } };
          expected[key] = item.map((e) => (Object.prototype.hasOwnProperty.call(e, 'id')
            ? { id: e.id }
            : {}));
        } else {
          shape[key] = { $count: true };
          expected[key] = item.length;
        }
      } else if (item !== null && typeof item === 'object') {
        shape[key] = { $count: true };
        expected[key] = Object.keys(item).length;
      } else {
        shape[key] = true;
        expected[key] = item;
      }
    }
    return { shape, expected };
  }
  return { shape: true, expected: v };
}

// === T1: corpus-wide projection parity =====================================
console.log('[lazy] T1 project(full-shape) + selective == JSON.parse over corpora');
let t1Full = 0;
let t1Sel = 0;
let t1Rejected = 0;

function t1Doc(label, bytes) {
  let v8Value;
  let v8Ok = true;
  try {
    v8Value = JSON.parse(bytes);
  } catch {
    v8Ok = false;
  }
  let parser;
  try {
    parser = ZeParser.open(binding, bytes);
  } catch (err) {
    if (v8Ok || !(err instanceof ZeParserError)) {
      failures += 1;
      console.error(`  FAIL T1 open verdict: ${label} — ${err}`);
    } else {
      t1Rejected += 1;
    }
    return;
  }
  try {
    if (!v8Ok) {
      // Verdict parity: the lazy layer must fail loud on a rejected doc.
      // project(true) == full materialization (same #materialize path
      // parse() uses), so the P7-5 verdict contract applies unchanged.
      try {
        parser.project(true);
        failures += 1;
        console.error(`  FAIL T1 reject parity: ${label} — project accepted`);
      } catch (err) {
        if (!(err instanceof ZeParserError)) {
          throw err;
        }
        t1Rejected += 1;
      }
      return;
    }
    if (SURROGATE_ESCAPE.test(bytes.toString('latin1'))) {
      // Pass-through divergence documents: run BOTH projections for
      // no-crash coverage but compare nothing (same policy as the
      // differential gate).
      parser.project(fullShape(v8Value));
      parser.project(selectiveShape(v8Value).shape);
      t1Escape += 1;
      return;
    }
    // (a) full-shape projection must equal JSON.parse exactly.
    const full = parser.project(fullShape(v8Value));
    if (JSON.stringify(full) !== JSON.stringify(v8Value)) {
      failures += 1;
      const preview = (s) => (s.length > 140 ? `${s.slice(0, 137)}...` : s);
      console.error(`  FAIL T1 full parity: ${label} — ze=${preview(JSON.stringify(full))} `
        + `v8=${preview(JSON.stringify(v8Value))}`);
    } else {
      t1Full += 1;
    }
    // (b) selective projection must equal the same subset picked from V8.
    const { shape, expected } = selectiveShape(v8Value);
    const sel = parser.project(shape);
    if (JSON.stringify(sel) !== JSON.stringify(expected)) {
      failures += 1;
      const preview = (s) => (s.length > 140 ? `${s.slice(0, 137)}...` : s);
      console.error(`  FAIL T1 selective: ${label} — ze=${preview(JSON.stringify(sel))} `
        + `expected=${preview(JSON.stringify(expected))}`);
    } else {
      t1Sel += 1;
    }
  } finally {
    parser.close();
    binding.resetHeap();
  }
}

for (const corpusDir of fs.readdirSync(BENCH_ROOT)) {
  const full = path.join(BENCH_ROOT, corpusDir);
  if (!fs.statSync(full).isDirectory()) {
    continue;
  }
  for (const file of fs.readdirSync(full)) {
    if (!file.endsWith('.json') || file === 'MANIFEST.json') {
      continue;
    }
    t1Doc(`${corpusDir}/${file}`, fs.readFileSync(path.join(full, file)));
  }
}
for (const file of fs.readdirSync(CHECKER_DIR)) {
  if (file.endsWith('.json')) {
    t1Doc(`json_checker/${file}`, fs.readFileSync(path.join(CHECKER_DIR, file)));
  }
}
check('T1 corpus coverage non-trivial', t1Full > 0 && t1Sel > 0 && t1Rejected > 0,
  `full=${t1Full} selective=${t1Sel} rejected=${t1Rejected} escape=${t1Escape}`);

// === T2: duplicate keys + escaped keys (D1) =================================
console.log('[lazy] T2 duplicate-key semantics + escaped keys');
{
  const parser = ZeParser.open(binding, bytesOf('{"a":1,"a":2}'));
  try {
    check('T2 get() first-wins', parser.get('a', parser.rootTs, 0) !== undefined
      && (() => {
        const f = parser.get('a', parser.rootTs, 0);
        return parser.getDouble(f.ts) === 1;
      })());
    const projected = parser.project({ a: true });
    check('T2 project() last-wins', projected.a === 2);
    const parsed = parser.parse();
    check('T2 parse() last-wins (V8 parity)', parsed.a === 2);
  } finally {
    parser.close();
    binding.resetHeap();
  }
}
{
  // Escaped KEY (raw contains \t escape): decoded content "a<TAB>b".
  const parser = ZeParser.open(binding, bytesOf('{"a\\tb":"v"}'));
  try {
    const hit = parser.get('a\tb', parser.rootTs, 0);
    check('T2 escaped key found via decode path', hit !== undefined
      && parser.getString(hit.ts) === 'v');
    // D1 trap: needle "a\\tb" byte-equals the RAW escaped span but its
    // decoded content differs — must NOT match.
    const trap = parser.get('a\\tb', parser.rootTs, 0);
    check('T2 raw-equal escaped needle rejected', trap === undefined);
  } finally {
    parser.close();
    binding.resetHeap();
  }
}
{
  // Length-changing unicode escape in a key: decoded "aAb".
  const parser = ZeParser.open(binding, bytesOf('{"a\\u0041b":7}'));
  try {
    const hit = parser.get('aAb', parser.rootTs, 0);
    check('T2 length-changing escaped key found', hit !== undefined
      && parser.getDouble(hit.ts) === 7);
  } finally {
    parser.close();
    binding.resetHeap();
  }
}
{
  // Raw multi-byte UTF-8 key (no escapes): raw fast-compare path.
  const parser = ZeParser.open(binding, bytesOf('{"é":"ok"}'));
  try {
    const hit = parser.get('é', parser.rootTs, 0);
    check('T2 raw UTF-8 key fast path', hit !== undefined
      && parser.getString(hit.ts) === 'ok');
  } finally {
    parser.close();
    binding.resetHeap();
  }
}
{
  // "__proto__" stays an OWN key on the null-prototype projection.
  const parser = ZeParser.open(binding, bytesOf('{"__proto__":1}'));
  try {
    const projected = parser.project({ ['__proto__']: true });
    check('T2 __proto__ own key in projection',
      Object.getOwnPropertyDescriptor(projected, '__proto__')?.value === 1);
  } finally {
    parser.close();
    binding.resetHeap();
  }
}

// === T3: lifetime (D2) ======================================================
console.log('[lazy] T3 lifetime: reuse, pool exhaustion fail-loud, close semantics');
{
  let ok = true;
  for (let i = 0; i < 100; i += 1) {
    const parser = ZeParser.open(binding, bytesOf(`{"i":${i}}`));
    const f = parser.get('i', parser.rootTs, 0);
    if (f === undefined || parser.getDouble(f.ts) !== i) {
      ok = false;
      break;
    }
    parser.close();
  }
  binding.resetHeap();
  check('T3 open/close reuse over 100 documents', ok);
}
{
  // Exhaust the pool: 64 simultaneous slots, the 65th open fails loud.
  const held = [];
  let poolFull = false;
  for (let i = 0; i < 64; i += 1) {
    held.push(ZeParser.open(binding, bytesOf(`{"n":${i}}`)));
  }
  try {
    ZeParser.open(binding, bytesOf('{"n":65}'));
  } catch (err) {
    poolFull = err instanceof ZeParserError;
  }
  check('T3 pool exhaustion fails loud', poolFull);
  held[0].close();
  let afterRelease = false;
  let released;
  try {
    released = ZeParser.open(binding, bytesOf('{"n":65}'));
    afterRelease = released.getDouble(released.get('n', released.rootTs, 0).ts) === 65;
  } catch {
    afterRelease = false;
  } finally {
    released?.close();
  }
  check('T3 released slot is reusable', afterRelease);
  const last = held.pop();
  last.close();
  check('T3 double close is idempotent', (() => {
    try {
      last.close();
      return true;
    } catch {
      return false;
    }
  })());
  check('T3 use-after-close throws at the access point', (() => {
    try {
      held[0].kind(held[0].rootTs);
      return false;
    } catch (err) {
      return err instanceof ZeParserError;
    }
  })());
  for (const p of held) {
    p.close();
  }
  binding.resetHeap();
}

// === T4: skip semantics (D3) ================================================
console.log('[lazy] T4 skip: malformed subtrees never error while skipped');
{
  // get() steps PAST a malformed field value (launcher 't' of "tru" is a
  // valid launcher; the bad literal is only validated on access).
  const parser = ZeParser.open(binding, bytesOf('{"bad":{"a":tru},"good":7}'));
  try {
    const good = parser.get('good', parser.rootTs, 0);
    check('T4 get() skips malformed sibling', good !== undefined
      && parser.getDouble(good.ts) === 7);
    const projected = parser.project({ good: true });
    check('T4 project() skips malformed unlisted field', projected.good === 7);
    const bad = parser.get('bad', parser.rootTs, 0);
    check('T4 consuming the malformed subtree throws', (() => {
      try {
        new ZeLazyValue(parser, bad.ts, bad.cursor).get('a')?.asBool();
        return false;
      } catch (err) {
        return err instanceof ZeParserError;
      }
    })());
  } finally {
    parser.close();
    binding.resetHeap();
  }
}
{
  // items(): skipping = never dereference; iteration still completes.
  const parser = ZeParser.open(binding, bytesOf('[1,{"x":tru},2]'));
  try {
    const root = parser.lazy();
    const seen = [];
    for (const item of root.items()) {
      if (item.type() === 'number') {
        seen.push(item.asNumber());
      } else {
        seen.push('skipped'); // never dereferenced — no error
      }
    }
    check('T4 items() skip keeps iteration alive',
      JSON.stringify(seen) === '[1,"skipped",2]', JSON.stringify(seen));
  } finally {
    parser.close();
    binding.resetHeap();
  }
}
{
  // Deep skip: a 5-level nested subtree is jumped by at() on the sibling.
  const parser = ZeParser.open(binding, bytesOf('[[[[[1]]]],42]'));
  try {
    const sib = parser.at(1, parser.rootTs, 0);
    check('T4 deep subtree skipped by at()', sib !== undefined
      && parser.getDouble(sib.ts) === 42);
  } finally {
    parser.close();
    binding.resetHeap();
  }
}

// === T5: at()/count() vs V8 =================================================
console.log('[lazy] T5 at()/count() bounds and parity');
{
  const arr = Array.from({ length: 1000 }, (_, i) => i);
  const parser = ZeParser.open(binding, bytesOf(JSON.stringify(arr)));
  try {
    const root = parser.rootTs;
    check('T5 count() matches length', parser.countElements(root) === 1000);
    check('T5 at(0) first element', parser.getDouble(parser.at(0, root, 0).ts) === 0);
    check('T5 at(999) last element', parser.getDouble(parser.at(999, root, 0).ts) === 999);
    check('T5 at(1000) out of range -> undefined', parser.at(1000, root, 0) === undefined);
    check('T5 at(-1) -> undefined', parser.at(-1, root, 0) === undefined);
  } finally {
    parser.close();
    binding.resetHeap();
  }
}
{
  const parser = ZeParser.open(binding, bytesOf('{"a":1,"b":2,"c":3}'));
  try {
    check('T5 object count() == field count', parser.countElements(parser.rootTs) === 3);
  } finally {
    parser.close();
    binding.resetHeap();
  }
}

// === T6: scalar edges vs V8 (promoted through get()) ========================
console.log('[lazy] T6 scalar parity through lazy access');
{
  const text = '{"s":"tab\\tnewline\\n\\u0000end","u":"\\ud83d\\ude00","bom":"\\ufeffB",'
    + '"n0":-0,"n1":9007199254740993,"n2":5e-324,"n3":1e400,"n4":-2.5e2}';
  const parser = ZeParser.open(binding, bytesOf(text));
  try {
    const v8 = JSON.parse(text);
    const root = parser.rootTs;
    const get = (key) => parser.get(key, root, 0);
    check('T6 escaped string parity', parser.getString(get('s').ts) === v8.s);
    check('T6 surrogate pair parity', parser.getString(get('u').ts) === v8.u);
    check('T6 BOM escape parity', parser.getString(get('bom').ts) === v8.bom);
    check('T6 -0 parity (Object.is)', Object.is(parser.getDouble(get('n0').ts), v8.n0));
    check('T6 big-int double parity', parser.getDouble(get('n1').ts) === v8.n1);
    check('T6 subnormal parity', parser.getDouble(get('n2').ts) === v8.n2);
    check('T6 Infinity parity', parser.getDouble(get('n3').ts) === v8.n3);
    check('T6 exponent parity', parser.getDouble(get('n4').ts) === v8.n4);
    const lazyRoot = parser.lazy();
    check('T6 type() classification', lazyRoot.type() === 'object'
      && lazyRoot.get('s').type() === 'string'
      && lazyRoot.get('n0').type() === 'number'
      && NODE_KINDS[7] === 'null');
  } finally {
    parser.close();
    binding.resetHeap();
  }
}

// === shape-contract edges (fail-loud + absent-key semantics) ================
console.log('[lazy] shape contract edges');
{
  const parser = ZeParser.open(binding, bytesOf('{"a":{"b":1},"c":[1,2],"d":3}'));
  try {
    check('shape nested projection', (() => {
      const r = parser.project({ a: { b: true } });
      return r.a.b === 1 && Object.keys(r).length === 1;
    })());
    check('shape $count on array', parser.project({ c: { $count: true } }).c === 2);
    check('shape $array take-all', (() => {
      const r = parser.project({ c: { $array: true } });
      return JSON.stringify(r.c) === '[1,2]';
    })());
    check('shape true = full subtree', (() => {
      const r = parser.project({ a: true });
      return JSON.stringify(r.a) === '{"b":1}';
    })());
    check('shape listed-but-absent key stays absent', (() => {
      const r = parser.project({ missing: true, d: true });
      return !('missing' in r) && r.d === 3;
    })());
    check('shape type mismatch fails loud', (() => {
      try {
        parser.project({ d: { $array: true } });
        return false;
      } catch (err) {
        return err instanceof ZeParserError;
      }
    })());
    check('shape non-object fails loud', (() => {
      try {
        parser.project({ a: 1 });
        return false;
      } catch (err) {
        return err instanceof ZeParserError;
      }
    })());
    check('shape $array on object fails loud', (() => {
      try {
        parser.project({ a: { $array: true } });
        return false;
      } catch (err) {
        return err instanceof ZeParserError;
      }
    })());
    check('get() on non-object fails loud', (() => {
      try {
        parser.get('x', parser.at(0, parser.get('c', parser.rootTs, 0).ts,
          parser.get('c', parser.rootTs, 0).cursor).ts, 0);
        return false;
      } catch (err) {
        return err instanceof ZeParserError;
      }
    })());
  } finally {
    parser.close();
    binding.resetHeap();
  }
}

console.log(`[lazy] ${checks} check(s): ${failures === 0 ? 'all pass'
  : `${failures} failure(s)`}`);
process.exit(failures === 0 ? 0 : 1);
