# Requirements

Exact versions and environment for building, testing, and measuring
`ze_json` from this workspace. All toolchain components live under
`<workspace>/.toolchains/` and are provisioned once by
`scripts/bootstraps/ze_json.bat` (large download — do not repeat).

## 1. Toolchain

| Component | Version | Used for |
|---|---|---|
| CMake | 4.4.5 | presets (`CMakePresets.json`) hardcode the `.toolchains` layout |
| Ninja | 1.13.2 | generator for every preset |
| Clang/LLVM | 23.1.5 | C++23 compiler; clang-tidy (native builds) and clang-format |
| vcpkg | 2026.10.16 | manifest mode — deps: `gtest` (tests), `benchmark` (native benchmarks) |
| MSVC BuildTools | 14.4x (`14.44.35207` in use) | MSVC STL + link env for clang; optional "C++ AddressSanitizer" component for `native-sanitize` |
| Python | 3.14 (any ≥3.10 works) | codegen (`GenerateLookups.py`), corpus generator, validator/converter/analyzer; only dependency: `urllib3` (JSON_checker installer) |
| Node.js | ≥ 24 (24.19.0 in use) | host introspection, node harness (`v8`, `ze-wasm64` engines), binding tests — memory64 requires ≥ 24 |

Language standards: C++23 (`std=c++23`), freestanding for wasm64
(`-nostdlib --no-entry`, memory64, exported memory).

## 2. Environment variables

Set by `scripts/env/activate.ps1` / `.bat` — required before cmake/vcpkg in
non-interactive sessions:

| Variable | Value | Consumer |
|---|---|---|
| `PROJECT_ROOT_DIR` | workspace root | corpus paths at configure time (`JSON_CHECKER_CORPUS_DIR`), benchmarks corpus dir |
| `TOOLCHAINS_DIR` | `<root>/.toolchains` | compilers, CMake, Ninja, vcpkg |
| `CORPUS_DIR` | `<root>/corpus` | test corpus resolution |
| `OUT_DIR` | `<root>/out` | export targets (tests/benchmarks sidecars) |
| `VCPKG_VISUAL_STUDIO_PATH` | MSVC instance root | vcpkg triplet acquisition; **fallback**: point at a system BuildTools install when `.toolchains/MSVC` is absent |

VS Code profile "PowerShell (ze_json)" loads these automatically.

## 3. Presets

| Preset | Notes |
|---|---|
| `native-{debug,release,release-with-debug-info}` | debug is coverage-instrumented — never a numeric basis |
| `native-sanitize` | ASan/UBSan, correctness only; x64 link needs the MSVC ASan optional component |
| `native-pgo` | GENERATE → run benchmark → `ninja pgo_merge` → rebuild (USE); owner decision |
| `wasm32-{debug,release}` | 32-bit wasm (scanner32 currently a stub) |
| `wasm64-{debug,release}` | the shipped artifact: memory64, exported memory, 64 MiB initial |

## 4. Corpus requirements

| Corpus | Source | Integrity |
|---|---|---|
| `corpus/test/json_checker` | json.org suite; auto-installed by `InstallJsonChecker.py` (PRE_BUILD, cached in `.cache/json_checker`) | 33 fail + 3 pass; legacy RFC 8259 exceptions documented in the tests |
| `corpus/benchmarks/synthetic_{1k,10k,100k,1mb}` | generator `scripts/corpus/make_corpus.py` (seed `20261108`, byte-exact ladder) | `MANIFEST.json` sha256 per corpus |
| `corpus/benchmarks/saq_payload_v1` | anonymized SAQ store shapes, pretty indent-2 | idem |
| `corpus/benchmarks/checker_batch` | pass1–3 byte-exact copies + 22 valid edge documents | idem, `copied:` provenance re-verified by `verify --all` |

Corpus bytes are hash-pinned (`corpus/** -text` in `.gitattributes`) so
`core.autocrlf=true` cannot silently invalidate manifests. Never edit corpus
files by hand — regenerate and review the new manifests.

## 5. CI parity

Reference workflow: `.github/workflows/ci.yml` (ubuntu + windows,
`native-debug`/`native-release`, env `CORPUS_DIR`/`TOOLCHAINS_DIR`/
`CACHE_DIR`/`PROJECT_ROOT_DIR`). The publish workflow
(`npm-publish.yml`) builds `wasm64-release` and runs the binding smoke test
before `npm publish` to GitHub Packages.

TODO(P6+): pin CI runner software versions in a table once the workflows are
exercised end-to-end on a tag push.
