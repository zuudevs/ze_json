'use strict';

/**
 * Capture the six values exposed by the real saq-report repositories.
 * Data is read-only: this script never writes to the application's database.
 */

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { createRequire } = require('node:module');
const { execFileSync } = require('node:child_process');

const PROJECT_ROOT = path.resolve(__dirname, '../..');
const DEFAULT_SAQ_REPORT_ROOT = path.resolve(__dirname, '../../../saq-report');
const SAQ_REPORT_ROOT = path.resolve(
    process.env.SAQ_REPORT_ROOT || DEFAULT_SAQ_REPORT_ROOT
);
const SAMPLE_DIR = path.join(PROJECT_ROOT, 'samples', 'saq-report');

const payloadRepositories = [
    { payloadId: 'assistants', sourceKey: 'assistants', exportName: 'assistantRepo' },
    { payloadId: 'courses', sourceKey: 'courses', exportName: 'courseRepo' },
    { payloadId: 'lecturers', sourceKey: 'lecturers', exportName: 'lecturerRepo' },
    { payloadId: 'schedules', sourceKey: 'schedules', exportName: 'scheduleRepo' },
    { payloadId: 'reports', sourceKey: 'reports', exportName: 'reportRepo' },
    { payloadId: 'globalConfig', sourceKey: 'globalConfig', exportName: 'globalConfigRepo' },
];

function sha256(buffer) {
    return crypto.createHash('sha256').update(buffer).digest('hex');
}

function utcCompact() {
    return new Date().toISOString().replace(/[-:.]/g, '');
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
        counts.max_nesting_depth = Math.max(counts.max_nesting_depth, depth);

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

    const rootType = value === null
        ? 'null'
        : (Array.isArray(value) ? 'array' : typeof value);
    const rootItemCount = Array.isArray(value)
        ? value.length
        : (value !== null && typeof value === 'object' ? Object.keys(value).length : null);

    return {
        root_type: rootType,
        root_item_count: rootItemCount,
        ...counts,
    };
}

function git(args) {
    return execFileSync('git', args, {
        cwd: SAQ_REPORT_ROOT,
        encoding: 'utf8',
        stdio: ['ignore', 'pipe', 'ignore'],
    }).trim();
}

function captureSourceInfo() {
    try {
        const status = git(['status', '--porcelain=v1', '--untracked-files=all']);

        return {
            repository_url: 'https://github.com/zuudevs/saq-report',
            repository_root: SAQ_REPORT_ROOT,
            git_commit: git(['rev-parse', 'HEAD']),
            branch: git(['branch', '--show-current']) || null,
            worktree_clean: status.length === 0,
            status_porcelain: status,
        };
    } catch (error) {
        return {
            repository_url: 'https://github.com/zuudevs/saq-report',
            repository_root: SAQ_REPORT_ROOT,
            git_commit: null,
            branch: null,
            worktree_clean: null,
            provenance_error: error.message,
        };
    }
}

function loadApplication() {
    const packagePath = path.join(SAQ_REPORT_ROOT, 'package.json');

    if (!fs.existsSync(packagePath)) {
        throw new Error(
            `SAQ Report checkout tidak ditemukan: ${SAQ_REPORT_ROOT}. ` +
            'Atur environment variable SAQ_REPORT_ROOT ke root repository saq-report.'
        );
    }

    const appRequire = createRequire(packagePath);
    const envPath = path.join(SAQ_REPORT_ROOT, '.env');

    if (fs.existsSync(envPath)) {
        appRequire('dotenv').config({ path: envPath });
    }

    return {
        appRequire,
        connectDB: appRequire(path.join(SAQ_REPORT_ROOT, 'config/database')).connectDB,
        repositories: appRequire(path.join(SAQ_REPORT_ROOT, 'repositories')),
        mongoose: appRequire('mongoose'),
    };
}

async function main() {
    const { connectDB, repositories, mongoose } = loadApplication();

    fs.mkdirSync(SAMPLE_DIR, { recursive: true });

    await connectDB();

    try {
        await repositories.initRepositories();

        const manifest = {
            schema_version: 1,
            dataset_kind: 'captured_application_data',
            snapshot_id: `saq-report-${utcCompact()}`,
            captured_at_utc: new Date().toISOString(),
            source: captureSourceInfo(),
            capture_method: 'repositories.read() after connectDB() and initRepositories()',
            payloads: [],
            notes: [
                'Payload files contain compact JSON.stringify(repositoryValue) bytes.',
                'The serialized bytes represent repository.read() values; they are not raw MongoDB BSON documents.',
                'Payloads may contain personal or operational data. Keep them local and do not commit real snapshots to a public repository.',
                'The feeder sends exact file bytes. SHA-256 and byte length are verified before and after transfer.',
            ],
        };

        for (const item of payloadRepositories) {
            const repository = repositories[item.exportName];

            if (!repository || typeof repository.read !== 'function') {
                throw new Error(
                    `Repository ${item.exportName} tidak ditemukan atau tidak memiliki read().`
                );
            }

            const value = repository.read();
            const rawPayload = JSON.stringify(value);

            if (typeof rawPayload !== 'string') {
                throw new Error(
                    `Repository ${item.exportName} menghasilkan nilai yang tidak dapat diserialisasi menjadi JSON.`
                );
            }

            const buffer = Buffer.from(rawPayload, 'utf8');
            const parsedAgain = JSON.parse(rawPayload);
            const fileName = `${item.payloadId}.json`;
            const filePath = path.join(SAMPLE_DIR, fileName);
            const payloadHash = sha256(buffer);

            // Preserve the exact compact serialization bytes, without formatting whitespace.
            fs.writeFileSync(filePath, buffer);

            manifest.payloads.push({
                payload_id: item.payloadId,
                source_key: item.sourceKey,
                source_repository_export: item.exportName,
                filename: fileName,
                payload_sha256: payloadHash,
                payload_bytes_utf8: buffer.length,
                payload_chars_utf16: rawPayload.length,
                valid_json: true,
                ...profileValue(parsedAgain),
            });

            console.log(
                `[capture] ${item.payloadId.padEnd(12)} ` +
                `${String(buffer.length).padStart(9)} bytes  sha256=${payloadHash}`
            );
        }

        const manifestPath = path.join(SAMPLE_DIR, 'manifest.json');
        fs.writeFileSync(
            manifestPath,
            `${JSON.stringify(manifest, null, 2)}\n`,
            'utf8'
        );

        console.log(`\nCaptured ${manifest.payloads.length} payloads into: ${SAMPLE_DIR}`);
        console.log(`Manifest: ${manifestPath}`);
        console.log('Review the snapshot locally; it may contain sensitive data.');
    } finally {
        await mongoose.disconnect();
    }
}

main().catch((error) => {
    console.error('[capture] Failed:', error.stack || error.message);
    process.exitCode = 1;
});
