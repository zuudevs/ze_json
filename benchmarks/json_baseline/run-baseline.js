'use strict';

/**
 * One-command orchestration for ze_json_research script-mode execution.
 * Starts the receiver, waits for readiness, runs the feeder, and shuts down
 * the receiver after it has written its own manifest and result artifact.
 */

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');

const PROJECT_ROOT = path.resolve(__dirname, '../..');
const SERVER_PATH = path.join(__dirname, 'server.js');
const FEEDER_PATH = path.join(__dirname, 'feeder.js');
const SAMPLE_SOURCE_DIR = path.join(PROJECT_ROOT, 'samples', 'saq-report');
const SAMPLE_MANIFEST_PATH = path.join(SAMPLE_SOURCE_DIR, 'manifest.json');
const EXPECTED_PAYLOAD_IDS = [
    'assistants',
    'courses',
    'lecturers',
    'schedules',
    'reports',
    'globalConfig',
];

function parseArguments(argv) {
    const result = {};

    for (let index = 0; index < argv.length; index += 1) {
        const key = argv[index];
        const value = argv[index + 1];

        if (!['--run-dir', '--run-id'].includes(key) || !value) {
            throw new Error(`Argument tidak dikenal atau tidak memiliki nilai: ${key}`);
        }

        result[key.slice(2)] = value;
        index += 1;
    }

    if (!result['run-dir'] || !result['run-id']) {
        throw new Error(
            'Penggunaan: node run-baseline.js --run-dir <path> --run-id <id>'
        );
    }

    return result;
}

