#!/usr/bin/env python3
"""Validate the YAML snippets that a README tells readers to copy.

A README snippet that no longer matches the registry schema is worse than no
snippet: it fails only when a reader tries it, long after the doc was written.
This extracts every ```yaml block that starts with `execution:` from a given
README, wraps it in a minimal valid definition, and runs the registry
validation against it.

Usage:

    python scripts/research/check_readme_snippet.py <README.md>
"""

from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[2]

BASE_DEFINITION = {
    "schema_version": 1,
    "experiment": {
        "id": "readme-snippet-check",
        "title": "readme snippet check",
        "status": "approved_for_run",
        "categories": ["smoke", "docs"],
        "purpose": "Memastikan cuplikan YAML di README masih dapat divalidasi.",
        "motivation": "Cuplikan yang usang hanya gagal ketika pembaca mencobanya.",
        "research_question": "Apakah cuplikan README lolos validasi registry?",
        "hypotheses": [
            {
                "id": "H1",
                "statement": "Cuplikan README lolos validasi formal.",
                "metric": "median_us_per_op",
                "expected_direction": "lower",
            }
        ],
        "assumptions": ["Definisi lengkap hanya untuk pengujian dokumentasi."],
        "limitations": ["Tidak melakukan run apa pun."],
        "research_basis": [
            {
                "id": "BASIS-DOC-001",
                "basis_type": "internal_implementation",
                "statement": "Cuplikan README harus cocok dengan skema registry.",
                "reference_ids": ["REF-DOC-001"],
            }
        ],
        "references": [
            {
                "id": "REF-DOC-001",
                "source_type": "source_code",
                "title": "ze_json_research registry schema",
                "url": "https://example.invalid",
                "locator": "src/ze_research/registry.py validate_experiment",
                "accessed_at": "2026-10-30",
                "evidence_note": "Dirujuk untuk memvalidasi cuplikan.",
                "review_status": "reviewed",
            }
        ],
        "metrics": {
            "primary": {
                "name": "median_us_per_op",
                "unit": "us/op",
                "direction": "lower_is_better",
            }
        },
        "protocol": {
            "design": "same_process_sequential",
            "require_same_workload": True,
            "require_same_measurement_contract": True,
        },
        "correctness_gate": {
            "id": "gate-correctness-suite",
            "experiment_id": "correctness-gtest-suite",
            "require_verification_status": "VERIFIED",
            "require_same_commit": True,
        },
        "variants": [
            {
                "id": "assistants",
                "name": "assistants payload",
                "description": "Varian tunggal untuk uji dokumentasi.",
                "comparison_role": "other",
                "lifecycle_status": "experimental",
                "measurement_contract_id": "json-parse-only-v1",
                "benchmark_name_pattern": "JSON.parse/{dataset}",
                "workloads": ["assistants"],
            }
        ],
        "workloads": [
            {
                "id": "assistants",
                "name": "assistants.json",
                "benchmark_token": "assistants",
                "dataset_path": "samples/saq-report/assistants.json",
                "dataset_sha256": (
                    "7de667957e4849c731b86a6c5b2e442b70557aa78b5cfe46d90d9334ed804d5a"
                ),
                "parameters": {},
            }
        ],
        "measurement_contracts": [
            {
                "id": "json-parse-only-v1",
                "name": "JavaScript JSON.parse baseline",
                "operation": "Parse the supplied payload with JSON.parse.",
                "includes": ["repeated JSON.parse calls inside each measured batch"],
                "excludes": ["HTTP transfer and body collection"],
            }
        ],
    },
}


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print("usage: check_readme_snippet.py <README.md>", file=sys.stderr)
        return 2
    readme = Path(argv[0]).expanduser().resolve()
    text = readme.read_text(encoding="utf-8")

    blocks = re.findall(r"```yaml\n(.*?)\n```", text, re.S)
    execution_blocks = [
        block for block in blocks if re.search(r"(?m)^\s*execution:\s*$", block)
    ]
    if not execution_blocks:
        print(f"Tidak ada blok ```yaml dengan 'execution:' di {readme}", file=sys.stderr)
        return 2

    sys.path.insert(0, str(REPO_ROOT / "research" / "src"))
    from ze_research.registry import validate_experiment

    failures = 0
    for index, block in enumerate(execution_blocks, start=1):
        try:
            snippet = yaml.safe_load(block)
        except yaml.YAMLError as exc:
            print(f"[{index}] YAML tidak dapat dibaca: {exc}")
            failures += 1
            continue
        if not isinstance(snippet, dict) or "execution" not in snippet:
            print(f"[{index}] Blok tidak memuat kunci 'execution'.")
            failures += 1
            continue

        document = yaml.safe_load(yaml.safe_dump(BASE_DEFINITION, allow_unicode=True))
        document["experiment"].update(snippet)
        errors = validate_experiment(document)
        errors_for_run = validate_experiment(document, for_run=True)
        if errors or errors_for_run:
            failures += 1
            print(f"[{index}] GAGAL validasi:")
            for message in errors + errors_for_run:
                print(f"  - {message}")
        else:
            keys = ", ".join(sorted(snippet["execution"]))
            print(f"[{index}] OK — kunci execution yang dicatat: {keys}")

    print(f"\n{len(execution_blocks)} cuplikan diperiksa, {failures} gagal.")
    return 1 if failures else 0


if __name__ == "__main__":
    # argparse tidak dipakai; main() urut argv-nya sendiri dari sys.argv.
    raise SystemExit(main(sys.argv[1:]))
