#!/usr/bin/env python3
"""Generate before/after bytecode disassembly snapshots for Morpho tests.

This script walks a source tree looking for .morpho files and runs:

    morpho6 -D <file>
    morpho6 -D -O <file>

It writes the resulting disassemblies into a single snapshot root under the
test directory while preserving the relative source layout. A CSV manifest is
also emitted for quick review.
"""

from __future__ import annotations

import argparse
import csv
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_SOURCE_ROOT = Path("test")
DEFAULT_OUTPUT_ROOT = Path("test") / "disassembly_snapshots"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate Morpho disassembly snapshots with and without optimization."
    )
    parser.add_argument(
        "--morpho",
        default="morpho6",
        help="Morpho executable to invoke. Defaults to 'morpho6'.",
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=DEFAULT_SOURCE_ROOT,
        help="Root directory to scan for .morpho files. Defaults to 'test'.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=DEFAULT_OUTPUT_ROOT,
        help="Directory to store snapshots. Defaults to 'test/disassembly_snapshots'.",
    )
    parser.add_argument(
        "--keep-stale",
        action="store_true",
        help="Keep existing files in the output directory instead of clearing it first.",
    )
    parser.add_argument(
        "--fail-fast",
        action="store_true",
        help="Stop immediately on the first command failure.",
    )
    return parser.parse_args()


def discover_inputs(source_root: Path) -> list[Path]:
    return sorted(path for path in source_root.rglob("*.morpho") if path.is_file())


def clear_output_root(output_root: Path) -> None:
    if output_root.exists():
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def relative_stem(path: Path, root: Path) -> Path:
    rel = path.relative_to(root)
    return rel.parent / rel.stem


def run_disassembly(morpho: str, src: Path, optimized: bool) -> subprocess.CompletedProcess[str]:
    cmd = [morpho, "-D"]
    if optimized:
        cmd.append("-O")
    cmd.append(str(src))

    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def emit_snapshot(
    output_root: Path,
    rel_stem: Path,
    label: str,
    proc: subprocess.CompletedProcess[str],
) -> Path:
    out_path = output_root / rel_stem.parent / f"{rel_stem.name}.{label}.txt"
    text = proc.stdout
    if proc.stderr:
        text += "\n=== STDERR ===\n" + proc.stderr
    if proc.returncode != 0:
        text += f"\n=== EXIT CODE: {proc.returncode} ===\n"
    write_text(out_path, text)
    return out_path


def write_manifest(output_root: Path, rows: list[dict[str, str]]) -> None:
    manifest = output_root / "manifest.csv"
    with manifest.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "source",
                "baseline_snapshot",
                "optimized_snapshot",
                "baseline_exit_code",
                "optimized_exit_code",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()

    source_root = args.source_root.resolve()
    output_root = args.output_root.resolve()

    if not source_root.exists():
        print(f"Source root does not exist: {source_root}", file=sys.stderr)
        return 2

    inputs = discover_inputs(source_root)
    if not inputs:
        print(f"No .morpho files found under {source_root}", file=sys.stderr)
        return 2

    if not args.keep_stale:
        clear_output_root(output_root)
    else:
        output_root.mkdir(parents=True, exist_ok=True)

    manifest_rows: list[dict[str, str]] = []
    failures = 0

    print(f"Scanning {source_root}")
    print(f"Writing snapshots to {output_root}")
    print(f"Using Morpho executable: {args.morpho}")

    for src in inputs:
        rel = src.relative_to(source_root)
        rel_stem = relative_stem(src, source_root)

        print(f"[{len(manifest_rows)+1}/{len(inputs)}] {rel}")

        baseline = run_disassembly(args.morpho, src, optimized=False)
        optimized = run_disassembly(args.morpho, src, optimized=True)

        baseline_path = emit_snapshot(output_root, rel_stem, "baseline", baseline)
        optimized_path = emit_snapshot(output_root, rel_stem, "optimized", optimized)

        if baseline.returncode != 0 or optimized.returncode != 0:
            failures += 1
            print(
                f"  command failure: baseline={baseline.returncode} optimized={optimized.returncode}",
                file=sys.stderr,
            )
            if args.fail_fast:
                manifest_rows.append(
                    {
                        "source": str(rel),
                        "baseline_snapshot": str(baseline_path.relative_to(output_root)),
                        "optimized_snapshot": str(optimized_path.relative_to(output_root)),
                        "baseline_exit_code": str(baseline.returncode),
                        "optimized_exit_code": str(optimized.returncode),
                    }
                )
                break

        manifest_rows.append(
            {
                "source": str(rel),
                "baseline_snapshot": str(baseline_path.relative_to(output_root)),
                "optimized_snapshot": str(optimized_path.relative_to(output_root)),
                "baseline_exit_code": str(baseline.returncode),
                "optimized_exit_code": str(optimized.returncode),
            }
        )

    write_manifest(output_root, manifest_rows)

    print(f"Wrote {len(manifest_rows)} snapshot pairs.")
    if failures:
        print(f"{failures} file(s) had command failures.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
