#!/usr/bin/env python3
"""Ensure Morpho example files import bytecodeoptimizer.

This script walks an examples tree looking for .morpho files and inserts:

    import bytecodeoptimizer

when the file does not already contain that import. Imports are inserted after
an initial shebang/comment prelude and before the first code line.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


DEFAULT_SOURCE_ROOT = Path("test") / "examples"
IMPORT_LINE = "import bytecodeoptimizer"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Add 'import bytecodeoptimizer' to Morpho example files when missing."
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=DEFAULT_SOURCE_ROOT,
        help="Root directory to scan for .morpho files. Defaults to 'test/examples'.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Report files that would be modified without writing changes.",
    )
    return parser.parse_args()


def discover_inputs(source_root: Path) -> list[Path]:
    return sorted(path for path in source_root.rglob("*.morpho") if path.is_file())


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

    source_root = args.source_root.resolve()
    if not source_root.exists():
        print(f"Source root does not exist: {source_root}", file=sys.stderr)
        return 2

    inputs = discover_inputs(source_root)
    if not inputs:
        print(f"No .morpho files found under {source_root}", file=sys.stderr)
        return 2

    modified = 0
    print(f"Scanning {source_root}")

    for path in inputs:
        rel = path.relative_to(source_root)
        if add_import(path, args.dry_run):
            modified += 1
            action = "would update" if args.dry_run else "updated"
            print(f"{action}: {rel}")

    if args.dry_run:
        print(f"{modified} file(s) would be updated.")
    else:
        print(f"{modified} file(s) updated.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
