# ADR 0002 — scanner64 SWAR as production default

- **Status:** Rejected (as production default; the variant is kept and
  exported for measurement)
- **Date:** measured in phase P4 (2026-11); recorded 2026-11-12
- **Deciders:** thesis measurement campaign + library owner

## Context

The scanner contract (see
[`../architecture/scanner-contract.md`](../architecture/scanner-contract.md))
requires every variant to reproduce `reference_scan()` index-for-index.
Two vectorized candidates were implemented and measured against the
table-driven `scanner8`:

- `scanner64` — SWAR 8-lane, three-path hybrid (fast outside-string /
  fast inside-string / serial fallback)
- `scanner128` — SSE2 16-lane, same three-path shape

Measurement basis: google-benchmark, `native-release`, P0-8 corpora
(synthetic ladder + `saq_payload_v1` + checker batch), dev device,
in-situ. Both vector variants lost to `scanner8` on **5 of 6 corpora**.

Why: in compact and pretty JSON the quote density is roughly one quote
every 6–15 bytes, so nearly every 8/16-byte block contains a quote and
falls to the serial path. The vector classification overhead
(6× SWAR equality, 12 `cmpeq` ops) then exceeds `scanner8`'s single L1
table load per byte (`kJsonCharClass`).

## Decision

- `structural_scan` (the production entry point) dispatches to
  **`scanner8`**, the measured winner on compact and pretty corpora.
- `scanner64` and `scanner128` remain compiled, exported (via
  `SCANNER_MODES` in the binding) and covered by the differential gate —
  they exist for the thesis comparison matrix and profiling, never as the
  default.
- The negative result is preserved as evidence (one change per
  measurement, revert documented), per the workspace methodology.

## Consequences

- Beating the table-driven scanner requires a simdjson-style **full-mask
  pipeline** (prefix-xor quote mask + SIMD escape-run handling) rather
  than another three-path hybrid — that remains future work, to be
  measured with the same gate chain.
- Full tables and analysis: thesis workspace
  `reports/P4-bottleneck-report.md` (raw exhibits and validated records
  under `out/` there, not committed).

@cite 10.14778_3115404.3115416 Mison — the structural-index shape whose
scan cost the variants were compared on.
