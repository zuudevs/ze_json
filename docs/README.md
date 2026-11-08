# ze_json documentation

Documentation for the `ze_json` library — a header-first, on-demand JSON
parsing library for C++23 (namespace `zuu`, no STL in core), measured
comparatively against the V8 baseline used by the SAQ server.

| Section | Contents |
|---|---|
| [`architecture/`](architecture/README.md) | Components, layers, scanner contract, data flow (Mermaid) |
| [`development/`](development/README.md) | Build, test, measurement workflow from a contributor's perspective (Mermaid) |
| [`requirement/`](requirement/README.md) | Toolchain versions, environment variables, corpus requirements |

Documentation is written in English; all diagrams use embedded Mermaid
syntax (no binary images). Prose here is finalized in phase P6 (TASKS.md);
until then sections carry skeletons with explicit TODO markers.

Related, outside this tree:

- `AGENTS.md` (workspace root) — session rules, measurement conventions
- `schemas/measurement.schema.json` — measurement record schema v1
- `corpus/benchmarks/README.md` — benchmark corpora and manifests
