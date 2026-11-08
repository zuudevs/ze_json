# Requirements (skeleton — full content in P6)

## Toolchain (portable, under `.toolchains/` — provision once via `scripts/bootstraps/ze_json.bat`)

| Component | Version | Notes |
|---|---|---|
| CMake | 4.4.5 | presets hardcode the `.toolchains` location |
| Ninja | 1.13.2 | generator for all presets |
| Clang/LLVM | 23.1.5 | C++23, also provides clang-tidy / clang-format |
| vcpkg | 2026.10.16 | manifest mode: `gtest`, `benchmark` (`vcpkg.json`) |
| MSVC BuildTools | 14.4x | link env for clang on Windows; ASan x64 optional component for `native-sanitize` |
| Python | 3.x | build-time codegen + tooling; runtime deps: `urllib3` only (`ze_json/scripts/requirements.txt`) |
| Node.js | >= 24 | wasm64 binding + node harness (P2/P3) |

## Environment variables (`scripts/env/activate.ps1` / `.bat`)

| Variable | Purpose |
|---|---|
| `PROJECT_ROOT_DIR` | workspace root; anchors corpus paths at configure time |
| `TOOLCHAINS_DIR` | portable toolchain root (CMake, Ninja, Clang, vcpkg) |
| `CORPUS_DIR` | corpus root (`corpus/`) |
| `OUT_DIR` | export root (`out/`) for tests/benchmarks helpers |
| `VCPKG_VISUAL_STUDIO_PATH` | MSVC instance used by vcpkg; fallback to system BuildTools if `.toolchains/MSVC` is absent |

Without activation, `OUT_DIR` is empty and export targets write to invalid paths.

## Corpus

| Corpus | Source | Used by |
|---|---|---|
| `corpus/test/json_checker` | json.org suite; auto-installed by `InstallJsonChecker.py` (cached in `.cache/json_checker`) | scanner tests (P1-4) |
| `corpus/benchmarks/*` | deterministic generator `scripts/corpus/make_corpus.py`, manifest sha256 (P0-8) | scanner benchmarks (P1-5/P1-6) |

Corpus bytes are hash-pinned (`corpus/** -text` in `.gitattributes`); never
edit them by hand — regenerate with the generator and review the new manifest.

## Presets

`native-{debug,release,release-with-debug-info,sanitize,pgo}`,
`wasm32-{debug,release}`, `wasm64-{debug,release}` — see
`ze_json/CMakePresets.json`.

TODO(P6): pin exact bootstrap steps per OS, add CI parity notes
(`.github/workflows/ci.yml`), and document optional ASan component install.
