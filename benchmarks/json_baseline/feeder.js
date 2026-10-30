'use strict';

/** Send captured sample/*.json payloads to the local JSON.parse receiver. */

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const PROJECT_ROOT = path.resolve(__dirname, '../..');
const SAMPLE_DIR = path.resolve(
    process.env.SAQ_BENCH_SAMPLE_DIR || path.join(PROJECT_ROOT, 'samples', 'saq-report')
);
const BASE_URL = (process.env.SAQ_BENCH_URL || 'http://127.0.0.1:8787').replace(/\/$/, '');
const EXPECTED_PAYLOAD_IDS = [
    'assistants',
    'courses',
    'lecturers',
    'schedules',
    'reports',
    'globalConfig',
];

function sha256(buffer) {
    return crypto.createHash('sha256').update(buffer).digest('hex');
}

async function postRawPayload(payload, snapshotInfo) {
    const response = await fetch(`${BASE_URL}/api/benchmark`, {
        method: 'POST',
        headers: {
            'content-type': 'application/json; charset=utf-8',
            'x-payload-id': payload.metadata.payload_id,
            'x-payload-sha256': payload.metadata.payload_sha256,
            'x-snapshot-id': snapshotInfo.snapshotId,
            'x-sample-manifest-sha256': snapshotInfo.manifestSha256,
        },
        body: payload.buffer,
    });

    const responseText = await response.text();
    let responseJson;

    try {
        responseJson = JSON.parse(responseText);
    } catch {
        throw new Error(
            `Receiver returned non-JSON response (${response.status}): ${responseText.slice(0, 300)}`
        );
    }

    if (!response.ok) {
        throw new Error(
            `Receiver rejected ${payload.metadata.payload_id} ` +
            `(${response.status}): ${responseJson.error || responseText}`
        );
    }

    return responseJson;
}

async function main() {
    const manifestPath = path.join(SAMPLE_DIR, 'manifest.json');

    if (!fs.existsSync(manifestPath)) {
        throw new Error(
            `Sample manifest belum ada: ${manifestPath}. ` +
            'Jalankan scripts/saq-dummy-generator.js atau capture-samples.js terlebih dahulu.'
        );
    }

    const manifestBuffer = fs.readFileSync(manifestPath);
    const manifest = JSON.parse(manifestBuffer.toString('utf8'));
    const snapshotInfo = {
        snapshotId: String(manifest.snapshot_id || ''),
        manifestSha256: sha256(manifestBuffer),
    };

    if (!snapshotInfo.snapshotId) {
        throw new Error('samples/saq-report/manifest.json tidak memiliki snapshot_id.');
    }

    if (!Array.isArray(manifest.payloads) || manifest.payloads.length !== EXPECTED_PAYLOAD_IDS.length) {
        throw new Error(
            `Manifest harus berisi tepat ${EXPECTED_PAYLOAD_IDS.length} payload.`
        );
    }

    const actualIds = manifest.payloads.map((item) => item?.payload_id);
    const expectedSorted = [...EXPECTED_PAYLOAD_IDS].sort();
    const actualSorted = [...actualIds].sort();

    if (JSON.stringify(actualSorted) !== JSON.stringify(expectedSorted)) {
        throw new Error(
            `Payload manifest tidak cocok. Expected: ${expectedSorted.join(', ')}; ` +
            `actual: ${actualSorted.join(', ')}`
        );
    }

    const healthResponse = await fetch(`${BASE_URL}/healthz`);

    if (!healthResponse.ok) {
        throw new Error(`Receiver health check gagal: HTTP ${healthResponse.status}`);
    }

    const health = await healthResponse.json();

    if (health.status !== 'READY') {
        throw new Error(`Receiver tidak siap: ${health.status}`);
    }

    console.log(`[feeder] Receiver : ${BASE_URL}`);
    console.log(`[feeder] Run ID   : ${health.run_id}`);
    console.log(`[feeder] Snapshot : ${snapshotInfo.snapshotId}`);
    console.log(`[feeder] Payloads : ${manifest.payloads.length}\n`);

    let sent = 0;

    try {
        for (const metadata of manifest.payloads) {
            const fileName = path.basename(String(metadata.filename || ''));
            if (!fileName || fileName !== metadata.filename) {
                throw new Error(`Filename payload tidak aman: ${metadata.filename}`);
            }

            const filePath = path.join(SAMPLE_DIR, fileName);

            if (!fs.existsSync(filePath)) {
                throw new Error(`Payload file tidak ditemukan: ${filePath}`);
            }

            const buffer = fs.readFileSync(filePath);
            const actualHash = sha256(buffer);

            if (actualHash !== metadata.payload_sha256) {
                throw new Error(
                    `SHA-256 mismatch pada ${metadata.filename}. ` +
                    'Sample berubah setelah capture; capture ulang atau pulihkan file yang benar.'
                );
            }

            if (buffer.length !== metadata.payload_bytes_utf8) {
                throw new Error(`Ukuran payload mismatch pada ${metadata.filename}.`);
            }

            const result = await postRawPayload({ metadata, buffer }, snapshotInfo);
            sent += 1;

            console.log(
                `[feeder] ${metadata.payload_id.padEnd(12)} ` +
                `${String(result.payload_bytes_utf8).padStart(9)} bytes  ` +
                `median=${Number(result.median_us_per_op).toFixed(3)} us/op  ` +
                `mean=${Number(result.mean_us_per_op).toFixed(3)} us/op  ` +
                `throughput=${Number(result.mib_per_second).toFixed(2)} MiB/s`
            );
        }

        const finishResponse = await fetch(`${BASE_URL}/api/finish`, {
            method: 'POST',
            headers: { 'content-type': 'application/json; charset=utf-8' },
            body: JSON.stringify({
                expected_payload_count: manifest.payloads.length,
                snapshot_id: snapshotInfo.snapshotId,
                sample_manifest_sha256: snapshotInfo.manifestSha256,
            }),
        });

        const finishText = await finishResponse.text();
        let finish;

        try {
            finish = JSON.parse(finishText);
        } catch {
            throw new Error(`Finalisasi menghasilkan respons non-JSON: ${finishText}`);
        }

        if (!finishResponse.ok) {
            throw new Error(`Finalisasi run gagal: ${finish.error || finishText}`);
        }

        console.log(`\n[feeder] Sent       : ${sent}/${manifest.payloads.length}`);
        console.log(`[feeder] Run status : ${finish.status}`);
        console.log(`[feeder] Verify     : ${finish.verification_status}`);
        console.log(`[feeder] Results    : ${finish.result_file}`);
        console.log(`[feeder] Manifest   : ${finish.manifest_file}`);

        if (finish.status !== 'COMPLETED' || finish.verification_status !== 'VERIFIED') {
            process.exitCode = 2;
        }
    } catch (error) {
        console.error(
            `\n[feeder] Stopped after ${sent}/${manifest.payloads.length} payloads: ${error.message}`
        );
        console.error(
            'Run belum berhasil difinalisasi; wrapper akan menghentikan receiver dan menyimpan status INTERRUPTED.'
        );
        process.exitCode = 1;
    }
}

main().catch((error) => {
    console.error('[feeder] Failed:', error.stack || error.message);
    process.exitCode = 1;
});
