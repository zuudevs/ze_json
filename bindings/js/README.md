# @zuudevs/ze-json

Raw wasm64 C-ABI binding for the `ze_json` on-demand JSON parser.
Synchronous module load, no runtime dependencies, no imports into the
wasm module — Node.js ≥ 24 (memory64).

Sections 1–19 cover installation, the scanner surface, the on-demand
parser, the lazy-access layer, lifetime, and errors. Everything here is
generated from the implementation in [`ze_json.mjs`](ze_json.mjs); the
C-ABI contract it rides on lives in
`include/ze_json/abi/cabi.hpp` (single source of truth).

## 1. Overview

`@zuudevs/ze-json` is the npm distribution of `ze_json` for Node.js:
a wasm64 module exposing a structural scanner, an on-demand parser, and
a JS-only lazy-access layer built on top of the unchanged parser surface.

- **Scanner** — one linear pass emits the byte offsets of `[ ] { } , :`
  outside strings (the structural index), in several selectable variants.
- **On-demand parser** — access a document through single forward
  iterator frames; materialize everything (`parse()`) or only what you
  need (`lazy()`, `project()`).
- **V8 parity** — scalar semantics, object shape and duplicate-key
  behavior are locked to `JSON.parse` (documented divergences called out
  below).

Design doc: `docs/architecture/` (repository). Changelog:
[`changelog.md`](../docs/development/changelog.md).

## 2. Requirements

| Requirement | Value |
|---|---|
| Node.js | ≥ 24 (wasm memory64; tested on 24.19.0) |
| Platforms | any Node-supported platform (pure wasm; no native add-ons) |
| Package size | ships `ze_json.mjs`, `ze_json.wasm` (64 MiB initial linear memory, exported), `tests/`, `benchmarks/` |

## 3. Installation

The package is published to **GitHub Packages**
(`npm.pkg.github.com`, scope `@zuudevs`). Add to your project `.npmrc`:

```ini
@zuudevs:registry=https://npm.pkg.github.com
```

Authenticate with a GitHub token that has the `read:packages` scope:

```ini
//npm.pkg.github.com/:_authToken=<YOUR_GITHUB_TOKEN>
always-auth=true
```

Then:

```sh
npm install @zuudevs/ze-json
```

## 4. Quickstart

```js
import { ZeJson, ZeParser } from '@zuudevs/ze-json';

const ze = ZeJson.load(); // loads ze_json.wasm next to the module (sync)
const bytes = new TextEncoder().encode('{"a":1,"b":{"c":2}}');

// Full materialization (V8 JSON.parse shape semantics)
const parser = ZeParser.open(ze, bytes);
try {
  const value = parser.parse(); // { a: 1, b: { c: 2 } }
} finally {
  parser.close(); // release the parser slot
}

// Lazy access (L1): only walk what you need
const p = ZeParser.open(ze, bytes);
try {
  const root = p.lazy();
  const b = root.get('b');       // ZeLazyValue | undefined
  b?.get('c')?.asNumber();       // 2 — the 'a' subtree is never decoded
} finally {
  p.close();
}

// Selected projection (L2): one forward pass over the shape
const p2 = ZeParser.open(ze, bytes);
try {
  p2.project({ a: true, b: { $count: true } }); // { a: 1, b: 2 }
} finally {
  p2.close();
}
```

## 5. Loading the module (`ZeJson`)

`ZeJson` wraps the raw wasm exports and is the factory for everything
else.

```js
import { ZeJson, ABI_VERSION } from '@zuudevs/ze-json';

const ze = ZeJson.load();                 // default: ze_json.wasm beside ze_json.mjs
const ze2 = ZeJson.load('/path/to/ze_json.wasm');
```

- `ZeJson.load(wasmPath?)` — synchronous `readFileSync` + `WebAssembly`
  instantiation with **no imports**; verifies `ze_abi_version()` against
  the binding's `ABI_VERSION` (mismatch → `Error`).
- `ze.abiVersion()` — numeric ABI version (3 for this release).
- `ze.defaultScanner` — `'structural'` (the library's measured static
  dispatch; `scanner8` today).
- `ze.hasScanner(mode)` / `ze.assertScanner(mode)` — availability check
  / loud failure for a scanner mode.
