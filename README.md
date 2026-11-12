# ze_json

**On-demand JSON parsing for C++23** — a header-first library (namespace
`zuu`, no STL in the core) with a raw **wasm64 C-ABI** surface for
Node.js, published as the npm package
[`@zuudevs/ze-json`](https://github.com/zuudevs/ze_json/packages).

ze_json is developed as the comparative-study artifact of a thesis
measuring the JSON paths used by the SAQ server (`saq-report/`), whose
baseline is V8's built-in `JSON.parse` / `JSON.stringify`. The library
is **correctness-locked to that baseline** (V8-parity scalar semantics,
null-prototype objects, last-wins duplicate keys) and every performance
claim is bound to a validated measurement record — see
[Measurement discipline](#measurement-discipline).

## Features

- **Structural scanner** — one linear pass emits the byte offsets of
  `[ ] { } , :` outside strings. Multiple selectable variants
  (`scanner8` table-driven default, SWAR/SIMD variants, table-free
  `reference_scan` correctness anchor), all index-identical by a
  differential gate.
- **On-demand parser** — `Document::open` scans once and builds a
  bracket-match table; access is a single forward iterator over
  `(token_start, cursor)` frames. Subtrees you never visit are never
  validated.
- **Lazy access from JS** — `ZeParser.lazy()` (L1) and
  `ZeParser.project(shape)` (L2): read only the fields you need, or
  materialize a selected subset in one pass with V8 `JSON.parse`
  shape semantics.
- **Full materialization parity** — `ZeParser.parse()` reproduces
  `JSON.parse` verdict-and-shape semantics (null-prototype objects,
  `__proto__` as own key, duplicates last-wins, `-0` preserved).

## Quickstart (Node.js ≥ 24)

```sh
npm install @zuudevs/ze-json
```

The package is published to **GitHub Packages**; first configure your
project `.npmrc`:

```ini
@zuudevs:registry=https://npm.pkg.github.com
```

(Generate a GitHub token with `read:packages` scope and add it as
`//npm.pkg.github.com/:_authToken=<TOKEN>`.)

```js
import { ZeJson, ZeParser } from '@zuudevs/ze-json';

const ze = ZeJson.load();               // loads ze_json.wasm next to the module
const bytes = new TextEncoder().encode('{"a":1,"b":{"c":2}}');

// Full materialization (V8 JSON.parse shape semantics)
const parser = ZeParser.open(ze, bytes);
try {
  const value = parser.parse();          // { a: 1, b: { c: 2 } }
} finally {
  parser.close();                        // release the parser slot
}

// Lazy access: only walk what you need (L1)
const p = ZeParser.open(ze, bytes);
try {
  const root = p.lazy();
  const b = root.get('b');               // ZeLazyValue | undefined
  b?.get('c')?.asNumber();               // 2 — 'a' is never decoded
  p.project({ a: true, b: { $count: true } }); // L2: { a: 1, b: 2 }
} finally {
  p.close();
}
```

Full API reference: [`bindings/js/README.md`](bindings/js/README.md).

## Quickstart (C++23)

Consume the header-first library directly (see
[`docs/architecture/ondemand-parser.md`](docs/architecture/ondemand-parser.md)
for the layer walkthrough and
[`docs/architecture/scanner-contract.md`](docs/architecture/scanner-contract.md)
for the scanner contract).

## Measurement discipline

ze_json is a measurement artifact: parser/scanner changes follow the
gate chain **baseline → profile → one change → differential test →
re-measure**, and every reported number comes from a schema-v1
measurement record that passed the validator. Numbers are only ever
compared within one run (same machine, preset, git hash, corpus).
Details:

- [`docs/development/README.md`](docs/development/README.md) — build,
  test and measurement workflow
- [`docs/architecture/measurement-architecture.md`](docs/architecture/measurement-architecture.md) —
  record pipeline and the node harness
- [CHANGELOG.md](CHANGELOG.md) — version history

## Documentation

| Path | Contents |
|---|---|
| [`docs/architecture/`](docs/architecture/README.md) | Layers, scanner contract, on-demand parser, wasm ABI, lazy-access layer |
| [`docs/requirements/`](docs/requirements/README.md) | Toolchain versions, environment, presets, corpora |
| [`docs/development/`](docs/development/README.md) | Build/test/measurement workflow, binding development |
| [`docs/adr/`](docs/adr/README.md) | Architecture decision records |
| [`bindings/js/README.md`](bindings/js/README.md) | npm package API reference |

## License

MIT — see `bindings/js/package.json`.
