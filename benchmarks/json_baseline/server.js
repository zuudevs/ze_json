'use strict';

/**
 * Local JSON.parse baseline receiver for saq-report.
 *
 * The HTTP body is the raw JSON payload itself. The timed region excludes
 * networking, body collection, SHA-256 verification, JSON validity checking,
 * and structural profiling.
 *
 * Node.js >= 18 is required only for the feeder's built-in fetch().
 */

const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const fs = require('node:fs');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');

const APP_ROOT = path.resolve(__dirname, '../..'); // Root of the ze_json source repository.
const DEFAULT_DATA_DIR = path.join(os.homedir(), 'ResearchData', 'saq-report-json-baseline');
const DATA_DIR = path.resolve(process.env.SAQ_BENCH_DATA_DIR || DEFAULT_DATA_DIR);
const HOST = process.env.SAQ_BENCH_HOST || '127.0.0.1';
const PORT = positiveInteger(process.env.SAQ_BENCH_PORT, 8787);
const WARMUP_ITERATIONS = positiveInteger(process.env.SAQ_BENCH_WARMUP_ITERATIONS, 100);
const ITERATIONS_PER_SAMPLE = positiveInteger(process.env.SAQ_BENCH_ITERATIONS, 200);
const SAMPLE_ROUNDS = positiveInteger(process.env.SAQ_BENCH_ROUNDS, 20);
const MAX_PAYLOAD_BYTES = positiveInteger(process.env.SAQ_BENCH_MAX_PAYLOAD_BYTES, 64 * 1024 * 1024);

const generatedRunId = `${utcCompact()}-${crypto.randomBytes(4).toString('hex')}`;
const runId = process.env.SAQ_BENCH_RUN_ID || generatedRunId;
if (!/^[a-zA-Z0-9][a-zA-Z0-9_.:-]{0,127}$/.test(runId)) {
    throw new Error(`Invalid SAQ_BENCH_RUN_ID: ${runId}`);
}
const requestedRunDir = process.env.SAQ_BENCH_RUN_DIR;
const runDir = requestedRunDir
    ? path.resolve(requestedRunDir)
    : path.join(DATA_DIR, 'runs', runId);
const relativeRunDir = path.relative(DATA_DIR, runDir);
if (relativeRunDir === '..' || relativeRunDir.startsWith(`..${path.sep}`) || path.isAbsolute(relativeRunDir)) {
    throw new Error(`SAQ_BENCH_RUN_DIR must be inside SAQ_BENCH_DATA_DIR: ${runDir}`);
}
const shutdownToken = process.env.SAQ_BENCH_SHUTDOWN_TOKEN || null;
const resultsPath = path.join(runDir, 'raw', 'benchmark_results.jsonl');
const manifestPath = path.join(runDir, 'baseline-server', 'manifest.json');

let finished = false;
let payloadsReceived = 0;
let errorCount = 0;

function positiveInteger(value, fallback) {
    if (value === undefined || value === '') return fallback;
    const parsed = Number.parseInt(value, 10);
    if (!Number.isSafeInteger(parsed) || parsed < 1) {
        throw new Error(`Expected positive integer, received: ${value}`);
    }
    return parsed;
}

function utcCompact() {
    return new Date().toISOString().replace(/[-:.]/g, '').replace('T', 'T').replace('Z', 'Z');
}

function sha256(buffer) {
    return crypto.createHash('sha256').update(buffer).digest('hex');
}