- `ze.alloc(byteLength)` — 16-byte-aligned bump allocation; **no free**
  (rewind wholesale with `resetHeap()`).
- `ze.resetHeap()` — rewind the bump allocator between batches; also
  closes every open parser slot (do this only when no parser is in use).
- `ze.writeBytes(ptr, bytes)` / `ze.readBytes(ptr, byteLength)` — move
  bytes in/out of linear memory (`readBytes` returns a live view; see §16).
- `ze.readIndices(dstPtr, count)` — copy `count` 64-bit structural
  indexes out as a JS number array.

## 6. Raw scanner surface (`ZeDocument`)

`ZeDocument` uploads a byte source once and scans it many times.

```js
import { ZeDocument } from '@zuudevs/ze-json';

const doc = new ZeDocument(ze, bytes); // scanner defaults to 'structural'
const result = doc.scan();             // { ok: true, count } — number of indexes
const indices = doc.indices(result);   // [123, 125, ...] byte offsets
```

- `new ZeDocument(binding, bytes, { scanner })` — allocates source and
  destination regions from the bump heap (worst case one index per byte);
  `assertScanner` fails loud at construction time for an unknown mode.
- `doc.scan(mode?)` — runs the chosen scanner; returns a packed result:
  `{ ok: true, count }` or `{ ok: false, error: 'InvalidJson', code }`
  (error names mirror `core::JsonErrc`, see §15).
- `doc.indices(result)` — reads the index array (empty array on error).
- `doc.byteLength` / `doc.scanner` — introspection.
- Back-compat aliases: `doc.scan8()`, `doc.referenceScan()`.

## 7. Scanner mode selection

`SCANNER_MODES` selects the variant per scan — mirrors the v2 export
table in the ABI header:

| Mode | Export | Status |
|---|---|---|
| `'structural'` | `ze_structural_scan` | **default** — measured static dispatch |
| `'scanner8'` | `ze_scanner8` | table-driven, measured winner |
| `'scanner64'` | `ze_scanner64` | SWAR 8-lane, tested & rejected (ADR 0002) |
| `'scanner128'` | `ze_scanner128` | SSE2 16-lane, tested & rejected (ADR 0002) |
| `'reference'` | `ze_reference_scan` | table-free correctness anchor |

Explicit variants exist for the thesis comparison matrix and profiling;
production code should keep `'structural'`.

## 8. On-demand parser (`ZeParser.open`)

```js
import { ZeParser } from '@zuudevs/ze-json';

const parser = ZeParser.open(ze, bytes); // upload + open + root frame
try {
  // ... access ...
} finally {
  parser.close(); // idempotent
}
```

- `ZeParser.open(binding, bytes)` — allocates source scratch + decode
  buffers from the bump heap, runs `Document::open` (ONE structural scan
  + bracket-match table), and eagerly frames the root. Open or root
  failures throw `ZeParserError` with the ABI error name (§15).
- One parser = one slot of the static pool (64 slots, §14).
- `parser.slot` / `parser.rootTs` — slot id and root token start.

## 9. Full materialization (`parse()`)

```js
const value = parser.parse(); // the whole document as a JS value
```

`parse()` replays the library's forward iterator and reproduces **V8
`JSON.parse` verdict-and-shape semantics**:

- objects are **null-prototype**; keys are plain data properties
  (`"__proto__"` stays an own key — no prototype-chain setter is hit);
- duplicate keys are **last-wins** at the first-occurrence position;
- numbers are exact doubles with V8-locked rounding, `-0` preserved;
- strings decode through the P7-3 decoder (see §16 for the lone-surrogate
  caveat — the one documented wrapper-boundary divergence).

@cite 10.1002_spe.3313 on-demand JSON — unvisited subtrees pay nothing.

## 10. Low-level frame walk

Every access point is a frame `(ts, cursor)` — token start and
continuation cursor — fed back into the next call (stateless v3 walk
protocol):

