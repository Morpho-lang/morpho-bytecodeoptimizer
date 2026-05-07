#!/usr/bin/env python3
"""Ensure Morpho source files import bytecodeoptimizer.

This script walks one or more source trees looking for .morpho files and
inserts:

    import bytecodeoptimizer

when the file does not already contain that import. Imports are inserted after
an initial shebang/comment prelude and before the first code line.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


DEFAULT_SOURCE_ROOTS = [Path("test") / "examples", Path("test") / "benchmarks"]
IMPORT_LINE = "import bytecodeoptimizer"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Add 'import bytecodeoptimizer' to Morpho source files when missing."
    )
    parser.add_argument(
        "--source-root",
        dest="source_roots",
        action="append",
        type=Path,
        help=(
            "Root directory to scan for .morpho files. "
            "May be supplied multiple times. Defaults to 'test/examples' and 'test/benchmarks'."
        ),
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Report files that would be modified without writing changes.",
    )
    return parser.parse_args()


def discover_inputs(source_roots: list[Path]) -> list[tuple[Path, Path]]:
    inputs: list[tuple[Path, Path]] = []

    for source_root in source_roots:
        for path in sorted(source_root.rglob("*.morpho")):
            if path.is_file():
                inputs.append((source_root, path))

    return inputs


def has_optimizer_import(lines: list[str]) -> bool:
    return any(line.strip() == IMPORT_LINE for line in lines)


def insertion_index(lines: list[str]) -> int:
    index = 0

    while index < len(lines):
        stripped = lines[index].strip()

        if index == 0 and stripped.startswith("#!"):
            index += 1
            continue
        if stripped == "" or stripped.startswith("#"):
            index += 1
            continue
        break

    return index


def add_import(path: Path, dry_run: bool) -> bool:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()

    if has_optimizer_import(lines):
        return False

    index = insertion_index(lines)
    lines.insert(index, IMPORT_LINE)
    new_text = "\n".join(lines) + ("\n" if text.endswith("\n") or lines else "")

    if not dry_run:
        path.write_text(new_text, encoding="utf-8")

    return True


def main() -> int:
    args = parse_args()
    source_roots = [path.resolve() for path in (args.source_roots or DEFAULT_SOURCE_ROOTS)]

    missing = [path for path in source_roots if not path.exists()]
    if missing:
        for path in missing:
            print(f"Source root does not exist: {path}", file=sys.stderr)
        return 2

    inputs = discover_inputs(source_roots)
    if not inputs:
        print("No .morpho files found under the selected source roots.", file=sys.stderr)
        return 2

    modified = 0
    print("Scanning:")
    for source_root in source_roots:
        print(f"  {source_root}")

    for source_root, path in inputs:
        if add_import(path, args.dry_run):
            modified += 1
            action = "would update" if args.dry_run else "updated"
            print(f"{action}: {path.relative_to(source_root)}")

    if args.dry_run:
        print(f"{modified} file(s) would be updated.")
    else:
        print(f"{modified} file(s) updated.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
