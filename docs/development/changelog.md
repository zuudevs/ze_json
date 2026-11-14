# Changelog

All notable changes to `ze_json` are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
The npm package `@zuudevs/ze-json` (`bindings/js/package.json`) and the CMake
project version (`CMakeLists.txt`) are versioned together.

## [0.3.0] - 2026-11-14

The s2-open decomposition probe (ND-13): the bracket-pairing pass of
`Document::open` becomes a shared, standalone-callable stage, exposed
through the C-ABI so a foreign harness can time the match sub-stage of
`ze_parser_open` from its own clock. Additive on top of v4 — every
existing export keeps its signature and packing.

### Added

- `ondemand::match_pass` (`include/ze_json/ondemand/document.hpp`,
  `src/ondemand/document.cpp`): the LIFO bracket-pairing pass extracted
  from `Document::open` — one source of truth, run in place by `open`
  and callable standalone. An opener pushes its index; a closer pairs
  with the TOP only on type match; crossed closers mark BOTH ends
  `kNoBracket` and the walk continues (marking, not erroring, keeps the
  pass structural-knowledge-only). Returns the paired-closer count.
  A scanner-contract guard (`indexes[j] >= src_size` → `InvalidJson`)
  never fires on scanner output and protects the foreign-facing export.
  @cite 10.14778_3115404.3115416 (Mison — staging the pairing pass over
  the flat structural index stream).
- ABI v5 export `ze_match_pass(src, src_size, indexes, index_count,
  dst_match, dst_stack, dst_cap)` (`abi/cabi.hpp`, `src/abi/cabi.cpp`):
  plain ok = paired-closer count; error = `JsonErrc << 32`;
  `index_count == 0` → plain 0; both dst buffers are caller scratch of
  `dst_cap` bytes each and must hold ≥ `index_count` entries.
- `bindings/js/ze_json.mjs`: `ZeJson.matchPass(...)` wrapper (same
  `#plain` packing as the scanner surface).
- `tests/reference_parser_test.cpp`:
  `ReferenceParser.MatchPassParityWithOpen` — the standalone
  scan + `match_pass` decomposition must reproduce the exact match
  table `Document::open` builds (index-for-index, paired-closer count
  included) over adversarial fixtures AND the whole json_checker corpus.

### Changed

- Core version `0.2.0` → `0.3.0` (`CMakeLists.txt`, `vcpkg.json`);
  `ze_abi_version()` = `2026000300` (packed core number). JS registry
  `0.2.0` → `0.3.0` (`ModuleRegistry.cmake`): the `matchPass` wrapper
  is an additive API surface (SemVer: minor bumps in the 0.x regime).
- `scripts/build-wasm.ps1`: the required-exports gate grows to 26
  symbols (v5) — a v4-era artifact (25) now fails the gate by design.

### Rationale

- wasm64 is freestanding (`-nostdlib`): no clock exists inside the
  module, so sub-stage timers in-module are impossible. The decomposition
  instead measures equivalent standalone calls (`ze_alloc` / `ze_scanner8`
  / `ze_match_pass`) from the harness's JS clock, and keeps the fused
  `ze_parser_open` as the production bound reported beside them — no
  force-subtraction of sub-stage numbers (ND-13, continuation of ND-9).

## [0.2.0] - 2026-11-14

Version-registry restructure: the core library and the JS binding now carry
two independent semver registries, and the C-ABI version number follows the
core registry automatically.

### Added

- `cmake/modules/ModuleRegistry.cmake`: `ZE_JSON_CORE_VERSION_NUMBER`
  (packed `<year><major><minor><patch>` number) and `ZE_JSON_JS_VERSION`
  (independent `bindings/js` semver) — the two version registries.
- `bindings/js/package.json` is now GENERATED at configure time from
  `cmake/templates/package.json.in`; it carries `version` (js semver) plus
  `coreVersion` (packed core number = `ze_abi_version()`) and `coreSemver`.
- `ze_json.mjs`: `JS_VERSION`, `CORE_VERSION_NUMBER`, `CORE_VERSION_SEMVER`
  exports; `ABI_VERSION` now derives from the generated `package.json`
  instead of a hardcoded constant — a wasm built from a different registry
  generation fails loud in `ZeJson.load()`.
- Generated header `ze_json/core/version.hpp` (`ZE_JSON_VERSION` plus
  MAJOR/MINOR/PATCH macros); `abi/cabi.hpp` derives `kAbiVersion` from it.

### Changed

