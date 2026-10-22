// Metrik benchmark ze_json: menjalankan seluruh binary benchmark dengan
// --benchmark_out=json lalu merangkum hasil semua kelompok uji menjadi
// out/results/benchmark/summary.json + summary.md (lampiran skripsi).
//
// Pemakaian:
//   node scripts/benchmark_metrics.js                    # native-release
//   node scripts/benchmark_metrics.js native-release ... (preset lain)
//   node scripts/benchmark_metrics.js --parse-only       # hanya re-summarize
const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");

const args = process.argv.slice(2);
const parse_only = args.includes("--parse-only");
const presets = args.filter((arg) => !arg.startsWith("--")).length
  ? args.filter((arg) => !arg.startsWith("--"))
  : ["native-release"];
const rootDir = path.join(__dirname, "..");
const outDir = path.join(rootDir, "out", "results", "benchmark");
fs.mkdirSync(outDir, { recursive: true });

const hash = execFileSync("git", ["rev-parse", "--short", "HEAD"], {
  cwd: rootDir,
})
  .toString()
  .trim();

// Normalisasi semua satuan waktu google-benchmark ke nanosekon.
function to_ns(time, unit) {
  const value = parseFloat(time);
  if (unit === "ms") return value * 1e6;
  if (unit === "us") return value * 1e3;
  if (unit === "s") return value * 1e9;
  return value; // ns
}

const groups = [];

for (const preset of presets) {
  const benchDir = path.join(rootDir, "build", preset, "benchmarks");
  if (!fs.existsSync(benchDir)) {
    console.error(`[skip] ${benchDir} tidak ada`);
    continue;
  }

  const exes = fs.globSync("*.exe", { cwd: benchDir }).sort();
  for (const exe of exes) {
    const group = path.basename(exe, ".exe");
    const report_path = path.join(
      outDir,
      `${group}_${preset}_${hash}.json`
    );
    const command = `${path.join(benchDir, exe)} --benchmark_out=${report_path}`;

    let failed_to_run = false;
    if (parse_only) {
      // Mode re-summarize: hanya baca report yang sudah ada dari run ini.
      if (!fs.existsSync(report_path)) {
        console.error(`[skip] report tidak ada: ${report_path}`);
        continue;
      }
    } else {
      try {
        execFileSync(path.join(benchDir, exe), [
          `--benchmark_out=${report_path}`,
          `--benchmark_out_format=json`,
        ]);
      } catch (error) {
        failed_to_run = true;
        console.error(
          `[GAGAL] ${group} (${preset}): ${error.stderr?.toString().trim()}`
        );
        console.error(`        ${command}`);
      }
    }

    let report;
    try {
      report = JSON.parse(fs.readFileSync(report_path, "utf8"));
    } catch (error) {
      groups.push({
        status: failed_to_run ? "RUN_FAILED" : "REPORT_MISSING",
        benchmarks: [],
        context: {},
      });
      continue;
    }

    // Ambil baris iterasi aktual saja (abaikan struktur meta lain).
    const benchmarks = report.benchmarks
      .filter((row) => row.run_type === "iteration" && row.iterations > 0)
      .map((row) => ({
        // Nama benchmark tanpa suffix argumen (".../min_time:...").
        // "Fixture/Nama" → ambil segmen terakhir; "BM_X" tetap utuh.
        name: row.name.split("/min")[0].split("/").pop(),
        time_ns: to_ns(row.real_time, row.time_unit),
        cpu_ns: to_ns(row.cpu_time, row.time_unit),
        iterations: row.iterations,
        bytes_per_second: row.bytes_per_second || null,
        items_per_second: row.items_per_second || null,
      }));

    groups.push({
      group,
      hash,
      executable: path.join("build", preset, "benchmarks", exe),
      report: path.relative(rootDir, report_path).replaceAll("\\", "/"),
      host_name: report.context?.host_name || "",
      cpus: `${report.context?.mhz_per_cpu || "?"} MHz × `
        + `${report.context?.num_cpus || "?"} core`,
      library_version: report.context?.library_version || "",
      build_type: report.context?.library_build_type || "",
      context_date: report.context?.date || "",
      benchmarks,
    });
  }
}

