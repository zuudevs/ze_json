# ADR 0003 — ABI v3 parser surface on a static slot pool

- **Status:** Accepted
- **Date:** phase P7 (2026-11); recorded 2026-11-12
- **Deciders:** library owner

## Context

The v2 wasm surface exposed only the scanner family. Exposing the
on-demand parser (see
[`../architecture/ondemand-parser.md`](../architecture/ondemand-parser.md))
to Node.js across a raw C ABI has hard constraints:

- freestanding module (no imports, `--no-entry`), fixed 64 MiB memory64
  with an exported memory;
- the bump `Arena` design has **no individual free** — allocation rewinds
  wholesale between batches (`ze_reset_heap`);
- traversal must not smuggle C++ iterator objects or internal pointers
  across the ABI boundary.

Alternatives considered:

- *Return raw arena/iterator pointers to JS* — leaks internal layout and
  lifetime into the binding; unsafe.
- *Single global parser* — cannot hold several documents in flight for
  batch workloads (the measurement harness and the SAQ repository clone
  both need one parser per document).
- *Heap-allocated context per open with an explicit free* — contradicts
  the bump-arena design and adds an ownership protocol for one pointer.

## Decision

**v3 = scanner surface + a 13-export parser surface operating on a static
slot pool of 64 (`kParserSlotMax`)**:

- `ze_parser_open(src, len)` claims one slot (one `Arena` + one
  `Document`), `ze_parser_root(slot)` re-derives the root frame
  (cursor = 0 contract), `ze_parser_close(slot)` releases the slot.
- Traversal is **stateless per call**: the `(cursor, token_start)` frame
  pair a step export returns is fed back as full-width u64 arguments to
  the next call (`ze_value_kind`, `ze_container_begin`, `ze_array_next`,
  `ze_object_next`, `ze_object_field`, `ze_get_string`, `ze_get_number`,
  `ze_get_bool`, `ze_is_null`, `ze_count_elements`). No framing logic is
  duplicated on the ABI side — the ABI rebuilds the real C++ iterator
  from the frame.
- Result packing: `hi === 0` → ok (payload in the low half);
  `hi === kStepSentinel` → container exhausted; `hi !== 0 && lo === 0` →
  error (`core::JsonErrc` code in `hi`).
- `ze_reset_heap()` closes every parser slot first, then rewinds the
  bump allocator.

## Consequences

- The JS binding (`bindings/js/ze_json.mjs`) mirrors the contract header
  (`include/ze_json/abi/cabi.hpp` — single source of truth) and gates
  module/binding versions through `ze_abi_version`.
- Pool exhaustion (64 concurrent documents) fails loud as a
  `ZeParserError`; the documented lifetime is open → traverse →
  `close()` per document, `resetHeap()` between batches.
- The contract is frozen by the `@cite`-annotated header; any change
  bumps `kAbiVersion` and updates the binding + tests together
  (see [`development/README.md`](../development/README.md)).
