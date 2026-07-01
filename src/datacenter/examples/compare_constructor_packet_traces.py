#!/usr/bin/env python3
"""Compare packet trace outputs for constructor runs (optionally with failures)."""

from __future__ import annotations

import argparse
import subprocess
import sys
from itertools import zip_longest
from pathlib import Path
from typing import Iterable, Optional


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run constructor (optional) and compare packet traces across routings."
    )
    parser.add_argument(
        "--config",
        required=True,
        help="Topology config passed to constructor (e.g., fattree_k4.json)",
    )
    parser.add_argument(
        "--failure",
        default="",
        help="Failure config passed to constructor (optional)",
    )
    parser.add_argument(
        "--algos",
        nargs="+",
        default=["RuleBased", "NodeBfsStrict"],
        help="Routing algorithms to compare (default: RuleBased NodeBfsStrict)",
    )
    parser.add_argument(
        "--ns3",
        default="./ns3",
        help="ns-3 wrapper path (default: ./ns3)",
    )
    parser.add_argument(
        "--randomFailureRate",
        type=float,
        default=0.0,
        help="Random failure ratio over links (0-1).",
    )
    parser.add_argument(
        "--randomFailureTime",
        type=float,
        default=0.5,
        help="Random failure trigger time value.",
    )
    parser.add_argument(
        "--randomFailureTimeUnit",
        default="s",
        help="Random failure time unit (s/ms/us/ns).",
    )
    parser.add_argument(
        "--randomFailureSeed",
        type=int,
        default=1,
        help="Random failure RNG seed.",
    )
    parser.add_argument(
        "--skip-run",
        action="store_true",
        help="Skip running constructor; only compare existing traces.",
    )
    parser.add_argument(
        "--no-ignore-comments",
        action="store_true",
        help="Include comment/header lines starting with '#'.",
    )
    parser.add_argument(
        "--time-from",
        type=float,
        default=None,
        help="Only compare lines with time >= this value (seconds).",
    )
    parser.add_argument(
        "--time-to",
        type=float,
        default=None,
        help="Only compare lines with time <= this value (seconds).",
    )
    return parser.parse_args()


def run_constructor(
    ns3_path: Path,
    config: str,
    failure: str,
    algo: str,
    repo_root: Path,
    random_rate: float,
    random_time: float,
    random_unit: str,
    random_seed: int,
) -> None:
    cmd = [str(ns3_path), "run", f"constructor --config={config} --routing={algo} --debug=1"]
    if failure:
        cmd[-1] += f" --failure={failure}"
    if random_rate and random_rate > 0.0:
        cmd[-1] += (
            f" --randomFailureRate={random_rate}"
            f" --randomFailureTime={random_time}"
            f" --randomFailureTimeUnit={random_unit}"
            f" --randomFailureSeed={random_seed}"
        )
    subprocess.run(cmd, cwd=repo_root, check=True)


def trace_path(traces_dir: Path, config: str, algo: str, failure: str) -> Path:
    config_stem = Path(config).stem
    failure_suffix = f"_{Path(failure).stem}" if failure else ""
    return traces_dir / f"{config_stem}_{algo}{failure_suffix}_packet_trace.txt"


def iter_lines(
    path: Path,
    ignore_comments: bool,
    time_from: Optional[float],
    time_to: Optional[float],
) -> Iterable[str]:
    with path.open(encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if ignore_comments and line.lstrip().startswith("#"):
                continue
            if not line:
                continue
            if time_from is not None or time_to is not None:
                head = line.split("\t", 1)[0].strip()
                try:
                    t = float(head)
                except ValueError:
                    pass
                else:
                    if time_from is not None and t < time_from:
                        continue
                    if time_to is not None and t > time_to:
                        continue
            yield line


def compare_files(
    left: Path,
    right: Path,
    ignore_comments: bool,
    time_from: Optional[float],
    time_to: Optional[float],
) -> tuple[bool, int, Optional[str], Optional[str]]:
    it_left = iter_lines(left, ignore_comments, time_from, time_to)
    it_right = iter_lines(right, ignore_comments, time_from, time_to)
    for idx, (l_line, r_line) in enumerate(zip_longest(it_left, it_right), start=1):
        if l_line != r_line:
            return False, idx, l_line, r_line
    return True, 0, None, None


def clip(line: Optional[str], limit: int = 400) -> str:
    if line is None:
        return "<EOF>"
    if len(line) <= limit:
        return line
    return line[:limit] + " ...[truncated]"


def main() -> int:
    args = parse_args()

    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent.parent.parent
    traces_dir = repo_root / "src/datacenter/examples/traces"
    traces_dir.mkdir(parents=True, exist_ok=True)

    ns3_path = Path(args.ns3)
    if not ns3_path.is_absolute():
        ns3_path = repo_root / ns3_path

    if not args.skip_run:
        for algo in args.algos:
            run_constructor(
                ns3_path,
                args.config,
                args.failure,
                algo,
                repo_root,
                args.randomFailureRate,
                args.randomFailureTime,
                args.randomFailureTimeUnit,
                args.randomFailureSeed,
            )

    trace_map = {}
    for algo in args.algos:
        path = trace_path(traces_dir, args.config, algo, args.failure)
        if not path.exists():
            print(f"Trace not found for {algo}: {path}", file=sys.stderr)
            return 1
        trace_map[algo] = path

    base_algo = args.algos[0]
    base_path = trace_map[base_algo]
    mismatches = []

    ignore_comments = not args.no_ignore_comments
    for algo in args.algos[1:]:
        ok, line_no, left, right = compare_files(
            base_path, trace_map[algo], ignore_comments, args.time_from, args.time_to
        )
        if not ok:
            mismatches.append((algo, line_no, left, right))

    if mismatches:
        print(f"Packet trace mismatch vs {base_algo}:")
        for algo, line_no, left, right in mismatches:
            print(f"- {algo}: line {line_no}")
            print(f"  {base_algo}: {clip(left)}")
            print(f"  {algo}: {clip(right)}")
        return 2

    print("All packet traces match.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
