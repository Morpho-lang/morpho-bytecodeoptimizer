#!/usr/bin/env python3
"""Analyze register usage in before/after bytecode disassembly snapshots.

This walks a snapshot tree produced by ``generate_disassembly_snapshots.py`` and
measures register-reference patterns in:

    *.baseline.txt
    *.optimized.txt

It reports:

    - total register references
    - mean referenced register index
    - per-file maximum register index statistics
    - most frequently referenced registers
    - share of references contained in low register windows
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


DEFAULT_SNAPSHOT_ROOT = Path("test") / "disassembly_snapshots"
REGISTER_RE = re.compile(r"r(\d+)")


@dataclass(frozen=True)
class SnapshotInfo:
    path: Path
    kind: str


@dataclass(frozen=True)
class FileRegisterStats:
    max_reg: int
    total_refs: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Analyze register usage in Morpho disassembly snapshots."
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
        default=12,
        help="Number of top referenced registers to display. Defaults to 12.",
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


def percentile(sorted_values: list[int], p: float) -> int:
    if not sorted_values:
        return 0
    idx = int(p * (len(sorted_values) - 1))
    return sorted_values[idx]


def analyze_kind(paths: list[Path]) -> dict[str, object]:
    hist: Counter[int] = Counter()
    file_maxima: list[int] = []

    for path in paths:
        max_reg = -1
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                for match in REGISTER_RE.finditer(line):
                    reg = int(match.group(1))
                    hist[reg] += 1
                    if reg > max_reg:
                        max_reg = reg
        file_maxima.append(max_reg if max_reg >= 0 else 0)

    file_maxima.sort()
    total_refs = sum(hist.values())
    weighted_sum = sum(reg * count for reg, count in hist.items())

    return {
        "files": len(paths),
        "hist": hist,
        "file_maxima": file_maxima,
        "total_refs": total_refs,
        "mean_reg_ref": (weighted_sum / total_refs) if total_refs else 0.0,
        "avg_file_max": (sum(file_maxima) / len(file_maxima)) if file_maxima else 0.0,
        "median_file_max": percentile(file_maxima, 0.5),
        "p90_file_max": percentile(file_maxima, 0.9),
        "p99_file_max": percentile(file_maxima, 0.99),
        "max_file_max": file_maxima[-1] if file_maxima else 0,
    }


def analyze_file(path: Path) -> FileRegisterStats:
    max_reg = -1
    total_refs = 0

    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            for match in REGISTER_RE.finditer(line):
                reg = int(match.group(1))
                total_refs += 1
                if reg > max_reg:
                    max_reg = reg

    return FileRegisterStats(max_reg=max_reg if max_reg >= 0 else 0, total_refs=total_refs)


def snapshot_stem(snapshot: SnapshotInfo, snapshot_root: Path) -> str:
    suffix = f".{snapshot.kind}.txt"
    relative = snapshot.path.relative_to(snapshot_root)
    relative_str = relative.as_posix()
    return relative_str[: -len(suffix)]


def compare_file_maxima(snapshots: list[SnapshotInfo], snapshot_root: Path) -> dict[str, object]:
    by_stem: dict[str, dict[str, Path]] = {}
    for snapshot in snapshots:
        by_stem.setdefault(snapshot_stem(snapshot, snapshot_root), {})[snapshot.kind] = snapshot.path

    deltas: list[int] = []
    decreased = 0
    unchanged = 0
    increased = 0
    baseline_only = 0
    optimized_only = 0

    delta_hist: Counter[int] = Counter()

    for stem, paths in by_stem.items():
        baseline = paths.get("baseline")
        optimized = paths.get("optimized")
        if baseline and optimized:
            baseline_stats = analyze_file(baseline)
            optimized_stats = analyze_file(optimized)
            delta = optimized_stats.max_reg - baseline_stats.max_reg
            deltas.append(delta)
            delta_hist[delta] += 1
            if delta < 0:
                decreased += 1
            elif delta > 0:
                increased += 1
            else:
                unchanged += 1
        elif baseline:
            baseline_only += 1
        elif optimized:
            optimized_only += 1

    deltas.sort()
    return {
        "matched_files": len(deltas),
        "baseline_only": baseline_only,
        "optimized_only": optimized_only,
        "decreased": decreased,
        "unchanged": unchanged,
        "increased": increased,
        "mean_delta": (sum(deltas) / len(deltas)) if deltas else 0.0,
        "median_delta": percentile(deltas, 0.5),
        "p10_delta": percentile(deltas, 0.1),
        "p90_delta": percentile(deltas, 0.9),
        "min_delta": deltas[0] if deltas else 0,
        "max_delta": deltas[-1] if deltas else 0,
        "delta_hist": delta_hist,
    }


def print_report(kind: str, stats: dict[str, object], top: int) -> None:
    hist: Counter[int] = stats["hist"]  # type: ignore[assignment]
    total_refs = int(stats["total_refs"])
    print(kind.upper())
    print(
        "files={files} total_refs={total_refs} mean_reg_ref={mean_reg_ref:.3f} "
        "avg_file_max={avg_file_max:.3f} median_file_max={median_file_max} "
        "p90_file_max={p90_file_max} p99_file_max={p99_file_max} max_file_max={max_file_max}".format(
            **stats
        )
    )
    print()

    print("Top register references:")
    print(f"{'register':<10} {'count':>12} {'share':>10}")
    for reg, count in hist.most_common(top):
        share = (count / total_refs) if total_refs else 0.0
        print(f"{('r' + str(reg)):<10} {count:>12} {share:>9.3%}")
    print()

    print("Low register share:")
    for cutoff in (4, 8, 12, 16, 24, 32):
        subtotal = sum(count for reg, count in hist.items() if reg <= cutoff)
        share = (subtotal / total_refs) if total_refs else 0.0
        print(f"r0..r{cutoff:<2} {share:>9.3%}")
    print()

    print("Per-file max register tail:")
    max_hist = Counter(stats["file_maxima"])  # type: ignore[arg-type]
    for reg, count in sorted(max_hist.items())[-15:]:
        print(f"maxr{reg:<4} {count:>6}")
    print()


def print_comparison_report(stats: dict[str, object]) -> None:
    delta_hist: Counter[int] = stats["delta_hist"]  # type: ignore[assignment]

    print("PER-FILE MAX REGISTER DELTAS (optimized - baseline)")
    print(
        "matched_files={matched_files} baseline_only={baseline_only} optimized_only={optimized_only} "
        "decreased={decreased} unchanged={unchanged} increased={increased}".format(
            **stats
        )
    )
    print(
        "mean_delta={mean_delta:.3f} median_delta={median_delta} p10_delta={p10_delta} "
        "p90_delta={p90_delta} min_delta={min_delta} max_delta={max_delta}".format(
            **stats
        )
    )
    print()

    print("Most common max-register deltas:")
    print(f"{'delta':<10} {'files':>12}")
    for delta, count in sorted(delta_hist.items(), key=lambda item: (-item[1], item[0]))[:15]:
        print(f"{delta:<10} {count:>12}")
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

    baseline_paths = [s.path for s in snapshots if s.kind == "baseline"]
    optimized_paths = [s.path for s in snapshots if s.kind == "optimized"]

    print(f"Snapshot root: {snapshot_root}")
    print()

    print_report("baseline", analyze_kind(baseline_paths), max(args.top, 0))
    print_report("optimized", analyze_kind(optimized_paths), max(args.top, 0))
    print_comparison_report(compare_file_maxima(snapshots, snapshot_root))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
