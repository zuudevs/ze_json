# ze_json documentation

Documentation for the `ze_json` library — a header-first, on-demand JSON
parsing library for C++23 (namespace `zuu`, no STL in core), measured
comparatively against the V8 baseline used by the SAQ server.

| Section | Contents |
|---|---|
| [`architecture/`](architecture/README.md) | Per-topic modules: layers, scanner contract and variants, on-demand parser, JS lazy access, wasm ABI, measurement architecture (Mermaid) |
| [`development/`](development/README.md) | Build, test, measurement workflow from a contributor's perspective (Mermaid) |
| [`requirements/`](requirements/README.md) | Toolchain versions, environment variables, corpus requirements, CI parity |
| [`adr/`](adr/README.md) | Architecture decision records |

Entry points outside this tree:

- [`../README.md`](../README.md) — repository landing page
- [`../CHANGELOG.md`](../CHANGELOG.md) — version history (npm package and
  CMake project version move together)
- [`../bindings/js/README.md`](../bindings/js/README.md) — npm package API
  reference (`@zuudevs/ze-json`)

Documentation is written in English; all diagrams use embedded Mermaid
syntax (no binary images). Prose was finalized in phase P6 of the thesis
workspace, re-aligned with the as-built on-demand parser / ABI v3 in the
Stage 2 correction (2026-11-12), and restructured into per-topic modules
at the 2026-11-12 documentation architecture overhaul (v0.1.1).

Related, in the thesis workspace (one level above this repository):

- `AGENTS.md` — session rules, measurement conventions
- `schemas/measurement.schema.json` — measurement record schema v1
- `corpus/benchmarks/README.md` — benchmark corpora and manifests
- `docs/project/tasks.md` — thesis task ledger