function delay(milliseconds) {
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function waitForExit(child) {
    return new Promise((resolve, reject) => {
        child.once('error', reject);
        child.once('exit', (code, signal) => resolve({ code, signal }));
    });
}

function sha256(buffer) {
    return crypto.createHash('sha256').update(buffer).digest('hex');
}

function stageWorkloadSnapshot(runDir) {
    if (!fs.existsSync(SAMPLE_MANIFEST_PATH)) {
        throw new Error(
            `Sample manifest tidak ditemukan: ${SAMPLE_MANIFEST_PATH}. ` +
            'Jalankan generator dummy atau capture-samples terlebih dahulu.'
        );
    }

    const manifestBuffer = fs.readFileSync(SAMPLE_MANIFEST_PATH);
    const manifest = JSON.parse(manifestBuffer.toString('utf8'));

    if (!manifest.snapshot_id) {
        throw new Error('Sample manifest tidak memiliki snapshot_id.');
    }

    if (!Array.isArray(manifest.payloads) || manifest.payloads.length !== EXPECTED_PAYLOAD_IDS.length) {
        throw new Error(`Sample manifest harus berisi tepat ${EXPECTED_PAYLOAD_IDS.length} payload.`);
    }

    const ids = manifest.payloads.map((item) => item?.payload_id).sort();
    const expectedIds = [...EXPECTED_PAYLOAD_IDS].sort();
    if (JSON.stringify(ids) !== JSON.stringify(expectedIds)) {
        throw new Error(`Payload manifest tidak cocok: ${ids.join(', ')}`);
    }

    const stagedInputDir = path.join(runDir, 'inputs', 'saq-report');
    const metadataDir = path.join(runDir, 'metadata');
    fs.mkdirSync(stagedInputDir, { recursive: true });
    fs.mkdirSync(metadataDir, { recursive: true });

    for (const item of manifest.payloads) {
        const filename = path.basename(String(item.filename || ''));
        if (!filename || filename !== item.filename || !filename.endsWith('.json')) {
            throw new Error(`Nama file payload tidak aman: ${item.filename}`);
        }

        const sourcePath = path.join(SAMPLE_SOURCE_DIR, filename);
        if (!fs.existsSync(sourcePath)) {
            throw new Error(`Payload tidak ditemukan: ${sourcePath}`);
        }

        const bytes = fs.readFileSync(sourcePath);
        if (sha256(bytes) !== item.payload_sha256) {
            throw new Error(`SHA-256 tidak cocok untuk ${filename}; snapshot tidak disalin.`);
        }
        if (bytes.length !== item.payload_bytes_utf8) {
            throw new Error(`Ukuran byte tidak cocok untuk ${filename}; snapshot tidak disalin.`);
        }

        fs.writeFileSync(path.join(stagedInputDir, filename), bytes);
    }

    const stagedManifestPath = path.join(metadataDir, 'saq-report-sample-manifest.json');
    fs.writeFileSync(stagedManifestPath, manifestBuffer);
    fs.writeFileSync(path.join(stagedInputDir, 'manifest.json'), manifestBuffer);

    return {
        stagedInputDir,
        manifest,
        manifestSha256: sha256(manifestBuffer),
    };
}

async function waitForReceiver(baseUrl, server, timeoutMilliseconds = 15000) {
    const deadline = Date.now() + timeoutMilliseconds;
    let lastError = 'belum ada respons';

    while (Date.now() < deadline) {
        if (server.exitCode !== null || server.signalCode !== null) {
            throw new Error(
                `Receiver berhenti sebelum siap (exit=${server.exitCode}, signal=${server.signalCode}).`
            );
        }

        try {
            const response = await fetch(`${baseUrl}/healthz`);
            if (response.ok) {
                const health = await response.json();
                if (health.status === 'READY') return health;
                lastError = `status receiver ${health.status}`;
            } else {
                lastError = `HTTP ${response.status}`;
            }
        } catch (error) {
            lastError = error.message;
        }

        await delay(150);
    }

    throw new Error(`Receiver tidak siap dalam ${timeoutMilliseconds} ms: ${lastError}`);
}

function runChild(scriptPath, env) {
    const child = spawn(process.execPath, [scriptPath], {
        cwd: PROJECT_ROOT,
        env,
        stdio: 'inherit',
        windowsHide: true,
    });

    return { child, exited: waitForExit(child) };
}

async function shutdownReceiver(baseUrl, token, server, serverExit) {
    if (server.exitCode !== null || server.signalCode !== null) {
        await serverExit;
        return;
    }

    try {
        await fetch(`${baseUrl}/api/shutdown`, {
            method: 'POST',
            headers: { 'x-shutdown-token': token },
        });
    } catch {
        // The child may already have exited; await its exit state below.
    }

    const result = await Promise.race([
        serverExit,
        delay(4000).then(() => null),
    ]);

    if (result === null) {
        server.kill();
        await Promise.race([serverExit, delay(2000)]);
    }
}

async function main() {
    const args = parseArguments(process.argv.slice(2));
    const runDir = path.resolve(args['run-dir']);
    const runId = args['run-id'];

    if (!/^[a-zA-Z0-9][a-zA-Z0-9_.:-]{0,127}$/.test(runId)) {
        throw new Error(`Run ID tidak valid: ${runId}`);
    }

    // Verify and copy the exact six input byte streams into this external run.
    // The run remains reproducible even if the source sample folder changes later.
    const stagedSnapshot = stageWorkloadSnapshot(runDir);

    const port = process.env.SAQ_BENCH_PORT || '8787';
    const host = process.env.SAQ_BENCH_HOST || '127.0.0.1';
    const connectHost = host === '0.0.0.0' ? '127.0.0.1' : host;
    const baseUrl = `http://${connectHost}:${port}`;
    const baselineManifestPath = path.join(runDir, 'baseline-server', 'manifest.json');
    const shutdownToken = crypto.randomBytes(32).toString('hex');

    const childEnvironment = {
        ...process.env,
        SAQ_BENCH_DATA_DIR: runDir,
        SAQ_BENCH_RUN_DIR: runDir,
        SAQ_BENCH_RUN_ID: runId,
        SAQ_BENCH_SHUTDOWN_TOKEN: shutdownToken,
        SAQ_BENCH_HOST: host,
        SAQ_BENCH_PORT: String(port),
        SAQ_BENCH_URL: baseUrl,
        SAQ_BENCH_SAMPLE_DIR: stagedSnapshot.stagedInputDir,
    };

    console.log('[baseline-runner] Starting receiver...');
    console.log(`[baseline-runner] Research run : ${runId}`);
    console.log(`[baseline-runner] Output       : ${runDir}`);
    console.log(`[baseline-runner] Snapshot     : ${stagedSnapshot.manifest.snapshot_id}`);
    console.log(`[baseline-runner] Manifest SHA : ${stagedSnapshot.manifestSha256}`);

    const serverProcess = runChild(SERVER_PATH, childEnvironment);
    let feederExit = null;

    try {
        const health = await waitForReceiver(baseUrl, serverProcess.child);
        console.log(`[baseline-runner] Receiver ready; server run ID: ${health.run_id}`);

        console.log('[baseline-runner] Starting feeder...');
        const feederProcess = runChild(FEEDER_PATH, childEnvironment);
        feederExit = await feederProcess.exited;

        if (feederExit.code !== 0) {
            throw new Error(
                `Feeder gagal (exit=${feederExit.code}, signal=${feederExit.signal}).`
            );
        }

        const healthResponse = await fetch(`${baseUrl}/healthz`);
        if (!healthResponse.ok) {
            throw new Error(`Receiver health check akhir gagal: HTTP ${healthResponse.status}`);
        }

        const finalHealth = await healthResponse.json();
        if (finalHealth.status !== 'FINISHED') {
            throw new Error(`Receiver belum difinalisasi: ${finalHealth.status}`);
        }

        if (!fs.existsSync(baselineManifestPath)) {
            throw new Error(`Baseline server manifest tidak ditemukan: ${baselineManifestPath}`);
        }

        console.log('[baseline-runner] Benchmark completed.');
    } finally {
        await shutdownReceiver(
            baseUrl,
            shutdownToken,
            serverProcess.child,
            serverProcess.exited
        );
    }
}

main().catch((error) => {
    console.error('[baseline-runner] Failed:', error.stack || error.message);
    process.exitCode = 1;
});
