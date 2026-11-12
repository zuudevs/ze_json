# ze_json measurement corpora

Benchmark corpora used by `benchmarks/` and the gtest suite
(`ReferenceParser.BenchmarkCorpusVerdictEquality` and the scanner
differential gates). These files are **byte-frozen**: each corpus
directory carries a deterministic, timestamp-free `MANIFEST.json` whose
sha256 is recorded in every measurement record (`corpus.sha256`).

## Provenance

- Canonical generator: `scripts/corpus/make_corpus.py` in the thesis
  workspace (one level above this repository), seed `20261108`.
- Canonical live tree: thesis workspace `corpus/benchmarks/`.
- The copies committed here are **byte-exact** (sha256-verified at
  import time, 2026-11-12) so CI — where the workspace tree is not
  available — runs the same documents: synthetic size ladder
  (`synthetic_1k` … `synthetic_1mb`), SAQ store-shaped workloads
  (`saq_payload_v1`, anonymized), and the many-small-documents batch
  (`checker_batch`, whose `pass*.json` copies originate from the
  json.org JSON_checker suite, auto-installed to
  `corpus/test/json_checker` at build time).

Corpus bytes are protected from endline conversion by `.gitattributes`
(`corpus/** -text`) — regeneration must go through the generator in the
thesis workspace, never hand edits.
