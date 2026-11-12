# Changelog

All notable changes to `ze_json` are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
The npm package `@zuudevs/ze-json` (`bindings/js/package.json`) and the CMake
project version (`CMakeLists.txt`) are versioned together.

## [0.1.1] - 2026-11-12

Documentation-only release; parser, scanner and C-ABI behavior are unchanged.

### Added

- Modular documentation architecture under `docs/`:
  `architecture/` (split into per-topic kebab-case files), `requirements/`
  (renamed from `requirement/`), `development/`, and `adr/`.
- Initial ADR set (cross-repo numbering with the thesis workspace):
  [0002](docs/adr/0002-scanner64-swar-rejected.md) — scanner64 SWAR as
  production default rejected; [0003](docs/adr/0003-abi-v3-slot-pool.md) —
  ABI v3 parser surface on a static slot pool.
- Package-page README for the npm binding (`bindings/js/README.md`) and a
  repository root README.

### Changed

- Root README rewritten as the repository landing page.
- `docs/` indexes updated to the as-built on-demand parser / ABI v3 state,
  with workspace cross-references migrated to the post-overhaul paths
  (`docs/project/tasks.md`, `docs/research/audit-2026-11-12.md`).

### Version bump rationale

No API or behavior change occurred in this release, so the version moves by
PATCH (`0.1.0` -> `0.1.1`) to publish the documentation overhaul and keep the
npm package and CMake project version in lockstep. The npm publish workflow
(`.github/workflows/npm-publish.yml`) triggers on `v*` tags; the `v0.1.1` tag
is created on this release's merge commit.

## [0.1.0]

Initial public state: structural scanner surface (v2) with the
scanner-mode selection (`SCANNER_MODES`), the on-demand parser surface
(v3, 13 exports on a static slot pool of 64), the lazy-access layer
(L1 `lazy()`/`get`/`at`, L2 `project(shape)`), and the differential /
smoke / lazy test gates. See the git history and
`docs/architecture/` for the as-built description.
