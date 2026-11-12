# Architecture decision records

ADRs capture significant architectural decisions for `ze_json` with their
context and consequences. Each record is immutable: supersede a decision by
writing a new ADR that references it, never by editing the old one.

## Index

| ADR | Title | Status |
|---|---|---|
| [0002](0002-scanner64-swar-rejected.md) | scanner64 SWAR as production default | Rejected |
| [0003](0003-abi-v3-slot-pool.md) | ABI v3 parser surface on a static slot pool | Accepted |

## Numbering convention

This repository and the thesis workspace share one continuous ADR numbering
(convention adopted at the 2026-11-12 documentation architecture overhaul):
each ADR keeps its original number even when it lives in the other
repository. The thesis workspace (`docs/adr/` there) holds the
measurement/research-side records — 0001 (on-demand direction),
0004 (corpus `saq_payload_v1`), 0005 (sole-owner parser slot `readCount`),
0006 (`cache_bytes` metric), 0007 (measurement schema v1 + extras); this
repository holds the library-side records.

## Related workspace decisions

Design contract D1–D3 for the JS lazy-access layer: workspace root
`docs/architecture/lazy-access-api.md` (design document, not an ADR).
