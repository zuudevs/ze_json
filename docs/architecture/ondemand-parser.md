# The on-demand parser (as-built since P7)

`Document::open` (`src/ondemand/document.cpp`) runs `structural_scan` ONCE
per document, then builds the bracket-match table (LIFO, type-aware) in the
same linear pass — unmatched or cross-type brackets are marked `kNoBracket`
and surface as framing violations when consumed. Allocation is one arena
carve of 3 × `src_size * 8` bytes (index, match, stack upper bounds).

Access is a single forward iterator over `(token_start, cursor)` frames:

- `get_root()` eagerly frames the root (opener = first index, closer =
  last index, trailing whitespace only — the document boundary contract);
  container interiors stay unvalidated until consumed (on-demand contract).
- `Value::type()` classifies from the staged launcher byte without
  materializing; `get_string_span` / `get_double` / `get_bool` promote
  lazily through the P7-3 / P7-2 scalar paths (V8-locked semantics,
  lone-surrogate escapes pass through verbatim).
- `begin_array()` / `begin_object()` return `ArrayIter` / `ObjectIter`
  (forward-only, sticky errors); `count_elements()` walks the structural
  index without materializing children.
- Re-entry from a returned frame is O(1) in the common case and falls
  back to `first_index_at_or_after` (gallop, O(log distance)) when a
  stale cursor-less position must be re-derived (O1-8).

Correctness gates: 75 gtest `TEST`s across 6 targets (differential vs
`reference_scan`, json_checker corpus, V8-parity scalar edges, skip
semantics `SkippedSubtreeStaysUnvalidated` / `DeepSkipIsIterative`).
Benchmarks: `benchmarks/parser_benchmark.cpp` phases `BM_ParseReference`
/ `BM_ParseOpen` / `BM_ParseOnDemandWalkOnly` / `BM_ParseOnDemandFullWalk`
(full-consumption phases; a selective-access phase is future work — see
the workspace audit `docs/research/audit-2026-11-12.md`).

@cite 10.1002_spe.3313 on-demand JSON — the single forward iterator this
layer replays token by token. @cite 10.14778_3115404.3115416 Mison — the
structural index that backs cursor re-entry without a materialized DOM.

Related: [`js-lazy-access.md`](js-lazy-access.md) — the JS-side lazy layer
built on this parser through the ABI; [`wasm-abi.md`](wasm-abi.md) — the
export surface it is driven through.