```js
const root = parser.rootTs;

parser.kind(ts);          // NODE_KINDS index: 'object'|'array'|'string'|'number'|'true'|'false'|'null'|'unknown'
parser.countElements(ts); // container length (index walk; framing verdict included)
parser.getString(ts);     // decoded JS string at ts
parser.getDouble(ts);     // exact double at ts
parser.getBool(ts);       // true/false (non-literal ts → ZeParserError)
parser.isNull(ts);        // true for a "null" literal

// Containers: step family
const first = parser.containerBegin(ts, cursor);   // first child frame or null (empty container)
const next  = parser.arrayNext(childTs, childCursor);    // ArrayIter++ → frame | null
const field = parser.objectField(keyTs, keyCursor);      // key frame → VALUE frame
const nextKey = parser.objectNext(valueTs, valueCursor); // value frame → next KEY frame | null
```

- Frames are `{ cursor, ts }`; a key frame carries
  `cursor = opening-quote position` (not the colon index).
- `countElements` walks the whole subtree one step per child — cheap for
  framing, but do not call it per element in hot loops (see the R5-2
  finding in the repository docs).
- Walk errors throw `ZeParserError` (a broken document surfaces as a
  framing/grammar verdict at the access point, never as undefined
  behavior).

## 11. Lazy access — L1 handles (`ZeLazyValue`)

`ZeLazyValue` is a stateless handle over one frame: nothing decodes or
materializes unless a method demands it.

```js
const root = parser.lazy();   // L1 handle over the root value

root.type();                  // 'object' | 'array' | 'string' | ...
root.count();                 // child count (containers)
root.at(index);               // array child → ZeLazyValue | undefined
root.get(key);                // object field → ZeLazyValue | undefined
root.asString();              // decoded string at this frame
root.asNumber();              // exact double at this frame
root.asBool();                // true/false literal at this frame
root.isNull();                // "null" check at this frame
root.value();                 // FULL V8-shape materialization of this subtree
```

- `get(key)` is **FIRST-wins with early exit** (design decision D1) — a
  documented divergence from `JSON.parse`'s last-wins; `project()` and
  `parse()` are last-wins.
- Key matching compares **decoded content**: escape-free keys are matched
  by raw byte compare over the source view; escaped keys (rare) are
  decoded once and compared.
- Handles are bound to an open parser — using one after `close()` throws
  at the access point.

## 12. Lazy access — L2 projection (`project()`)

`project(shape)` materializes a selected subset in ONE forward pass;
subtrees outside the shape are skipped (one step + gallop, never a
decode):

```js
parser.project({
  schedules: { $array: { day: true, hour: true } }, // every element: only these fields
  assistants: { $count: true },                     // container length only
  meta: true,                                       // fully materialize this subtree
});
```

Shape grammar:

| Shape | Meaning |
|---|---|
| `true` | take the value (scalars decoded; containers fully materialized) |
| `{ $count: true }` | container length (index walk, no children) |
| `{ $array: itemShape }` | project every element by `itemShape` |
| `{ key: shape, ... }` | object projection — unlisted fields stepped past, listed-but-absent keys stay **absent** (never `null` — `??` fallback parity with `JSON.parse`) |

- `$count` / `$array` are reserved; type mismatches fail loud
  (`ZeParserError` with the offending path, e.g. `project.schedules[3]`).
- Duplicate keys are **last-wins** (plain assignment on a null-prototype
  target — exactly `JSON.parse` semantics), unlike `get()` (§11).

## 13. Iteration and skipping

```js
for (const { key, value } of root.entries()) { /* object fields */ }
for (const item of root.items()) { /* array children */ }
```

- `entries()` yields `{ key, value }` — every key pays a decode; hot
  paths should use `get()` / `project()` instead.
- **Skipping (D3):** to skip a child, simply never dereference the
  yielded handle — the next step jumps past the subtree via the
  structural index, and a malformed skipped subtree can never error
  (it is never validated).

## 14. Memory and lifetime

- The wasm module has a fixed, exported **memory64** (64 MiB initial;
  nothing grows it) and a **bump allocator** (`ze_alloc`, 16-byte
  aligned) with no per-allocation free.
- Parser slots: static pool of `PARSER_SLOT_MAX` = 64; exhaustion fails
  loud (`ZeParserError`).
- Documented lifetime (D2): `open → traverse → close()` per document in
  `try/finally`; `ze.resetHeap()` **between batches only** (it closes
  every slot). Buffers stay committed until the rewind.
