# ze_json

> **High-performance on-demand JSON parser** — SWAR scanner + structural
> tape, zero-copy reading with no per-value allocation, arena allocator,
> and a **Raw WebAssembly memory64** binding (no Emscripten) for JavaScript.

![License](https://img.shields.io/badge/license-MIT-green)
![C++](https://img.shields.io/badge/C%2B%2B-23-blue)
![Tests](https://img.shields.io/badge/tests-58%2F58%20passing-success)
![Node](https://img.shields.io/badge/Node-24%2B%20%28memory64%29-brightgreen)

*This document is also available in [Bahasa Indonesia](README.md).*

---

## Overview

`ze_json` is built on two simple ideas:

1. **Scan once, read on demand.** A SWAR scanner validates the whole payload
   (UTF-8, strings, brackets) and **records a tape** — a list of byte offsets
   of every structural character — *without building a tree*. Values are
   parsed only when read, and strings are **never copied**: reading points
   directly into the payload memory (zero-copy); unescaping runs only when
   actually required, and the result is allocated from the arena.
2. **Every allocation comes from an arena.** `Session` + unescaped strings
   live inside a single bump allocator; the next document's cycle only needs
   a `reset_arena` — hot loops run **allocation-free** with no fragmentation.

The full pipeline:

```
payload ──scanner8/scanner64──▶ tape (structural offsets)
                                   │
              Session (arena) ─────┴─▶ find / array traversal / path query
                                   │
                        C ABI (EXPORT_NAME) ──▶ JavaScript (wasm64)
```

## Performance (measured; full data in `out/results/`)

All figures come from the development machine (`12 cores × 2,096 MHz`,
Clang LLVM 23, google-benchmark v1.9.5, Node v24.19.0) — see the complete
tables in [`out/results/benchmark/summary_*.md`](out/results/benchmark) and
[`out/results/test/summary_*.md`](out/results/test).

| Aspect | Result |
|---|---|
| Scanner throughput | **1.1–1.3 GB/s** across all datasets (61 MB corpus) |
| Scanner64 vs Scanner8 | **1.11–1.34× faster** on large files (`UpdateCenter` 1.34×, `Jeopardy` 1.17×) |
| C++ pipeline per small document | create (scan + Session) **238 ns** · find×3 + getters **411 ns** |
| Path query in C++ | **41 ns** (flat) — **208 ns** for 3 type-mixed queries |
| Wide-object search (512 fields) | first field **18 ns** vs last field **7.6 µs** (evidence of linear forward-only cost) |
| JS hot loop (demo 4) | step-by-step **0.75 µs/doc** vs path **0.92 µs/doc** for sequential top-level keys — path stays **1 wasm call** at any depth |

## Repository layout

```
ze_json/
├── include/ze_json/        Public C++ API, on-demand layer, scanner, memory
│   ├── ze_json.hpp         Session + C ABI (EXPORT_NAME) + Status/ValueType enums
│   ├── scanner/scanner.hpp scanner8 / scanner64 (SWAR, UTF-8 validation)
│   ├── ondemand/           document.hpp, json_object.hpp, json_value.hpp,
│   │                       json_iterator.hpp, json_array.hpp
│   ├── core/macros.hpp     NOEXCEPT / EXPORT_NAME / RESTRICT macros
│   └── memory/arena.hpp    bump allocator
├── src/                    implementation (scanner, ondemand, C ABI, compiler_rt stub)
├── tests/                  7 google-test groups (one group = one folder):
│                           scanner, document, object, number, session, array, path
├── benchmarks/             3 google-benchmark groups (one group = one folder):
│                           scanner (dataset), parser (Document), session (C ABI)
├── js/                     PUBLIC JavaScript API (@zuudevs/ze-json)
│   ├── ze_json.mjs         wasm64 binding + layout constants + ZeJson/Doc classes
│   └── test/main.mjs       25 cross-ABI smoke assertions
├── examples/js/example.mjs end-to-end JS demo (4 demos)
├── scripts/                metrics aggregators: test_metrics.js, benchmark_metrics.js
├── cmake/                  CMake presets (C++23, LLVM/clang, vcpkg helpers)
├── out/results/            committed metrics artifacts (JSON + Markdown)
└── dataset/                61 MB corpus — not tracked in git (distributed separately)
```

## Build

Requirements: **CMake ≥ 3.40**, a clang (LLVM) toolchain, and
**Node.js ≥ 24** for the JS path (memory64 enabled by default in Node 24+).

```bash
# Native (tests & benchmarks)
cmake --preset native-debug          # or native-release
cmake --build build/native-debug

# WebAssembly memory64 (for JavaScript)
cmake --preset wasm64-release
cmake --build build/wasm64-release
#   → build/wasm64-release/src/ze_json_core.wasm
```

## Usage — C++

```cpp
using namespace zuu;

memory::Arena* arena = create_arena(buffer, sizeof(buffer));

// High-level layer: Session — scan + step-by-step reads (root object)
types::size_t tape[512];
api::Status st = api::OK;
api::Session* s = create_session(payload, len, tape, 512, arena, st);

find_field(s, "position.x", 10);   // find_path also available on Session
types::int64_t id = get_int(s);    // cursor now sits on the number

// On-demand style layer: Document + JsonObject + array traversal
ondemand::Document doc(payload, tape, tape_len, arena);
auto found = doc.get_root().get_object().find_field({"colors", 6});
//            json_iterator::get_array / array_next / json_value::skip ...

destroy_arena(arena);
```

Cursor contracts (array drain, consuming `null`, rewind after a miss) are
documented through tests — see the comments in `tests/session_test/` and
`tests/array_test/`.

## C ABI (wasm export surface)

| Category | Exports |
|---|---|
| Arena | `create_arena`, `reset_arena`, `destroy_arena`, `get_arena_used_space` |
| Session | `create_session`, `find_field`, `destroy_session`, `value_type` |
| Getters | `get_int`, `get_double`, `get_bool`, `get_string` |
| Array | `array_size`, `array_next` |
| Path query | `find_path`, `get_int_at`, `get_double_at`, `get_bool_at`, `get_string_at` |
| Scanner | `scanner8`, `scanner64`, `scanner32` |

All status codes in `Status` (`OK`, `INVALID_JSON`, `NOT_FOUND`,
`TYPE_MISMATCH`, `INVALID_PATH`, …) are documented in
[`include/ze_json/ze_json.hpp`](include/ze_json/ze_json.hpp).

## Usage — JavaScript

```js
import { ZeJson } from "./js/ze_json.mjs";

const ze = await ZeJson.load();          // loads ze_json_core.wasm
const doc = ze.openDocument(jsonText);

doc.find("id");                    // → { found: true, type: "Number" }
doc.asInt();                       // → 21n (BigInt, 64-bit ABI)
doc.intAt("position.x");           // → { value: 24, status: "OK" }
doc.stringAt("colors[1]");         // → { value: "black", status: "OK" }

for (let i = 0; i < doc.arraySize(); i++) {
  if (doc.arrayNext()) doc.asString();   // array traversal + drain
}

ze.close();                        // release the arena at end of life
```

The npm package ([`js/package.json`](js/package.json)):

```bash
cd js
npm run build:wasm   # cmake --preset wasm64-release && build
npm test             # 25 smoke assertions across the ABI
npm pack             # @zuudevs/ze-json-0.1.0.tgz (~23 kB, wasm packed in)
```

## Testing

7 groups × 2 presets = **58 tests, all passing** (native-debug &
native-release). One test group = one folder:
`tests/{scanner,document,object,number,session,array,path}_test/`.

```bash
# Test metrics (summary + gtest JSON reports per group → out/results/test/)
node scripts/test_metrics.js native-debug native-release
```

## Benchmarks

3 groups × **28 benchmarks**: scanner (10), parser/Document (9),
session/C ABI end-to-end (9).

```bash
node scripts/benchmark_metrics.js native-release        # full run
node scripts/benchmark_metrics.js --parse-only native-release   # re-summarize only
# → out/results/benchmark/summary_<hash>.{json,md}
```

## Known limitations

- **Byte-exact keys**: `find_field`/path matching compares raw payload bytes —
  keys containing escapes (`"\u0061"`) do not match the path `"a"`
  (documented in the header).
- **1 Session = 1 object-rooted document**: a root-level array must be wrapped
  in an object, or read directly through `Document`.
- **Forward-only cursor**: cumulative `find_field` only moves forward; for
  random access use path queries (each query rewinds from the root).
- **Keys written to the region must be rewritten after `reset_arena`** —
  the payload region persists, but a fresh Session must be created again.

## Dataset

The benchmark corpus (`Tiny`, `Literal`, `GithubEvents`, `UpdateCenter`,
`Jeopardy` — ~61 MB total) is **not tracked in git**
([`.gitignore`](.gitignore)); run the benchmarks by placing the corpus files
back under `dataset/`, or adjust the dataset list in the benchmark to your
own files.

## License

[MIT](LICENSE) — © 2026 **Rafi Indra Pramudhito Zuhayr**.
Developed as the core of a thesis project (2026).
