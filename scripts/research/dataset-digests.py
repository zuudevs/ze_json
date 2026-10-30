#!/usr/bin/env python3
"""Compute SHA-256 digests for ze_json sample datasets.

The experiment registry in ze_json_research declares a ``dataset_sha256`` for
every workload. Those values must come from the files that were actually
measured, not from a remembered number. This script is the single source of
truth for that value: it walks a sample directory, hashes every regular file,
and writes a deterministic JSON manifest.

Usage (from the ze_json repository root):

    python scripts/research/dataset-digests.py
    python scripts/research/dataset-digests.py samples/commons
    python scripts/research/dataset-digests.py --output /tmp/digests.json

The output is sorted by relative path and contains no timestamps, so two runs
against the same tree produce byte-identical output.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_manifest(root: Path, directory: Path) -> dict[str, object]:
    entries: list[dict[str, object]] = []
    for path in sorted(directory.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        entries.append(
            {
                "path": path.relative_to(root).as_posix(),
                "name": path.name,
                "sha256": sha256_file(path),
                "size_bytes": path.stat().st_size,
            }
        )

    # Digest over the whole set: identifies the dataset collection as a unit,
    # which is what a workload digest in the registry should be pinned to.
    formula = "\n".join(
        f"{entry['path']}\t{entry['sha256']}\t{entry['size_bytes']}" for entry in entries
    )
    return {
        "schema_version": 1,
        "generator": "scripts/research/dataset-digests.py",
        "source_dir": directory.relative_to(root).as_posix(),
        "file_count": len(entries),
        "set_sha256": hashlib.sha256(formula.encode("utf-8")).hexdigest(),
        "files": entries,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "directory",
        nargs="?",
        default="samples/commons",
        help="sample directory to hash, relative to the repository root",
    )
    parser.add_argument(
        "--repo-root",
        default=None,
        help="ze_json repository root; defaults to the parent of scripts/research",
    )
    parser.add_argument(
        "--output",
        default="scripts/research/dataset-digests.json",
        help="output path; defaults to scripts/research/dataset-digests.json",
    )
    args = parser.parse_args(argv)

    root = (
        Path(args.repo_root).expanduser().resolve()
        if args.repo_root
        else Path(__file__).resolve().parents[2]
    )
    directory = (root / args.directory).resolve()
    if not directory.is_dir():
        print(f"ERROR: sample directory not found: {directory}", file=sys.stderr)
        return 2

    manifest = build_manifest(root, directory)
    destination = (
        Path(args.output).expanduser().resolve() if args.output else root / "scripts/research/dataset-digests.json"
    )
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"{manifest['file_count']} file -> {destination}\n"
        f"set_sha256={manifest['set_sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
