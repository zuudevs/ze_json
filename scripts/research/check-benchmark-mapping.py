#!/usr/bin/env python3
"""Check that a declared experiment would map every benchmark row it measures.

A formal run can take ten minutes or more, and a declared name pattern that
matches nothing turns into UNMAPPED rows only afterwards. This tool answers the
question first: it lists the benchmarks a built executable registers, builds the
benchmark specs exactly the way the importer would, and reports the mapping in
both directions.

Usage (from the ze_json repository root):

    python scripts/research/check-benchmark-mapping.py \
        --experiment parser-ondemand --target parser_benchmark \
        --build-dir build/native-release

Exit code 0 means every registered benchmark maps to exactly one spec and no
declared spec is unmeasured. Exit code 1 means the definition does not describe
the executable, which is a defect in the definition rather than in the run.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def _import_research() -> tuple:
    """Import the research package from the submodule checkout."""
    repo_root = Path(__file__).resolve().parents[2]
    research_src = repo_root / "research" / "src"
    if str(research_src) not in sys.path:
        sys.path.insert(0, str(research_src))
    try:
        from ze_research.database import connect, migrate, persist_definition
        from ze_research.ingest import _benchmark_spec_match, _normalize_benchmark_name
        from ze_research.registry import (
            definition_sha256,
            find_experiment,
            validate_experiment,
        )
    except ImportError as exc:
        print(f"ERROR: cannot import ze_research from {research_src}: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
    return (
        find_experiment,
        validate_experiment,
        definition_sha256,
        persist_definition,
        connect,
        migrate,
        _benchmark_spec_match,
        _normalize_benchmark_name,
    )


def _resolve_executable(build_dir: Path, target: str) -> Path:
    candidate = build_dir / "benchmarks" / target
    if candidate.is_file():
        return candidate
    windows = Path(str(candidate) + ".exe")
    if windows.is_file():
        return windows
    print(f"ERROR: benchmark executable not found: {candidate}(.exe)", file=sys.stderr)
    raise SystemExit(2)


def _registered_benchmarks(executable: Path) -> list[str]:
    result = subprocess.run(
        [str(executable), "--benchmark_list_tests"],
        capture_output=True,
        text=True,
        check=False,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        print(f"ERROR: {executable} --benchmark_list_tests failed", file=sys.stderr)
        print((result.stderr or "")[-2000:], file=sys.stderr)
        raise SystemExit(2)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment", required=True, help="Experiment ID from the registry")
    parser.add_argument("--target", required=True, help="Benchmark target name, for example parser_benchmark")
    parser.add_argument("--build-dir", required=True, help="CMake binary directory")
    parser.add_argument("--research-dir", default=None, help="research checkout; defaults to <repo>/research")
    parser.add_argument("--registry-dir", default=None, help="Experiment registry directory")
    parser.add_argument(
        "--allow-unmeasured-specs",
        action="store_true",
        help="Do not fail when a declared spec has no registered benchmark",
    )
    args = parser.parse_args(argv)

    (
        find_experiment,
        validate_experiment,
        definition_sha256,
        persist_definition,
        connect,
        migrate,
        spec_match,
        normalize_name,
    ) = _import_research()

    repo_root = Path(__file__).resolve().parents[2]
    research_dir = (
        Path(args.research_dir).expanduser().resolve()
        if args.research_dir
        else repo_root / "research"
    )
    registry_dir = (
        Path(args.registry_dir).expanduser().resolve()
        if args.registry_dir
        else research_dir / "research" / "experiments"
    )

    _, document = find_experiment(registry_dir, args.experiment)
    structural = validate_experiment(document)
    if structural:
        print(f"ERROR: definisi {args.experiment} tidak valid:", file=sys.stderr)
        for message in structural:
            print(f"- {message}", file=sys.stderr)
        return 2

    executable = _resolve_executable(
        Path(args.build_dir).expanduser().resolve(), args.target
    )
    registered = _registered_benchmarks(executable)
    if not registered:
        print(f"ERROR: {executable} registered no benchmarks", file=sys.stderr)
        return 2

    # Windows keeps a sqlite file locked while any connection to it is open, and
    # both migrate() and connect() hold handles, so clean up tolerantly rather
    # than letting a locked file abort a check that already produced its answer.
    temporary = Path(tempfile.mkdtemp(prefix="benchmark-mapping-"))
    try:
        database = temporary / "mapping.sqlite"
        migrate(database)
        snapshot = {
            "schema_version": document.get("schema_version"),
            "experiment": document["experiment"],
        }
        connection = connect(database)
        try:
            version_id = persist_definition(connection, snapshot)
            connection.commit()
            rows = connection.execute(
                "SELECT benchmark_spec_id, name_pattern, algorithm_id, workload_id "
                "FROM benchmark_specs WHERE experiment_version_id=?",
                (version_id,),
            ).fetchall()
        finally:
            connection.close()
        specs = [dict(row) for row in rows]
    finally:
        shutil.rmtree(temporary, ignore_errors=True)

    mapped = 0
    problems: list[str] = []
    covered: set[str] = set()
    print(f"experiment   : {args.experiment}")
    print(f"executable   : {executable}")
    print(f"definition   : {definition_sha256(snapshot)[:16]}")
    print(f"specs        : {len(specs)}")
    print(f"registered   : {len(registered)}")
    print()
    for name in registered:
        spec_id, status = spec_match(name, specs)
        if status == "MAPPED":
            mapped += 1
            covered.add(spec_id)
            continue
        problems.append(f"{status:9} {name}")
        normalized = normalize_name(name)
        hint = ""
        if not any(normalize_name(pattern) == normalized for pattern in [s["name_pattern"] for s in specs]):
            hint = "  (no spec for this registered name)"
        print(f"{status:9} {name}{hint}")

    print()
    unmeasured = [spec for spec in specs if spec["benchmark_spec_id"] not in covered]
    if unmeasured:
        print("Declared specs with no registered benchmark:")
        for spec in unmeasured:
            print(
                f"  {spec['algorithm_id']}/{spec['workload_id']}: {spec['name_pattern']}"
            )

    print()
    print(f"mapped      : {mapped}/{len(registered)}")
    print(f"unmapped    : {len(problems)}")
    print(f"unmeasured  : {len(unmeasured)}")

    if problems:
        return 1
    if unmeasured and not args.allow_unmeasured_specs:
        print(
            "\nNote: specs without measurements are allowed for a restricted "
            "variant, but check that this is intended.",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    # argparse expects argv without the program name, so let it read sys.argv
    # itself rather than handing it the script path as a positional argument.
    raise SystemExit(main())