function median(values) {
    const sorted = [...values].sort((a, b) => a - b);
    const middle = Math.floor(sorted.length / 2);
    return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

function summarize(values) {
    const mean = values.reduce((sum, value) => sum + value, 0) / values.length;
    const variance = values.length > 1
        ? values.reduce((sum, value) => sum + ((value - mean) ** 2), 0) / (values.length - 1)
        : 0;
    const standardDeviation = Math.sqrt(variance);
    return {
        min: Math.min(...values),
        median: median(values),
        mean,
        standard_deviation: standardDeviation,
        max: Math.max(...values),
        coefficient_of_variation_percent: mean === 0 ? null : (standardDeviation / mean) * 100,
    };
}

function profileValue(value) {
    const counts = {
        object_count: 0,
        array_count: 0,
        string_count: 0,
        number_count: 0,
        boolean_count: 0,
        null_count: 0,
        max_nesting_depth: 0,
    };

    function visit(current, depth) {
        if (depth > counts.max_nesting_depth) counts.max_nesting_depth = depth;

        if (current === null) {
            counts.null_count += 1;
        } else if (Array.isArray(current)) {
            counts.array_count += 1;
            for (const item of current) visit(item, depth + 1);
        } else if (typeof current === 'object') {
            counts.object_count += 1;
            for (const item of Object.values(current)) visit(item, depth + 1);
        } else if (typeof current === 'string') {
            counts.string_count += 1;
        } else if (typeof current === 'number') {
            counts.number_count += 1;
        } else if (typeof current === 'boolean') {
            counts.boolean_count += 1;
        }
    }

    visit(value, 0);

    const rootType = value === null ? 'null' : (Array.isArray(value) ? 'array' : typeof value);
    const rootItemCount = Array.isArray(value)
        ? value.length
        : (value !== null && typeof value === 'object' ? Object.keys(value).length : null);

    return { root_type: rootType, root_item_count: rootItemCount, ...counts };
}

function getGitProvenance() {
    function git(args) {
        return execFileSync('git', args, {
            cwd: APP_ROOT,
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'ignore'],
        }).trim();
    }

    try {
        const status = git(['status', '--porcelain=v1', '--untracked-files=all']);
        const trackedDiff = git(['diff', '--binary', 'HEAD', '--']);
        const untrackedHashes = {};

        for (const line of status.split(/\r?\n/).filter(Boolean)) {
            if (!line.startsWith('?? ')) continue;
            const relative = line.slice(3);
            const absolute = path.resolve(APP_ROOT, relative);
            if (!absolute.startsWith(APP_ROOT + path.sep) || !fs.existsSync(absolute)) continue;
            if (!fs.statSync(absolute).isFile()) continue;
            untrackedHashes[relative] = sha256(fs.readFileSync(absolute));
        }

        return {
            repository_root: APP_ROOT,
            repository_url: 'https://github.com/zuudevs/ze_json',
            git_commit: git(['rev-parse', 'HEAD']),
            branch: git(['branch', '--show-current']) || null,
            worktree_clean: status.length === 0,
            status_porcelain: status,
            tracked_diff_sha256: trackedDiff ? sha256(Buffer.from(trackedDiff, 'utf8')) : null,
            untracked_file_sha256: untrackedHashes,
        };
    } catch (error) {
        return {
            repository_root: APP_ROOT,
            repository_url: 'https://github.com/zuudevs/ze_json',
            git_commit: null,
            branch: null,
            worktree_clean: null,
            provenance_error: error.message,
        };
    }
}

function writeJsonAtomic(filePath, value) {
    fs.mkdirSync(path.dirname(filePath), { recursive: true });
    const temporaryPath = `${filePath}.tmp`;
    fs.writeFileSync(temporaryPath, `${JSON.stringify(value, null, 2)}\n`, 'utf8');
    fs.renameSync(temporaryPath, filePath);
}

function writeJsonl(filePath, value) {
    fs.mkdirSync(path.dirname(filePath), { recursive: true });
    fs.appendFileSync(filePath, `${JSON.stringify(value)}\n`, 'utf8');
}

function artifactRecord(filePath, relativePath, kind) {
    if (!fs.existsSync(filePath)) return null;
    const bytes = fs.readFileSync(filePath);
    return {
        path: relativePath,
        kind,
        sha256: sha256(bytes),
        size_bytes: bytes.length,
    };
}

function makeInitialManifest() {
    return {
        manifest_schema_version: '1.0',
        experiment_id: 'saq-report-json-parse-baseline',
        run_id: runId,
        started_at: new Date().toISOString(),
        finished_at: null,
        status: 'RUNNING',
        verification_status: 'PENDING',
        source: getGitProvenance(),
        environment: {
            platform: process.platform,
            os_release: os.release(),
            architecture: process.arch,
            cpu_model: os.cpus()[0]?.model || null,
            logical_cpu_count: os.cpus().length,
            node_version: process.version,
            v8_version: process.versions.v8,
        },
        benchmark: {
            parser: 'JSON.parse',
            operation: 'parse_only',
            measurement_contract: 'json-parse-only-v1',
            timed_region: 'JSON.parse(rawPayload) calls only; aggregate batch timing',
            excluded_from_timing: [
                'HTTP transfer and request-body collection',
                'SHA-256 computation and verification',
                'JSON validity pre-check',
                'payload structural profiling',
                'warmup iterations',
                'result serialization and file writes',
            ],
            warmup_iterations: WARMUP_ITERATIONS,
            iterations_per_sample: ITERATIONS_PER_SAMPLE,
            sample_rounds: SAMPLE_ROUNDS,
        },
        payloads_received: 0,
        error_count: 0,
        workload_snapshot: { snapshot_id: null, sample_manifest_sha256: null, expected_payload_count: null },
        expected_outputs: ['raw/benchmark_results.jsonl'],
        artifacts: [],
        notes: [
            'A completed or mechanically verified run does not prove a scientific hypothesis.',
            'Each sample point is the mean time per operation over one batch, not a single-call latency.',
            'Payload files must be captured from the actual saq-report repositories for an application-data baseline.',
        ],
    };
}