- Core version `0.1.2` → `0.2.0` (`CMakeLists.txt`, `vcpkg.json`): the
  `ze_abi_version()` value changes from `4` to `2026000200` because its
  source moved to the core registry — the call contract stays append-only
  compatible, only the number source changed (SemVer: minor bump in the
  0.x regime).
- `ValidateVersion.py`: now checks BOTH registries (vcpkg.json == core
  semver, bindings package.json == `ZE_JSON_JS_VERSION`) and no longer
  short-circuits on the first check.

## [0.1.2] - 2026-11-12

Release-infrastructure fix; library code and documentation content of
`0.1.1` are unchanged.

### Fixed

- `CMakePresets.json`: the wasm presets referenced
  `cmake/modules/Toolchains.cmake`, a file name that does not exist in the
  repository (the tracked module is `Toolchains.cmake`). Windows resolves
  the reference case-insensitively (NTFS), Linux does not — the
  `wasm64-*` / `wasm32-*` presets could never configure on a fresh
  Linux checkout (first exercised by the `v0.1.1` publish run).
- `npm-publish.yml`: the runner now provisions the same `.toolchains`
  layout `ci.yml` uses (`$TOOLCHAINS_DIR/ninja/ninja`, llvm symlink) and
  exports `TOOLCHAINS_DIR`, since `Toolchains.cmake` resolves its
  compilers and ninja from there. Also verifies the artifact and prints
  its sha256 before the smoke test.

### Added

- `corpus/benchmarks/**` committed byte-exact (42 files, sha256-verified
  against the thesis workspace canonical tree at import) with a
  provenance README and a `corpus/** -text` `.gitattributes` guard, so
  `ctest` has its benchmark corpora on a fresh checkout (CI test #6 was
  red since the first CI run because the corpora lived only in the
  workspace).

### Version bump rationale

Per the repository versioning rule, any change to `ze_json` moves the
PATCH version; this release carries CI/build-system fixes plus the
committed corpora. The `[0.1.1]` tag exists but its publish run failed
(missing `Toolchains.cmake` on the Linux runner); `0.1.2` supersedes it
as the first published release.

### Publish verification

First successful publish to **GitHub Packages**:
`@zuudevs/ze-json@0.1.2`, verified by `workflow_dispatch` run
[43182598841](https://github.com/zuudevs/ze_json/actions/runs/43182598841)
(main HEAD `eebe953` — all 14 steps green, publish step exit 0). The
tag `v0.1.2` itself points at `9ed51a5`, whose publish run failed on
infrastructure only; the package content is identical, so no new tag
was cut for the fixes. Blocker chain that led here:

1. `v0.1.1` (run 43179466435): configure failed — preset referenced a
   non-existent `Toolchains.cmake` (case bug, NTFS-masked).
2. `v0.1.2` (run 43181252021): configure fixed; build failed —
   `CACHE_DIR` unset degraded the json_checker package dir to
   `/json_checker`.
3. Dispatch (run 43181733067): build fixed; PRE_BUILD failed —
   `PROJECT_ROOT_DIR` unset degraded the corpus path to `/corpus`.
4. Dispatch (run 43182269477): full library side green (configure,
   build, verify, smoke); publish failed `ENEEDAUTH` — the workflow
   referenced `secrets.NPM_PACKAGE_TOKEN`, the repository secret is
   `NPM_PUBLISH_TOKEN`.
5. Dispatch (run 43182598841): **green end-to-end.**

Registry lineage note: the package name was previously published to the
**public npmjs.org** registry by the same owner (versions `0.1.0`–
`0.1.6`, 2026-10-22 → 2026-10-26, predating this changelog and the
current workspace/git history). The current target registry is
**GitHub Packages** (`publishConfig.registry` in `bindings/js/package.json`);
the two registries are independent namespaces — today's `0.1.2` on
GitHub Packages is the first release of the current as-built lineage
there.

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
is created on this release's merge commit. **Publish outcome:** the run
triggered by `v0.1.1` failed at the configure step (the preset referenced a
non-existent `Toolchains.cmake` on the Linux runner — fixed in `0.1.2`);
version `0.1.1` was therefore never published to the registry.

## [0.1.0]

Initial public state: structural scanner surface (v2) with the
scanner-mode selection (`SCANNER_MODES`), the on-demand parser surface
(v3, 13 exports on a static slot pool of 64), the lazy-access layer
(L1 `lazy()`/`get`/`at`, L2 `project(shape)`), and the differential /
smoke / lazy test gates. See the git history and
`docs/architecture/` for the as-built description.
