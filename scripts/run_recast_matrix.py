#!/usr/bin/env python3
"""Run RECAST over a dataset/workload matrix.

This script is intentionally small and RECAST-only.  It generates query files
from data objects, runs the final L2/QFD binary, and writes compact TSV/JSON
summaries under `run_outputs/`.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import ctypes
import json
import math
from pathlib import Path
import subprocess
import time
from typing import Any

import numpy as np

DATASETS = {}

QFD_DATASETS = set()
WORKLOADS = ["fixed_B", "jump_A_to_B", "drift_A_to_B", "three_jump_A_B_C"]


def read_data(path: Path) -> np.ndarray:
    with path.open() as handle:
        header = handle.readline().split()
        dim = int(header[0])
        data = np.loadtxt(handle, dtype=np.float32)
    if data.ndim != 2 or data.shape[1] != dim:
        raise ValueError(f"bad data shape for {path}: {data.shape}, expected dim {dim}")
    return data


def write_queries(path: Path, centers: np.ndarray, radius: float) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as handle:
        handle.write(f"{len(centers)}\n")
        for row in centers:
            handle.write(f"{radius:.9f} " + " ".join(f"{float(x):.9f}" for x in row) + "\n")


def qfd_matrix_np(dim: int, seed: int = 54321) -> np.ndarray:
    # Matches the deterministic QFD matrix construction used by the C++ binary.
    libc = ctypes.CDLL(None)
    libc.srand(seed + 12345)
    rand_max = 2147483647.0
    random_matrix = np.empty((dim, dim), dtype=np.float32)
    for i in range(dim):
        for j in range(dim):
            random_matrix[i, j] = (float(libc.rand()) / rand_max - 0.5) * 2.0
    return (random_matrix.T @ random_matrix).astype(np.float32)


def metric_dists(data: np.ndarray, center: np.ndarray, metric: str, qfd: np.ndarray | None) -> np.ndarray:
    if metric == "qfd":
        if qfd is None:
            raise ValueError("qfd matrix required")
        diff = data - center
        vals = np.einsum("ij,jk,ik->i", diff, qfd, diff, optimize=True)
        return np.sqrt(np.maximum(vals, 0.0)).astype(np.float32)
    return np.linalg.norm(data - center, axis=1)


def far_apart_pools(data: np.ndarray, seed: int, metric: str, qfd: np.ndarray | None, pool_size: int = 5000):
    rng = np.random.default_rng(seed)
    sample_ids = rng.choice(data.shape[0], size=min(2000, data.shape[0]), replace=False)
    first = int(sample_ids[0])
    a_center = int(sample_ids[int(np.argmax(metric_dists(data[sample_ids], data[first], metric, qfd)))])
    b_center = int(sample_ids[int(np.argmax(metric_dists(data[sample_ids], data[a_center], metric, qfd)))])
    take = min(pool_size, data.shape[0])
    a = np.argpartition(metric_dists(data, data[a_center], metric, qfd), take - 1)[:take]
    b = np.argpartition(metric_dists(data, data[b_center], metric, qfd), take - 1)[:take]
    return a, b


def three_pools(data: np.ndarray, seed: int, metric: str, qfd: np.ndarray | None, pool_size: int = 5000):
    a, b = far_apart_pools(data, seed, metric, qfd, pool_size)
    rng = np.random.default_rng(seed + 919)
    sample = rng.choice(data.shape[0], size=min(3000, data.shape[0]), replace=False)
    da = metric_dists(data[sample], data[int(a[0])], metric, qfd)
    db = metric_dists(data[sample], data[int(b[0])], metric, qfd)
    c_center = int(sample[int(np.argmax(np.minimum(da, db)))])
    take = min(pool_size, data.shape[0])
    c = np.argpartition(metric_dists(data, data[c_center], metric, qfd), take - 1)[:take]
    return a, b, c


def choose(data: np.ndarray, pool: np.ndarray, count: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return data[rng.choice(pool, size=count, replace=True)].copy()


def drift(data: np.ndarray, a: np.ndarray, b: np.ndarray, count: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    out = []
    denom = max(1, count - 1)
    for idx in range(count):
        pool = b if rng.random() < idx / denom else a
        out.append(data[int(rng.choice(pool))])
    return np.asarray(out, dtype=np.float32)


def calibrate_radius(data: np.ndarray, centers: np.ndarray, selectivity: float, seed: int, metric: str, qfd: np.ndarray | None) -> float:
    rng = np.random.default_rng(seed)
    point_ids = rng.choice(data.shape[0], size=min(10_000, data.shape[0]), replace=False)
    query_ids = rng.choice(centers.shape[0], size=min(40, centers.shape[0]), replace=False)
    sampled = data[point_ids]
    target = max(1, int(round(selectivity * sampled.shape[0]))) - 1
    kth = []
    for qid in query_ids:
        ds = metric_dists(sampled, centers[int(qid)], metric, qfd)
        kth.append(float(np.partition(ds, target)[target]))
    kth.sort()
    return kth[len(kth) // 2]


def prepare_workloads(dataset: str, data: np.ndarray, out_dir: Path, queries: int, selectivity: float, seed: int) -> dict[str, Path]:
    metric = "qfd" if dataset in QFD_DATASETS else "l2"
    qfd = qfd_matrix_np(data.shape[1]) if metric == "qfd" else None
    a, b, c = three_pools(data, seed + data.shape[1], metric, qfd)
    half = queries // 2
    third = queries // 3
    workloads = {
        "fixed_B": choose(data, b, queries, seed + 1000),
        "jump_A_to_B": np.vstack([choose(data, a, half, seed + 2000), choose(data, b, queries - half, seed + 3000)]).astype(np.float32),
        "drift_A_to_B": drift(data, a, b, queries, seed + 4000),
        "three_jump_A_B_C": np.vstack([
            choose(data, a, third, seed + 5000),
            choose(data, b, third, seed + 6000),
            choose(data, c, queries - 2 * third, seed + 7000),
        ]).astype(np.float32),
    }
    radius = calibrate_radius(data, np.vstack(list(workloads.values())), selectivity, seed + 8000, metric, qfd)
    paths = {}
    for name, centers in workloads.items():
        path = out_dir / f"{dataset}_{name}_q{queries}_s{seed}.txt"
        write_queries(path, centers, radius)
        paths[name] = path
    (out_dir / f"{dataset}_meta_s{seed}.json").write_text(json.dumps({
        "dataset": dataset,
        "queries": queries,
        "selectivity": selectivity,
        "radius": radius,
        "metric": metric,
        "seed": seed,
    }, indent=2))
    return paths


def parse_rows(stdout: str) -> list[dict[str, Any]]:
    rows = []
    in_table = False
    for line in stdout.splitlines():
        if line.startswith("Results"):
            in_table = True
            continue
        if not in_table:
            continue
        parts = line.strip().split()
        if len(parts) < 3 or not parts[0].lstrip("-").isdigit():
            continue
        rows.append({
            "result": int(parts[0]),
            "query_time_s": float(parts[1]),
            "query_ms": float(parts[1]) * 1000.0,
            "dc": int(float(parts[2])),
            "filtered": int(float(parts[3])) if len(parts) > 3 else 0,
            "leaf_points": int(float(parts[4])) if len(parts) > 4 else 0,
        })
    if not rows:
        raise RuntimeError("no result rows parsed from RECAST output")
    return rows


def run_one(root: Path, binary: Path, dataset: str, data_path: Path, query_path: Path, workload: str, seed: int, out_root: Path, force: bool):
    case_dir = out_root / dataset / workload / f"seed_{seed}"
    json_path = case_dir / "recast.json"
    if json_path.exists() and not force:
        return json.loads(json_path.read_text())
    case_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(binary), str(data_path), str(query_path)]
    t0 = time.time()
    proc = subprocess.run(cmd, cwd=root, text=True, capture_output=True, check=False)
    elapsed = time.time() - t0
    if proc.returncode != 0:
        (case_dir / "recast.stderr.log").write_text(proc.stderr)
        raise RuntimeError(f"RECAST failed for {dataset}/{workload}/seed={seed}: {proc.stderr[-2000:]}")
    (case_dir / "recast.stdout.log").write_text(proc.stdout)
    rows = parse_rows(proc.stdout)
    result = {
        "dataset": dataset,
        "workload": workload,
        "seed": seed,
        "command": cmd,
        "wallclock_s": elapsed,
        "queries": len(rows),
        "mean_query_dc": sum(r["dc"] for r in rows) / len(rows),
        "mean_query_ms": sum(r["query_ms"] for r in rows) / len(rows),
        "mean_results": sum(r["result"] for r in rows) / len(rows),
        "rows": rows,
    }
    json_path.write_text(json.dumps(result, indent=2))
    return result


def write_tsv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = ["dataset", "workload", "seed", "queries", "mean_query_dc", "mean_query_ms", "mean_results", "wallclock_s"]
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        for row in rows:
            writer.writerow({k: row.get(k, "") for k in fieldnames})


def aggregate(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups = {}
    for row in rows:
        groups.setdefault((row["dataset"], row["workload"]), []).append(row)
    out = []
    for (dataset, workload), items in sorted(groups.items()):
        out.append({
            "dataset": dataset,
            "workload": workload,
            "seeds": len(items),
            "mean_query_dc": sum(float(x["mean_query_dc"]) for x in items) / len(items),
            "mean_query_ms": sum(float(x["mean_query_ms"]) for x in items) / len(items),
            "mean_results": sum(float(x["mean_results"]) for x in items) / len(items),
        })
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-root", default="data/highdim")
    parser.add_argument("--out", default="run_outputs/recast_matrix")
    parser.add_argument("--workload-root", default="workloads")
    parser.add_argument("--datasets", required=True)
    parser.add_argument("--workloads", default=",".join(WORKLOADS))
    parser.add_argument("--qfd-datasets", default="")
    parser.add_argument("--seeds", default="1,2,3")
    parser.add_argument("--queries", type=int, default=1000)
    parser.add_argument("--selectivity", type=float, default=0.01)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    data_root = root / args.data_root
    out_root = root / args.out
    workload_root = root / args.workload_root
    datasets = [x for x in args.datasets.split(",") if x]
    global QFD_DATASETS
    QFD_DATASETS = {x for x in args.qfd_datasets.split(",") if x}
    workloads = [x for x in args.workloads.split(",") if x]
    seeds = [int(x) for x in args.seeds.split(",") if x]

    jobs = []
    for seed in seeds:
        for dataset in datasets:
            data_path = data_root / f"{dataset}_data.txt"
            if not data_path.exists():
                raise FileNotFoundError(f"missing dataset file: {data_path}")
            data = read_data(data_path)
            query_paths = prepare_workloads(dataset, data, workload_root / f"seed_{seed}", args.queries, args.selectivity, seed)
            binary = root / ("implementation/bin/recast_qfd" if dataset in QFD_DATASETS else "implementation/bin/recast_l2")
            if not binary.exists():
                raise FileNotFoundError(f"missing binary {binary}; run `make -C implementation` first")
            for workload in workloads:
                jobs.append((root, binary, dataset, data_path, query_paths[workload], workload, seed, out_root, args.force))

    rows = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futs = [pool.submit(run_one, *job) for job in jobs]
        for fut in as_completed(futs):
            row = fut.result()
            rows.append(row)
            print(f"done\t{row['dataset']}\t{row['workload']}\tseed={row['seed']}\tdc={row['mean_query_dc']:.3f}\tms={row['mean_query_ms']:.6f}", flush=True)

    rows.sort(key=lambda x: (x["dataset"], x["workload"], x["seed"]))
    write_tsv(out_root / "summary_by_seed.tsv", rows)
    write_tsv(out_root / "summary_aggregate.tsv", aggregate(rows))
    print(f"wrote {out_root / 'summary_by_seed.tsv'}")
    print(f"wrote {out_root / 'summary_aggregate.tsv'}")


if __name__ == "__main__":
    main()