function readRequestBody(request) {
    return new Promise((resolve, reject) => {
        const chunks = [];
        let totalBytes = 0;
        request.on('data', (chunk) => {
            totalBytes += chunk.length;
            if (totalBytes > MAX_PAYLOAD_BYTES) {
                reject(Object.assign(new Error(`Payload exceeds limit (${MAX_PAYLOAD_BYTES} bytes).`), { statusCode: 413 }));
                request.destroy();
                return;
            }
            chunks.push(chunk);
        });
        request.on('end', () => resolve(Buffer.concat(chunks)));
        request.on('error', reject);
    });
}

function sendJson(response, statusCode, value) {
    const body = Buffer.from(`${JSON.stringify(value, null, 2)}\n`, 'utf8');
    response.writeHead(statusCode, {
        'content-type': 'application/json; charset=utf-8',
        'content-length': body.length,
        'cache-control': 'no-store',
    });
    response.end(body);
}

function runParseBenchmark(rawPayload, payloadBytes) {
    let sink;

    // Warm up the same parser/input before collecting timed samples.
    for (let index = 0; index < WARMUP_ITERATIONS; index += 1) {
        sink = JSON.parse(rawPayload);
    }

    const perRoundUsPerOp = [];
    for (let round = 0; round < SAMPLE_ROUNDS; round += 1) {
        const start = process.hrtime.bigint();
        for (let iteration = 0; iteration < ITERATIONS_PER_SAMPLE; iteration += 1) {
            sink = JSON.parse(rawPayload);
        }
        const elapsedNs = Number(process.hrtime.bigint() - start);
        perRoundUsPerOp.push((elapsedNs / ITERATIONS_PER_SAMPLE) / 1000);
    }

    // Retain the final parse result until after timing and prove a result was produced.
    if (typeof sink === 'undefined') throw new Error('Parser produced undefined unexpectedly.');

    const stats = summarize(perRoundUsPerOp);
    const opsPerSecond = stats.mean > 0 ? 1_000_000 / stats.mean : null;
    const bytesPerSecond = stats.mean > 0 ? (payloadBytes * 1_000_000) / stats.mean : null;

    return {
        min_us_per_op: stats.min,
        median_us_per_op: stats.median,
        mean_us_per_op: stats.mean,
        standard_deviation_us_per_op: stats.standard_deviation,
        max_us_per_op: stats.max,
        coefficient_of_variation_percent: stats.coefficient_of_variation_percent,
        ops_per_second: opsPerSecond,
        bytes_per_second: bytesPerSecond,
        mib_per_second: bytesPerSecond === null ? null : bytesPerSecond / (1024 * 1024),
        sample_count: perRoundUsPerOp.length,
        iterations_per_sample: ITERATIONS_PER_SAMPLE,
        warmup_iterations: WARMUP_ITERATIONS,
        sample_us_per_op: perRoundUsPerOp,
    };
}

function finalizeManifest(manifest, status) {
    manifest.finished_at = new Date().toISOString();
    manifest.status = status;
    manifest.payloads_received = payloadsReceived;
    manifest.error_count = errorCount;
    manifest.artifacts = [artifactRecord(resultsPath, 'raw/benchmark_results.jsonl', 'raw_result')].filter(Boolean);
    manifest.verification_status = 'PENDING';
    writeJsonAtomic(manifestPath, manifest);

    // Verify the recorded result artifact mechanically after writing the manifest.
    let artifactChecksPassed = manifest.artifacts.length === 1;
    for (const artifact of manifest.artifacts) {
        const absolutePath = path.resolve(runDir, artifact.path);
        const relative = path.relative(runDir, absolutePath);
        if (relative.startsWith('..') || path.isAbsolute(relative) || !fs.existsSync(absolutePath)) {
            artifactChecksPassed = false;
            continue;
        }
        const bytes = fs.readFileSync(absolutePath);
        if (bytes.length !== artifact.size_bytes || sha256(bytes) !== artifact.sha256) {
            artifactChecksPassed = false;
        }
    }
    manifest.verification_status = status === 'COMPLETED' && artifactChecksPassed ? 'VERIFIED' : 'FAILED';
    writeJsonAtomic(manifestPath, manifest);
}

