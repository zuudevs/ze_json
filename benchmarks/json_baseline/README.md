# SAQ Report JSON.parse baseline

This benchmark measures JavaScript `JSON.parse(rawPayload)` over six serialized values exposed by the six SAQ Report repositories. The benchmark harness belongs to the `ze_json` repository; the application data source is a separate `saq-report` checkout.

## Repository layout

```text
ze_json/
├── benchmarks/json_baseline/
│   ├── capture-samples.js
│   ├── feeder.js
│   ├── run-baseline.js
│   ├── server.js
│   └── README.md
├── samples/saq-report/
│   ├── assistants.json
│   ├── courses.json
│   ├── lecturers.json
│   ├── schedules.json
│   ├── reports.json
│   ├── globalConfig.json
│   └── manifest.json
└── scripts/saq-dummy-generator.js
```

## Prepare a dummy snapshot

Run from the `ze_json` repository root. Set `SAQ_REPORT_ROOT` to the root of a local `saq-report` checkout because the generator imports the original `routes/_helpers.js` implementation of `generateId()`.

```powershell
$env:SAQ_REPORT_ROOT = "D:\Projects\saq-report"
node .\scripts\saq-dummy-generator.js
```

This writes exactly six compact JSON payload files and `samples/saq-report/manifest.json`. The five repository arrays contain one linked dummy record each; `globalConfig` is an object. The manifest marks the snapshot as `synthetic_dummy`.

Dummy data is for integration testing and parser characterization only. It is not a representative sample of production SAQ Report data.

## Capture a real application snapshot

Set the same `SAQ_REPORT_ROOT` to the local `saq-report` checkout. Make sure its `.env` and MongoDB configuration are valid.

```powershell
$env:SAQ_REPORT_ROOT = "D:\Projects\saq-report"
node .\benchmarks\json_baseline\capture-samples.js
```

The capture script calls `connectDB()`, `initRepositories()`, and each repository's `read()` method. It does not write to the database. The sample files are written as compact `JSON.stringify(value)` UTF-8 bytes, without an added newline. The manifest records each payload's SHA-256, byte length, root type, and structural profile, as well as the source `saq-report` Git revision.

Real snapshots may contain personal or operational data. Keep them local and out of public Git history. The generated `samples/saq-report/` directory should be ignored or otherwise protected according to the project's data-handling rules.

## Run through ze_json_research

The research runner uses script mode and invokes `run-baseline.js`. That wrapper starts the HTTP receiver, waits for its health check, executes the feeder, verifies finalization, and shuts down the receiver. The runner's external run directory is passed to the receiver, so the result artifacts are collected by the outer research manifest.

Set the research data root outside both Git repositories:

```powershell
$env:ZE_JSON_SOURCE_DIR = (Get-Location).Path
$env:ZE_JSON_RESEARCH_HOME = "D:\ResearchData\ze_json_research"
$env:SAQ_BENCH_WARMUP_ITERATIONS = "100"
$env:SAQ_BENCH_ITERATIONS = "200"
$env:SAQ_BENCH_ROUNDS = "20"
```

The experiment registry must use these execution settings:

```yaml
execution:
  mode: script
  require_clean_worktree: true
  run_command:
    - node
    - benchmarks/json_baseline/run-baseline.js
    - --run-dir
    - "{run_dir}"
    - --run-id
    - "{run_id}"
  expected_outputs:
    - baseline-server/manifest.json
    - raw/benchmark_results.jsonl
    - metadata/saq-report-sample-manifest.json
    - inputs/saq-report/manifest.json
    - inputs/saq-report/assistants.json
    - inputs/saq-report/courses.json
    - inputs/saq-report/lecturers.json
    - inputs/saq-report/schedules.json
    - inputs/saq-report/reports.json
    - inputs/saq-report/globalConfig.json
```

The registry definition must still be reviewed and approved before a formal run. A dummy snapshot should not be treated as a representative application workload.

## Direct integration smoke test

Without using the formal research CLI, the wrapper can be exercised against the current dummy snapshot:

```powershell
$env:SAQ_BENCH_WARMUP_ITERATIONS = "5"
$env:SAQ_BENCH_ITERATIONS = "20"
$env:SAQ_BENCH_ROUNDS = "3"
node .\benchmarks\json_baseline\run-baseline.js `
  --run-dir "$env:TEMP\saq-baseline-smoke" `
  --run-id "saq-baseline-smoke"
```

Expected outputs:

```text
%TEMP%/saq-baseline-smoke/
├── baseline-server/manifest.json
├── raw/benchmark_results.jsonl
├── inputs/saq-report/
│   ├── manifest.json
│   └── six JSON payload files
└── metadata/saq-report-sample-manifest.json
```

## Measurement semantics

- Operation: `JSON.parse(rawPayload)` only.
- Timed region: repeated `JSON.parse` calls inside each measured batch.
- Excluded: HTTP transfer and body collection, SHA-256 verification, correctness pre-check, structural profiling, warmup, result serialization, and file writes.
- Each `sample_us_per_op` is the elapsed time for a batch divided by its iteration count. It is not a separately timestamped single-call latency.
- Median and mean are in microseconds per operation. Throughput is computed from mean time and UTF-8 payload bytes.
- `sample_count` is the number of timed batches per payload.

This measures the JavaScript baseline only. It does not measure `ze_json`, full `cloneJson()` execution, database loading, or network access to MongoDB.
