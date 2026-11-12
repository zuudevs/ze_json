# Architecture

`ze_json` is a header-first C++23 library for **on-demand** JSON processing
(namespace `zuu`, no STL in core) with a raw wasm64 C-ABI surface for Node.js
integration. It is measured comparatively against the V8 baseline used by the
SAQ server (see [`../development/README.md`](../development/README.md) for the
measurement workflow).

| Document | Contents |
|---|---|
| [`layers-and-data-flow.md`](layers-and-data-flow.md) | Core layer map + end-to-end data flow |
| [`scanner-contract.md`](scanner-contract.md) | The `reference_scan()` correctness contract every scanner variant must reproduce |
| [`scanner-variants.md`](scanner-variants.md) | `scanner8` / `scanner64` / `scanner128` / `scanner32` + the measured dispatch decision |
| [`ondemand-parser.md`](ondemand-parser.md) | `Document` / `Value` single forward-iterator design (as-built since P7) |
| [`js-lazy-access.md`](js-lazy-access.md) | JS-only L1/L2 lazy access on the unchanged v3 wasm surface (ND-4) |
| [`wasm-abi.md`](wasm-abi.md) | wasm64 C-ABI v3 surface: export groups, result packing, slot pool |
| [`measurement-architecture.md`](measurement-architecture.md) | How benchmark artifacts become validated schema-v1 measurement records |

Design contract and numbered decisions D1–D3 for the JS lazy-access layer:
workspace root `docs/architecture/lazy-access-api.md`. The as-built direction
audit is preserved as a historical snapshot at workspace root
`docs/research/audit-2026-11-12.md`.