fs.mkdirSync(path.dirname(resultsPath), { recursive: true });
const manifest = makeInitialManifest();
writeJsonAtomic(manifestPath, manifest);

const server = http.createServer(async (request, response) => {
    const url = new URL(request.url || '/', `http://${request.headers.host || `${HOST}:${PORT}`}`);

    if (request.method === 'GET' && url.pathname === '/healthz') {
        return sendJson(response, 200, {
            status: finished ? 'FINISHED' : 'READY',
            run_id: runId,
            received: payloadsReceived,
            output_dir: runDir,
            benchmark: manifest.benchmark,
        });
    }

    if (request.method === 'POST' && url.pathname === '/api/benchmark') {
        if (finished) return sendJson(response, 409, { error: 'Run sudah difinalisasi.' });

        const payloadId = String(request.headers['x-payload-id'] || '');
        const declaredHash = String(request.headers['x-payload-sha256'] || '').toLowerCase();
        const snapshotId = String(request.headers['x-snapshot-id'] || '');
        const sampleManifestHash = String(request.headers['x-sample-manifest-sha256'] || '').toLowerCase();
        if (!/^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,99}$/.test(payloadId)) {
            return sendJson(response, 400, { error: 'Header X-Payload-Id tidak valid.' });
        }
        if (!/^[a-f0-9]{64}$/.test(declaredHash)) {
            return sendJson(response, 400, { error: 'Header X-Payload-SHA256 harus berisi SHA-256 hex.' });
        }
        if (!/^[a-zA-Z0-9][a-zA-Z0-9_.:-]{0,127}$/.test(snapshotId)) {
            return sendJson(response, 400, { error: 'Header X-Snapshot-Id tidak valid.' });
        }
        if (!/^[a-f0-9]{64}$/.test(sampleManifestHash)) {
            return sendJson(response, 400, { error: 'Header X-Sample-Manifest-SHA256 harus berisi SHA-256 hex.' });
        }
        if (manifest.workload_snapshot.snapshot_id && manifest.workload_snapshot.snapshot_id !== snapshotId) {
            return sendJson(response, 409, { error: 'Snapshot ID berbeda dalam satu run.' });
        }
        if (manifest.workload_snapshot.sample_manifest_sha256 && manifest.workload_snapshot.sample_manifest_sha256 !== sampleManifestHash) {
            return sendJson(response, 409, { error: 'Sample manifest hash berbeda dalam satu run.' });
        }
        manifest.workload_snapshot.snapshot_id = snapshotId;
        manifest.workload_snapshot.sample_manifest_sha256 = sampleManifestHash;

        let body;
        try {
            body = await readRequestBody(request);
        } catch (error) {
            errorCount += 1;
            return sendJson(response, error.statusCode || 400, { error: error.message });
        }

        const actualHash = sha256(body);
        const baseResult = {
            schema_version: 1,
            experiment_id: 'saq-report-json-parse-baseline',
            run_id: runId,
            timestamp_utc: new Date().toISOString(),
            payload_id: payloadId,
            snapshot_id: snapshotId,
            sample_manifest_sha256: sampleManifestHash,
            payload_sha256: actualHash,
            payload_bytes_utf8: body.length,
            payload_chars_utf16: null,
            parser: 'JSON.parse',
            operation: 'parse_only',
            measurement_contract_id: 'json-parse-only-v1',
        };

        if (!body.equals(Buffer.from(body.toString('utf8'), 'utf8'))) {
            errorCount += 1;
            const record = { ...baseResult, result_status: 'error', error: 'Payload bukan UTF-8 valid.' };
            writeJsonl(resultsPath, record);
            return sendJson(response, 400, record);
        }
        if (actualHash !== declaredHash) {
            errorCount += 1;
            const record = { ...baseResult, result_status: 'error', error: 'SHA-256 payload tidak cocok.' };
            writeJsonl(resultsPath, record);
            return sendJson(response, 409, record);
        }

        const rawPayload = body.toString('utf8');
        baseResult.payload_chars_utf16 = rawPayload.length;

        let parsedValue;
        try {
            // Correctness check is outside the timed region.
            parsedValue = JSON.parse(rawPayload);
        } catch (error) {
            errorCount += 1;
            const record = { ...baseResult, result_status: 'error', error: `JSON tidak valid: ${error.message}` };
            writeJsonl(resultsPath, record);
            return sendJson(response, 422, record);
        }

        try {
            const profile = profileValue(parsedValue);
            const perf = runParseBenchmark(rawPayload, body.length);
            const record = {
                ...baseResult,
                ...profile,
                ...perf,
                result_status: 'ok',
                error_count: 0,
                environment: {
                    node_version: process.version,
                    v8_version: process.versions.v8,
                    platform: process.platform,
                    architecture: process.arch,
                },
            };
            writeJsonl(resultsPath, record);
            payloadsReceived += 1;
            return sendJson(response, 200, {
                run_id: runId,
                payload_id: payloadId,
                result_status: record.result_status,
                payload_sha256: record.payload_sha256,
                payload_bytes_utf8: record.payload_bytes_utf8,
                median_us_per_op: record.median_us_per_op,
                mean_us_per_op: record.mean_us_per_op,
                ops_per_second: record.ops_per_second,
                mib_per_second: record.mib_per_second,
                sample_count: record.sample_count,
            });
        } catch (error) {
            errorCount += 1;
            const record = { ...baseResult, result_status: 'error', error: error.message };
            writeJsonl(resultsPath, record);
            return sendJson(response, 500, record);
        }
    }

    if (request.method === 'POST' && url.pathname === '/api/finish') {
        if (finished) return sendJson(response, 409, { error: 'Run sudah difinalisasi.', run_id: runId });
        let finishRequest;
        try {
            const finishBody = await readRequestBody(request);
            finishRequest = JSON.parse(finishBody.toString('utf8'));
        } catch (error) {
            return sendJson(response, 400, { error: `Body finalisasi tidak valid: ${error.message}` });
        }
        const expectedCount = Number(finishRequest.expected_payload_count);
        if (!Number.isSafeInteger(expectedCount) || expectedCount < 1) {
            return sendJson(response, 400, { error: 'expected_payload_count harus integer positif.' });
        }
        if (finishRequest.snapshot_id !== manifest.workload_snapshot.snapshot_id ||
            finishRequest.sample_manifest_sha256 !== manifest.workload_snapshot.sample_manifest_sha256) {
            return sendJson(response, 409, { error: 'Snapshot finalisasi tidak cocok dengan payload yang diterima.' });
        }
        manifest.workload_snapshot.expected_payload_count = expectedCount;
        finished = true;
        const allExpectedPayloadsReceived = payloadsReceived === expectedCount;
        finalizeManifest(manifest, errorCount === 0 && allExpectedPayloadsReceived && payloadsReceived > 0 ? 'COMPLETED' : 'FAILED');
        return sendJson(response, 200, {
            run_id: runId,
            status: manifest.status,
            verification_status: manifest.verification_status,
            payloads_received: payloadsReceived,
            error_count: errorCount,
            result_file: resultsPath,
            manifest_file: manifestPath,
        });
    }

    if (request.method === 'POST' && url.pathname === '/api/shutdown') {
        const suppliedToken = String(request.headers['x-shutdown-token'] || '');
        if (!shutdownToken || suppliedToken !== shutdownToken) {
            return sendJson(response, 403, { error: 'Shutdown token tidak valid.' });
        }

        if (!finished) {
            finished = true;
            finalizeManifest(manifest, 'INTERRUPTED');
        }

        response.on('finish', () => {
            server.close(() => process.exit(0));
        });
        return sendJson(response, 200, { status: 'SHUTTING_DOWN', run_id: runId });
    }

    return sendJson(response, 404, { error: 'Not found.' });
});

server.listen(PORT, HOST, () => {
    console.log('[json-baseline] Receiver ready');
    console.log(`  URL       : http://${HOST}:${PORT}`);
    console.log(`  Run ID    : ${runId}`);
    console.log(`  Output    : ${runDir}`);
    console.log(`  Config    : warmup=${WARMUP_ITERATIONS}, iterations/sample=${ITERATIONS_PER_SAMPLE}, rounds=${SAMPLE_ROUNDS}`);
    console.log('  Next      : run node benchmarks/json_baseline/feeder.js (or use run-baseline.js)');
});

function stopServer(signal) {
    if (!finished) {
        try {
            finalizeManifest(manifest, 'INTERRUPTED');
        } catch (error) {
            console.error('[json-baseline] Could not write interrupted manifest:', error.message);
        }
    }
    server.close(() => process.exit(0));
    setTimeout(() => process.exit(1), 3000).unref();
    console.warn(`[json-baseline] Received ${signal}; shutting down.`);
}

process.on('SIGINT', () => stopServer('SIGINT'));
process.on('SIGTERM', () => stopServer('SIGTERM'));