// ------------------------------------------------------------------------
//  Derivasi: pengganda speed scanner64 terhadap scanner8 per dataset
// ------------------------------------------------------------------------
const scanner_speedups = [];
for (const g of groups) {
  const map8 = new Map(
    g.benchmarks.filter((b) => b.name.startsWith("BM_Scanner8_"))
      .map((b) => [b.name.replace("BM_Scanner8_", ""), b])
  );
  for (const b of g.benchmarks.filter((b) =>
    b.name.startsWith("BM_Scanner64_"))) {
    const key = b.name.replace("BM_Scanner64_", "");
    const base = map8.get(key);
    if (base && base.time_ns > 0) {
      scanner_speedups.push({
        dataset: key,
        scanner8_ns: base.time_ns,
        scanner64_ns: b.time_ns,
        speedup: base.time_ns / b.time_ns,
        scanner8_mbs: base.bytes_per_second
          ? base.bytes_per_second / 1e6 : null,
        scanner64_mbs: b.bytes_per_second
          ? b.bytes_per_second / 1e6 : null,
      });
    }
  }
}

const summary = {
  hash,
  generated_at: new Date().toISOString(),
  presets,
  groups,
  scanner_speedups,
};
fs.writeFileSync(
  path.join(outDir, `summary_${hash}.json`),
  JSON.stringify(summary, null, 2)
);

// ------------------------------------------------------------------------
//  Markdown: ringkasan per kelompok benchmark + tabel derivasi
// ------------------------------------------------------------------------
let md = `# Metrik Benchmark ze_json (\`${hash}\`)\n\n`;
md += `Dibuat: ${summary.generated_at} · preset: ${presets.join(", ")}\n\n`;

for (const g of groups) {
  md += `## ${g.group} (${g.hash}, ${g.build_type})\n\n`;
  md += `> host \`${g.host_name}\` · CPU ${g.cpus} · benchmark lib `
    + `${g.library_version} · ${g.context_date}\n\n`;
  md += "| benchmark | ns/op | iterasi | throughput |\n";
  md += "|---|---:|---:|---:|\n";
  for (const b of g.benchmarks) {
    const throughput = b.bytes_per_second
      ? `${(b.bytes_per_second / 1e6).toFixed(1)} MB/s`
      : "—";
    md += `| ${b.name} | ${b.time_ns.toFixed(1)} `
      + `| ${b.iterations.toLocaleString("id-ID")} | ${throughput} |\n`;
  }
  md += "\n";
}

if (scanner_speedups.length) {
  md += "## Pengganda speed scanner64 vs scanner8 (per dataset)\n\n";
  md += "| dataset | scanner8 (ns/op) | scanner64 (ns/op) | pengganda | scanner8 (MB/s) | scanner64 (MB/s) |\n";
  md += "|---|---:|---:|---:|---:|---:|\n";
  for (const s of scanner_speedups) {
    md += `| ${s.dataset} | ${s.scanner8_ns.toFixed(1)} `
      + `| ${s.scanner64_ns.toFixed(1)} `
      + `| ${s.speedup.toFixed(2)}× `
      + `| ${s.scanner8_mbs ? s.scanner8_mbs.toFixed(1) : "—"} `
      + `| ${s.scanner64_mbs ? s.scanner64_mbs.toFixed(1) : "—"} |\n`;
  }
  md += "\n";
}

fs.writeFileSync(path.join(outDir, `summary_${hash}.md`), md);

// Log ringkas ke konsol.
console.log(`[metrik-benchmark] ${hash}: ${groups.length} kelompok`);
for (const g of groups) {
  console.log(`  ${g.group}: ${g.benchmarks.length} benchmark`);
}
