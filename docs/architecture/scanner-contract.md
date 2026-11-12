# The scanner contract

Every scanner variant MUST reproduce `reference_scan()`
(`include/ze_json/ondemand/reference.hpp`) index-for-index and
error-for-error. The reference is deliberately table-free so a codegen bug
cannot corrupt the correctness anchor. Contract:

- output = byte offsets of `[ ] { } , :` occurring **outside** strings
- `"` toggles string state; `\` inside a string escapes the next byte
- errors (with precedence): `EmptyJson` (len 0) → `InvalidJson` (null
  source, dangling escape, unclosed string) → `InsufficientBuffer`
  (null/zero destination, capacity exhausted — `dst` then holds exactly
  `dst_cap` indices)
- UTF-8 validation and JSON grammar checks belong to later stages
- capacity overflow inside serial paths is propagated as
  `InsufficientBuffer` (regression-added in P4; previously silently ignored)

The differential gate (`tests/scanner_test.cpp`) runs fixtures, deterministic
fuzz (300+500 iterations), the full json_checker corpus (with documented RFC
8259 legacy exceptions `fail1`/`fail18`), and a 256-byte validation of the
generated `kJsonCharClass` table itself — across **all** variants.

Related: [`scanner-variants.md`](scanner-variants.md) — the variants bound by
this contract and the measured dispatch decision.
