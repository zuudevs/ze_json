'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { createRequire } = require('node:module');
const { execFileSync } = require('node:child_process');

const PROJECT_ROOT = path.resolve(__dirname, '..');
const DEFAULT_SAQ_REPORT_ROOT = path.resolve(__dirname, '../../saq-report');
const SAQ_REPORT_ROOT = path.resolve(
    process.env.SAQ_REPORT_ROOT || DEFAULT_SAQ_REPORT_ROOT
);
const SAMPLE_DIR = path.join(PROJECT_ROOT, 'samples', 'saq-report');

const payloadDefinitions = [
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

function dateInJakarta(date) {
    const parts = new Intl.DateTimeFormat('en-CA', {
        timeZone: 'Asia/Jakarta',
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
    }).formatToParts(date);

    const values = Object.fromEntries(parts.map((part) => [part.type, part.value]));
    return `${values.year}-${values.month}-${values.day}`;
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

function loadGenerateId() {
    const packagePath = path.join(SAQ_REPORT_ROOT, 'package.json');

    if (!fs.existsSync(packagePath)) {
        throw new Error(
            `SAQ Report checkout tidak ditemukan: ${SAQ_REPORT_ROOT}. ` +
            'Atur SAQ_REPORT_ROOT ke root repository saq-report.'
        );
    }

    const appRequire = createRequire(packagePath);
    const helpersPath = path.join(SAQ_REPORT_ROOT, 'routes', '_helpers.js');
    const helpers = appRequire(helpersPath);

    if (typeof helpers.generateId !== 'function') {
        throw new Error(
            `generateId tidak ditemukan atau bukan function: ${helpersPath}`
        );
    }

    return helpers.generateId;
}

function main() {
    const generateId = loadGenerateId();
    const now = new Date();
    const date = dateInJakarta(now);
    const formattedDate = new Date(`${date}T00:00:00+07:00`).toLocaleDateString('id-ID', {
        timeZone: 'Asia/Jakarta',
        weekday: 'long',
        day: 'numeric',
        month: 'long',
        year: 'numeric',
    });

    const assistantId = generateId('ASST');
    const courseId = generateId('COURSE');
    const lecturerId = generateId('LECT');
    const scheduleId = generateId('SCHED');
    const reportId = generateId('RPT');

    const payloads = [
        {
            payload_id: 'assistants',
            source_key: 'assistants',
            source_repository_export: 'assistantRepo',
            value: [
                {
                    id: assistantId,
                    nama_asisten: 'ADITYA',
                    nama_panjang: 'Aditya Pratama',
                    panggilan: 'Aditya',
                    no_telp: '6281111111111',
                    ttd_image: '',
                },
            ],
        },
        {
            payload_id: 'courses',
            source_key: 'courses',
            source_repository_export: 'courseRepo',
            value: [
                {
                    id: courseId,
                    code: 'IF101',
                    name: 'Algoritma dan Pemrograman',
                    credits: 3,
                    sks: 3,
                    description: 'Pengenalan algoritma dan dasar pemrograman.',
                    department: 'Teknik Informatika',
                    semester: 1,
                },
            ],
        },
        {
            payload_id: 'lecturers',
            source_key: 'lecturers',
            source_repository_export: 'lecturerRepo',
            value: [
                {
                    id: lecturerId,
                    name: 'Budi Santoso',
                    title: 'S.Kom., M.Kom.',
                    email: 'budi.santoso@example.test',
                    phone: '6281222222222',
                    department: 'Teknik Informatika',
                },
            ],
        },
        {
            payload_id: 'schedules',
            source_key: 'schedules',
            source_repository_export: 'scheduleRepo',
            value: [
                {
                    id: scheduleId,
                    course_id: courseId,
                    lecturer_id: lecturerId,
                    class: 'A',
                    day: 'Senin',
                    time: '08:00 - 10:30',
                    pj_assistant_id: assistantId,
                    assistant_ids: [assistantId],
                },
            ],
        },
        {
            payload_id: 'reports',
            source_key: 'reports',
            source_repository_export: 'reportRepo',
            value: [
                {
                    id: reportId,
                    schedule_id: scheduleId,
                    date,
                    formattedDate,
                    materi: 'Pengenalan variabel dan tipe data',
                    deskripsiKegiatan: 'Praktikum dasar pemrograman dan latihan deklarasi variabel.',
                    keterangan: '',
                    attending_assistant_ids: [assistantId],
                    status: 'hadir',
                    imagePath: null,
                    imageFilename: null,
                    author: 'dummy-sender@example.test',
                    receivedAt: now.toISOString(),
                },
            ],
        },
        {
            payload_id: 'globalConfig',
            source_key: 'globalConfig',
            source_repository_export: 'globalConfigRepo',
            value: {
                semester: {
                    id: 'SEM_001',
                    name: 'Ganjil',
                    year: Number(date.slice(0, 4)),
                    months: [8, 9, 10, 11, 12, 1],
                },
                system: {
                    cron_time: '15:00',
                    group_id: '',
                    version: '1.0.0',
                    last_updated: date,
                },
                bot_messages: {
                    greetings: ['Terima kasih atas laporannya!'],
                    emojis: ['👍', '✨'],
                },
            },
        },
    ];

    fs.mkdirSync(SAMPLE_DIR, { recursive: true });

    const manifest = {
        schema_version: 1,
        dataset_kind: 'synthetic_dummy',
        snapshot_id: `saq-report-dummy-${utcCompact()}`,
        captured_at_utc: new Date().toISOString(),
        source: captureSourceInfo(),
        capture_method: 'synthetic one-record-per-repository generator using saq-report routes/_helpers.js generateId()',
        payloads: [],
        notes: [
            'This snapshot is synthetic dummy data, not a capture of production or live SAQ Report records.',
            'Each of the five repository arrays contains exactly one linked dummy record; globalConfig is an object.',
            'Payload files use compact JSON.stringify(value) bytes without a trailing newline, matching cloneJson serialization form.',
            'The source repository helper generateId() is imported from routes/_helpers.js of the configured SAQ Report checkout.',
        ],
    };

    for (const item of payloads) {
        const rawPayload = JSON.stringify(item.value);
        const buffer = Buffer.from(rawPayload, 'utf8');
        const fileName = `${item.payload_id}.json`;
        const filePath = path.join(SAMPLE_DIR, fileName);
        const payloadHash = sha256(buffer);

        fs.writeFileSync(filePath, buffer);

        manifest.payloads.push({
            payload_id: item.payload_id,
            source_key: item.source_key,
            source_repository_export: item.source_repository_export,
            filename: fileName,
            payload_sha256: payloadHash,
            payload_bytes_utf8: buffer.length,
            payload_chars_utf16: rawPayload.length,
            valid_json: true,
            ...profileValue(item.value),
        });

        console.log(
            `[generator] ${item.payload_id.padEnd(12)} ` +
            `${String(buffer.length).padStart(9)} bytes  sha256=${payloadHash}`
        );
    }

    const manifestPath = path.join(SAMPLE_DIR, 'manifest.json');
    fs.writeFileSync(
        manifestPath,
        `${JSON.stringify(manifest, null, 2)}\n`,
        'utf8'
    );

    console.log(`\nGenerated ${manifest.payloads.length} dummy payloads in ${SAMPLE_DIR}`);
    console.log(`Manifest: ${manifestPath}`);
    console.log('Dataset kind: synthetic_dummy (not representative production data).');
}

try {
    main();
} catch (error) {
    console.error('[generator] Failed:', error.stack || error.message);
    process.exitCode = 1;
}
