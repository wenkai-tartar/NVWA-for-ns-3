#!/usr/bin/env python3
"""比较 constructor 示例在不同路由算法下生成的 packet trace 是否一致."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Dict, List


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=
        "运行 constructor 示例，比较不同 routing 参数生成的 packet trace 是否一致。"
    )
    parser.add_argument(
        "--config",
        default="clos.json",
        help="传递给 constructor 示例的拓扑配置（默认：clos.json）"
    )
    parser.add_argument(
        "--algos",
        nargs="+",
        default=["RuleBased", "NodeBfs", "NodeBfsWithHost", "Global"],
        help="要测试的路由算法列表（默认同时测试 RuleBased/NodeBfs/NodeBfsWithHost/Global）"
    )
    parser.add_argument(
        "--ns3",
        default="./ns3",
        help="ns-3 包装脚本路径（默认：./ns3）"
    )
    parser.add_argument(
        "--keep-traces",
        action="store_true",
        help="保留已生成的 trace 文件，默认测试后保留"
    )
    return parser.parse_args()


def run_constructor(ns3_path: Path, config: str, algo: str, repo_root: Path) -> None:
    cmd = [
        str(ns3_path),
        "run",
        f"constructor --config={config} --routing={algo} --debug=1",
    ]
    subprocess.run(cmd, cwd=repo_root, check=True)


def main() -> int:
    args = parse_args()
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent.parent.parent
    traces_dir = repo_root / "src/datacenter/examples/traces"
    traces_dir.mkdir(parents=True, exist_ok=True)

    ns3_path = (Path(args.ns3)
                if Path(args.ns3).is_absolute()
                else (repo_root / args.ns3))

    config_path = Path(args.config)
    config_stem = config_path.stem

    trace_map: Dict[str, Path] = {}

    for algo in args.algos:
        trace_file = traces_dir / f"{config_stem}_{algo}_packet_trace.txt"
        if trace_file.exists() and not args.keep_traces:
            trace_file.unlink()

        run_constructor(ns3_path, args.config, algo, repo_root)

        if not trace_file.exists():
            print(f"路由 {algo} 未生成 packet trace：{trace_file}", file=sys.stderr)
            return 1

        trace_map[algo] = trace_file

    algos: List[str] = list(trace_map.keys())
    base_algo = algos[0]
    base_content = trace_map[base_algo].read_text(encoding="utf-8")
    mismatches = []

    for algo in algos[1:]:
        content = trace_map[algo].read_text(encoding="utf-8")
        if content != base_content:
            mismatches.append(algo)

    if mismatches:
        print("以下路由的 packet trace 与 {0} 不一致：{1}".format(
            base_algo, ", ".join(mismatches)),
              file=sys.stderr)
        return 2

    print("所有路由算法的 packet trace 完全一致。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
