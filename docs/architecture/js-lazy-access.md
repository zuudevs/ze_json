# JS lazy-access layer (as-built, ND-4)

`bindings/js/ze_json.mjs` adds a **JS-only** lazy-access layer on the
UNCHANGED v3 wasm surface (no ABI change, no wasm rebuild). Design contract
and numbered decisions D1–D3: workspace root
`docs/architecture/lazy-access-api.md`.

- **L1** — `ZeParser.get(key, ts, cursor)` (FIRST-wins early exit by design
  decision D1 — a documented divergence from `JSON.parse` last-wins),
  `ZeParser.at(index, ts, cursor)`, `sourceView()`, `materializeAt()` and
  the `lazy()` handle `ZeLazyValue` (`type`, `count`, `at`, `get`,
  `asString`/`asNumber`/`asBool`/`isNull`, `value`, plus the iteration
  generators `entries()`/`items()` where skipping a subtree means never
  dereferencing the yielded handle — D3, no new ABI export).
- **L2** — `ZeParser.project(shape)`: selected-subset V8-shape
  materialization in ONE forward pass. Shape grammar: `true` (take the
  value; containers fully materialize), `{$count: true}` (index-walk
  length), `{$array: itemShape}`, object shape (unlisted fields are
  stepped past, listed-but-absent keys stay ABSENT — `??` fallback parity
  with `JSON.parse`, duplicates last-wins, null-prototype target).
- Key matching is on DECODED content: a key frame's raw span is re-derived
  from the source view with a skip-pair walk (key frames carry
  `cursor == opening-quote position`, NOT the colon index — the span is
  therefore walked to its first unescaped `"`), an escape-free span is a
  raw byte compare (`TextEncoder` needle), an escaped span is decoded once
  and compared (rare path).
- Lifetime (D2): open → traverse → `close()` per document in
  `try/finally`; slot pool exhaustion (64) fails loud as `ZeParserError`;
  `binding.resetHeap()` only between batches.

Tests: `bindings/js/tests/lazy.mjs` (43 checks — T1 corpus-wide full+selective
projection parity vs `JSON.parse`, T2 duplicate/escaped/UTF-8/`__proto__`
keys, T3 lifetime + pool exhaustion, T4 skip semantics, T5 `at()`/`count()`
bounds, T6 scalar edges) plus the unchanged `npm run smoke` /
`npm run differential` gates (T7). The preliminary probe lives in
`bindings/js/benchmarks/` (labelled preliminary — measured records are ND-6
scope, see the thesis workspace measurement docs).

Related: [`ondemand-parser.md`](ondemand-parser.md) — the C++ layer under
this API; [`wasm-abi.md`](wasm-abi.md) — the v3 surface it rides on.
