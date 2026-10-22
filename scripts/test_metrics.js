// Metrik pengujian ze_json: menjalankan seluruh binary test dengan
// --gtest_output=json lalu merangkum hasil semua kelompok uji menjadi
// out/results/test/summary.json + summary.md (lampiran skripsi).
//
// Pemakaian:
//   node scripts/test_metrics.js                # preset native-debug
//   node scripts/test_metrics.js native-debug native-release
const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");

const roots = process.argv.slice(2);
const presets = roots.length ? roots : ["native-debug"];
const rootDir = path.join(__dirname, "..");
const outDir = path.join(rootDir, "out", "results", "test");
fs.mkdirSync(outDir, { recursive: true });

const hash = execFileSync("git", ["rev-parse", "--short", "HEAD"], {
  cwd: rootDir,
})
  .toString()
  .trim();

// Konversi string durasi gtest ("0.001s" / "15ms" / "500us") → milidetik.
function gtest_ms(duration) {
  if (duration === undefined || duration === null) return 0;
  const text = String(duration);
  if (text.endsWith("ms")) return parseFloat(text);
  if (text.endsWith("us")) return parseFloat(text) / 1e3;
  return parseFloat(text) * 1e3; // satuan detik (bawaan gtest JSON)
}

const groups = [];
const all_tests = [];

for (const preset of presets) {
  const testDir = path.join(rootDir, "build", preset, "tests");
  if (!fs.existsSync(testDir)) {
    console.error(`[skip] ${testDir} tidak ada`);
    continue;
  }

  const exes = fs.globSync("*.exe", { cwd: testDir }).sort();
  for (const exe of exes) {
    const group = path.basename(exe, ".exe");
    const report_path = path.join(
      outDir,
      `${group}_${preset}_${hash}.json`
    );
    const command = `${path.join(testDir, exe)} --gtest_output=json:${report_path}`;

    let failed_to_run = false;
    try {
      execFileSync(path.join(testDir, exe), [
        `--gtest_output=json:${report_path}`,
      ]);
    } catch (error) {
      // Exit-code non-nol: run gagal. Lapor lalu tetap baca report JSON
      // (gtest tetap menulis report meskipun ada test gagal).
      failed_to_run = true;
      console.error(
        `[GAGAL] ${group} (${preset}): ${error.stderr?.toString().trim()}`
      );
      console.error(`        ${command}`);
    }

    let report;
    try {
      report = JSON.parse(fs.readFileSync(report_path, "utf8"));
    } catch (error) {
      groups.push({
        group,
        preset,
        hash,
        command,
        executable: path.join("build", preset, "tests", exe),
        report: path.relative(rootDir, report_path).replaceAll("\\", "/"),
        status: failed_to_run ? "RUN_FAILED" : "REPORT_MISSING",
        tests: 0,
        failures: 0,
        disabled: 0,
        errors: 0,
        time_ms: 0,
        detail: [],
      });
      continue;
    }

    const detail = report.testsuites.flatMap((suite) =>
      suite.testsuite.map((test) => ({
        suite: suite.name,
        test: test.name,
        status: test.status,
        result: test.result,
        time_ms: gtest_ms(test.time),
      }))
    );

    const entry = {
      group,
      preset,
      hash,
      command,
      executable: path.join("build", preset, "tests", exe),
      report: path.relative(rootDir, report_path).replaceAll("\\", "/"),
      status: failed_to_run ? "RUN_FAILED" : "PASSED",
      tests: Number(report.tests) || 0,
      failures: Number(report.failures) || 0,
      disabled: Number(report.disabled) || 0,
      errors: Number(report.errors) || 0,
      time_ms: gtest_ms(report.time),
      detail,
    };
    groups.push(entry);
    all_tests.push(
      ...detail.map((test) => ({ group, preset, ...test }))
    );
  }
}

// Agregat keseluruhan: jumlah test/gagal + seluruh waktu.
const totals = groups.reduce(
  (acc, group) => {
    acc.tests += group.tests;
    acc.failures += group.failures;
    acc.disabled += group.disabled;
    acc.errors += group.errors;
    acc.time_ms += group.time_ms;
    return acc;
  },
  { tests: 0, failures: 0, disabled: 0, errors: 0, time_ms: 0 }
);

const slowest_tests = all_tests
  .slice()
  .sort((a, b) => b.time_ms - a.time_ms)
  .slice(0, 8);

const summary = {
  hash,
  generated_at: new Date().toISOString(),
  presets,
  totals,
  groups,
  slowest_tests,
};
fs.writeFileSync(
  path.join(outDir, `summary_${hash}.json`),
  JSON.stringify(summary, null, 2)
);

// --------------------------------------------------------------------
//  Markdown: tabel metrik per kelompok uji W+ test tercepat/terlamaan
// --------------------------------------------------------------------
let md = `# Metrik Pengujian ze_json (\`${hash}\`)\n\n`;
md += `Dibuat: ${summary.generated_at} · preset: ${presets.join(", ")}\n\n`;
md += `**Total: ${totals.tests} test · ${totals.failures} gagal · `
  + `${(totals.time_ms / 1000).toFixed(2)} s**\n\n`;

md += "## Metrik per kelompok uji\n\n";
md += "| kelompok | preset | status | test | gagal | waktu (ms) | rata-rata (ms/test) |\n";
md += "|---|---|---|---:|---:|---:|---:|\n";
for (const group of groups) {
  const avg = group.tests ? group.time_ms / group.tests : 0;
  md += `| ${group.group} | ${group.preset} | ${group.status} `
    + `| ${group.tests} | ${group.failures} | ${group.time_ms.toFixed(3)} `
    + `| ${avg.toFixed(3)} |\n`;
}

md += "\n## 8 test paling memakan waktu\n\n";
md += "| # | kelompok | test | waktu (ms) |\n";
md += "|---:|---|---|---:|\n";
slowest_tests.forEach((test, index) => {
  md += `| ${index + 1} | ${test.group} | ${test.test} `
    + `| ${test.time_ms.toFixed(3)} |\n`;
});

fs.writeFileSync(path.join(outDir, `summary_${hash}.md`), md);

// --------------------------------------------------------------------
//  Log ringkas ke konsol
// --------------------------------------------------------------------
console.log(`[metrik] ${hash} — ${totals.tests} test, `
  + `${totals.failures} gagal, ${(totals.time_ms / 1000).toFixed(2)} s`);
for (const group of groups) {
  console.log(
    `  ${group.group} (${group.preset}): ${group.tests} test `
      + `${group.failures} gagal — ${group.time_ms.toFixed(3)} ms`
  );
}
const exit_failed = totals.failures > 0;
process.exit(exit_failed ? 1 : 0);
