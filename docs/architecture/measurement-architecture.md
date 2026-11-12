# Measurement architecture

Each scanner variant is benchmarked through `benchmarks/scanner_benchmark.cpp`
(google-benchmark, 5 repetitions, P0-8 corpora) exported via CMake helpers
with a metadata sidecar, then normalized into schema-v1 measurement records
(6/record-set, one per corpus) and validated. The on-demand parser is
benchmarked separately through `benchmarks/parser_benchmark.cpp` (phases:
reference / open / structural walk-only / full walk, see
[`ondemand-parser.md`](ondemand-parser.md)). The node harness
(`scripts/node-benchmark/harness.mjs`, thesis workspace root) runs the same
corpora for engines `v8` and `ze-wasm64` (contexts `wasm-scan`, `wasm-open`,
`wasm-walk-full`, `wasm-parse-full`) through the same record schema — see
[`../development/README.md`](../development/README.md) for the canonical
pipeline commands.

Comparison discipline: numbers are compared **within one run only** (same
machine, preset, git hash, corpus). Cross-machine / cross-preset /
cross-hash comparisons are forbidden by the workspace measurement
conventions.

Further selective-access API work is tracked in the workspace audit
`docs/research/audit-2026-11-12.md` (§9 proposal, historical snapshot) and
the thesis workspace task ledger `docs/project/tasks.md` (ND entries).