- Structural scan buffers (`ZeDocument`) share the same heap — size
  batches accordingly or rewind between corpora.

## 15. Error handling

```js
import { JSON_ERRORS } from '@zuudevs/ze-json';
// ['None', 'EmptyJson', 'InvalidJson', 'UnexpectedQuote',
//  'InvalidQuote', 'InsufficientBuffer', 'InvalidUtf8']
```

- `ZeParserError extends Error` — thrown by every wrapper for ABI
  errors, framing/grammar verdicts, and driver contract violations.
  Carries `.code` (the `core::JsonErrc` index) when it originates from an
  export result.
- Scanner packed result: `{ ok: false, error: 'InvalidJson', code }`.
- Frame-family packing (low-level §10): `hi === 0` ok;
  `hi === STEP_SENTINEL` (0xffffffff) container exhausted;
  `hi !== 0 && lo === 0` error with `code = hi`.

## 16. Source view and string semantics

- `parser.sourceView()` — live `Uint8Array` over the uploaded source
  bytes (raw key compare, zero copy). Treat it as unstable across
  `resetHeap()`; use it immediately.
- Strings decode via a module-level `TextDecoder('utf8',
  { ignoreBOM: true })` — a leading U+FEFF is **kept**, matching
  `JSON.parse` (`\uFEFF` is legal in JSON strings).
- Lone-surrogate escapes pass through **verbatim** as their literal
  6-byte escape text (lossless encoding decision) — they are therefore
  not byte-equal to a V8 string built from the same escape. This is the
  one documented wrapper-boundary divergence from `JSON.parse`.
- Decode buffers auto-grow from the bump heap (`InsufficientBuffer`
  → retry with doubled capacity) until the next `resetHeap()`.

## 17. Tests

```sh
npm run smoke        # ABI + scan cases + in-module agreement (tests/main.mjs)
npm run differential # corpus + json_checker: wasm scanner8 == wasm reference == JS reference (tests/differential.mjs)
npm run lazy         # 43 lazy-layer checks (tests/lazy.mjs):
                     #   T1 corpus-wide full+selective projection parity vs JSON.parse
                     #   T2 duplicate / escaped / UTF-8 / __proto__ keys
                     #   T3 lifetime + slot-pool exhaustion
                     #   T4 skip semantics
                     #   T5 at()/count() bounds
                     #   T6 scalar edges
                     #   T7 smoke/differential regression gate
```

`tests/reference.mjs` provides the plain-JS reference scanner used by the
differential gate.

## 18. Performance notes

ze_json is a measurement artifact: every performance claim is bound to a
schema-v1 measurement record validated by the thesis workspace
(`scripts/measurement-schema/validate.py`), and numbers are only compared
within one run (same machine, preset, git hash, corpus).

- Scanner + full-walk + selective-access benchmarks live in the
  repository (`benchmarks/`) and the thesis workspace harness
  (`scripts/node-benchmark/harness.mjs`, engines `v8` / `ze-wasm64` /
  `ze-parser`).
- The on-demand lazy path was measured in-situ on the SAQ server
  workload: the mongo-cache read path reached **2.85×–3.00×** the V8
  baseline (p95 ≥ 1.5×) — see the thesis workspace
  `reports/ondemand-saq-evaluation.md`.
- `bindings/js/benchmarks/lazy_probe.mjs` is a preliminary probe, labelled
  as such — measured records come from the harness, not the probe.

## 19. ABI contract and versioning

- Contract header: `include/ze_json/abi/cabi.hpp` (single source of
  truth); `ze_json.mjs` mirrors it. `ABI_VERSION` = 3 today.
- `PARSER_SLOT_MAX` = 64, `STEP_SENTINEL` = 0xffffffff — both mirror the
  C++ constants.
- Any C-ABI change bumps `kAbiVersion`, updates `ze_json.mjs` + tests
  together, and is recorded in the repository ADRs
  (`docs/adr/0003-abi-v3-slot-pool.md` documents the v3 decision).
- Releases: npm package version and CMake project version move together
  (see [`docs/development/changelog.md`](../../docs/development/changelog.md)); a `v*` tag triggers the
  publish workflow to GitHub Packages.

## License

MIT — see [`package.json`](package.json).
