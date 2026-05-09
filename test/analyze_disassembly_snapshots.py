#!/usr/bin/env python3
"""Analyze before/after bytecode disassembly snapshots for Morpho tests.

This script walks a snapshot tree produced by ``generate_disassembly_snapshots.py``
and counts emitted opcodes in:

    *.baseline.txt
    *.optimized.txt

It produces aggregate totals plus baseline/optimized deltas. A focused opcode
list can be supplied to track patterns such as ``invoke -> method`` and
``lix -> lixl``.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


DEFAULT_SNAPSHOT_ROOT = Path("test") / "disassembly_snapshots"
DEFAULT_FOCUS_OPCODES = [
    "invoke",
    "method",
    "call",
    "lix",
    "lixl",
    "lgl",
    "sgl",
    "lpr",
    "spr",
    "six",
]

INSTRUCTION_RE = re.compile(r"^\s*\d+\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\b")


@dataclass(frozen=True)
class SnapshotInfo:
    path: Path
    kind: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Analyze Morpho disassembly snapshots generated with and without optimization."
    )
    parser.add_argument(
        "--snapshot-root",
        type=Path,
        default=DEFAULT_SNAPSHOT_ROOT,
        help="Root directory containing snapshot files. Defaults to 'test/disassembly_snapshots'.",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=20,
        help="Number of largest positive/negative opcode deltas to display. Defaults to 20.",
    )
    parser.add_argument(
        "--focus-opcode",
        dest="focus_opcodes",
        action="append",
        default=[],
        help="Opcode to highlight explicitly. May be passed multiple times.",
    )
    parser.add_argument(
        "--csv",
        type=Path,
        help="Optional path to write a CSV report of opcode counts and deltas.",
    )
    return parser.parse_args()


def discover_snapshots(snapshot_root: Path) -> list[SnapshotInfo]:
    snapshots: list[SnapshotInfo] = []
    for path in sorted(snapshot_root.rglob("*.txt")):
        if not path.is_file():
            continue
        if path.name.endswith(".baseline.txt"):
            snapshots.append(SnapshotInfo(path=path, kind="baseline"))
        elif path.name.endswith(".optimized.txt"):
            snapshots.append(SnapshotInfo(path=path, kind="optimized"))
    return snapshots


def count_opcodes(path: Path) -> Counter[str]:
    counts: Counter[str] = Counter()
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = INSTRUCTION_RE.match(line)
            if match:
                counts[match.group(1)] += 1
    return counts


def format_delta(delta: int) -> str:
    return f"{delta:+d}"


def write_csv(path: Path, baseline: Counter[str], optimized: Counter[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    opcodes = sorted(set(baseline) | set(optimized))
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["opcode", "baseline", "optimized", "delta"])
        for opcode in opcodes:
            b = baseline[opcode]
            o = optimized[opcode]
            writer.writerow([opcode, b, o, o - b])


def print_focus_table(baseline: Counter[str], optimized: Counter[str], focus_opcodes: list[str]) -> None:
    if not focus_opcodes:
        return

    print("Focus opcodes:")
    print(f"{'opcode':<10} {'baseline':>10} {'optimized':>10} {'delta':>10}")
    for opcode in focus_opcodes:
        b = baseline[opcode]
        o = optimized[opcode]
        print(f"{opcode:<10} {b:>10} {o:>10} {format_delta(o - b):>10}")
    print()


def print_delta_table(
    title: str,
    rows: list[tuple[str, int, int, int]],
) -> None:
    print(title)
    print(f"{'opcode':<10} {'baseline':>10} {'optimized':>10} {'delta':>10}")
    for opcode, baseline_count, optimized_count, delta in rows:
        print(f"{opcode:<10} {baseline_count:>10} {optimized_count:>10} {format_delta(delta):>10}")
    print()


def main() -> int:
    args = parse_args()

    snapshot_root = args.snapshot_root.resolve()
    if not snapshot_root.exists():
        print(f"Snapshot root does not exist: {snapshot_root}", file=sys.stderr)
        return 2

    snapshots = discover_snapshots(snapshot_root)
    if not snapshots:
        print(f"No snapshot files found under {snapshot_root}", file=sys.stderr)
        return 2

    baseline_totals: Counter[str] = Counter()
    optimized_totals: Counter[str] = Counter()

    baseline_files = 0
    optimized_files = 0

    for snapshot in snapshots:
        counts = count_opcodes(snapshot.path)
        if snapshot.kind == "baseline":
            baseline_totals.update(counts)
            baseline_files += 1
        else:
            optimized_totals.update(counts)
            optimized_files += 1

    all_opcodes = sorted(set(baseline_totals) | set(optimized_totals))
    rows = [
        (
            opcode,
            baseline_totals[opcode],
            optimized_totals[opcode],
            optimized_totals[opcode] - baseline_totals[opcode],
        )
        for opcode in all_opcodes
    ]

    baseline_total = sum(baseline_totals.values())
    optimized_total = sum(optimized_totals.values())
    total_delta = optimized_total - baseline_total
    total_ratio = (optimized_total / baseline_total) if baseline_total else 0.0

    focus = DEFAULT_FOCUS_OPCODES.copy()
    for opcode in args.focus_opcodes:
        if opcode not in focus:
            focus.append(opcode)

    print(f"Snapshot root: {snapshot_root}")
    print(f"Baseline files: {baseline_files}")
    print(f"Optimized files: {optimized_files}")
    print(f"Unique opcodes: {len(all_opcodes)}")
    print(f"Baseline total instructions: {baseline_total}")
    print(f"Optimized total instructions: {optimized_total}")
    print(f"Overall instruction delta: {format_delta(total_delta)}")
    print(f"Optimized/baseline ratio: {total_ratio:.3f}")
    print()

    print_focus_table(baseline_totals, optimized_totals, focus)

    top = max(args.top, 0)
    positive = sorted(rows, key=lambda row: row[3], reverse=True)[:top]
    negative = sorted(rows, key=lambda row: row[3])[:top]

    print_delta_table("Largest positive deltas:", positive)
    print_delta_table("Largest negative deltas:", negative)

    if args.csv:
        write_csv(args.csv.resolve(), baseline_totals, optimized_totals)
        print(f"Wrote CSV report to {args.csv.resolve()}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
