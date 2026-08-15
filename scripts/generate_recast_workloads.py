#!/usr/bin/env python3
"""Generate the four query workloads used by the RECAST experiments."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
from pathlib import Path

import numpy as np


WORKLOADS = (
    "fixed_B",
    "jump_A_to_B",
    "drift_A_to_B",
    "three_jump_A_B_C",
)


def read_data(path: Path) -> np.ndarray:
    with path.open() as handle:
        header = handle.readline().split()
        if len(header) < 2:
            raise ValueError(f"bad data header in {path}")
        dim, expected_rows = int(header[0]), int(header[1])
        data = np.loadtxt(handle, dtype=np.float32)
    if data.ndim == 1:
        data = data.reshape(1, -1)
    if data.shape != (expected_rows, dim):
        raise ValueError(
            f"bad data shape for {path}: {data.shape}, expected {(expected_rows, dim)}"
        )
    return data


def qfd_matrix(dim: int, seed: int = 54321) -> np.ndarray:
    """Match the deterministic QFD matrix used by recast_qfd."""
    libc = ctypes.CDLL(None)
    libc.srand(seed + 12345)
    rand_max = 2147483647.0
    random_matrix = np.empty((dim, dim), dtype=np.float32)
    for i in range(dim):
        for j in range(dim):
            random_matrix[i, j] = (float(libc.rand()) / rand_max - 0.5) * 2.0
    return (random_matrix.T @ random_matrix).astype(np.float32)


def metric_distances(
    data: np.ndarray,
    center: np.ndarray,
    metric: str,
    qfd: np.ndarray | None,
) -> np.ndarray:
    if metric == "qfd":
        if qfd is None:
            raise ValueError("QFD matrix is required for QFD workloads")
        diff = data - center
        values = np.einsum("ij,jk,ik->i", diff, qfd, diff, optimize=True)
        return np.sqrt(np.maximum(values, 0.0)).astype(np.float32)
    return np.linalg.norm(data - center, axis=1)


def far_apart_pools(
    data: np.ndarray,
    seed: int,
    metric: str,
    qfd: np.ndarray | None,
    pool_size: int,
) -> tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(seed)
    sample_ids = rng.choice(data.shape[0], size=min(2000, data.shape[0]), replace=False)
    first = int(sample_ids[0])
    a_center = int(
        sample_ids[
            int(np.argmax(metric_distances(data[sample_ids], data[first], metric, qfd)))
        ]
    )
    b_center = int(
        sample_ids[
            int(np.argmax(metric_distances(data[sample_ids], data[a_center], metric, qfd)))
        ]
    )
    take = min(pool_size, data.shape[0])
    a_pool = np.argpartition(
        metric_distances(data, data[a_center], metric, qfd), take - 1
    )[:take]
    b_pool = np.argpartition(
        metric_distances(data, data[b_center], metric, qfd), take - 1
    )[:take]
    return a_pool, b_pool


def far_apart_three_pools(
    data: np.ndarray,
    seed: int,
    metric: str,
    qfd: np.ndarray | None,
    pool_size: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    a_pool, b_pool = far_apart_pools(data, seed, metric, qfd, pool_size)
    rng = np.random.default_rng(seed + 919)
    sample_ids = rng.choice(data.shape[0], size=min(3000, data.shape[0]), replace=False)
    distance_a = metric_distances(data[sample_ids], data[int(a_pool[0])], metric, qfd)
    distance_b = metric_distances(data[sample_ids], data[int(b_pool[0])], metric, qfd)
    c_center = int(sample_ids[int(np.argmax(np.minimum(distance_a, distance_b)))])
    take = min(pool_size, data.shape[0])
    c_pool = np.argpartition(
        metric_distances(data, data[c_center], metric, qfd), take - 1
    )[:take]
    return a_pool, b_pool, c_pool


def choose(data: np.ndarray, pool: np.ndarray, count: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return data[rng.choice(pool, size=count, replace=True)].copy()


def gradual_drift(
    data: np.ndarray,
    a_pool: np.ndarray,
    b_pool: np.ndarray,
    count: int,
    seed: int,
) -> np.ndarray:
    rng = np.random.default_rng(seed)
    rows = []
    denominator = max(1, count - 1)
    for query_id in range(count):
        pool = b_pool if rng.random() < query_id / denominator else a_pool
        rows.append(data[int(rng.choice(pool))])
    return np.asarray(rows, dtype=np.float32)


def calibrate_radius(
    data: np.ndarray,
    centers: np.ndarray,
    selectivity: float,
    seed: int,
    metric: str,
    qfd: np.ndarray | None,
) -> float:
    rng = np.random.default_rng(seed)
    point_ids = rng.choice(data.shape[0], size=min(10_000, data.shape[0]), replace=False)
    query_ids = rng.choice(centers.shape[0], size=min(40, centers.shape[0]), replace=False)
    sampled_data = data[point_ids]
    target = max(1, int(round(selectivity * sampled_data.shape[0]))) - 1
    kth_distances = []
    for query_id in query_ids:
        distances = metric_distances(sampled_data, centers[int(query_id)], metric, qfd)
        kth_distances.append(float(np.partition(distances, target)[target]))
    kth_distances.sort()
    return kth_distances[len(kth_distances) // 2]


def write_queries(path: Path, centers: np.ndarray, radius: float) -> None:
    with path.open("w") as handle:
        handle.write(f"{len(centers)}\n")
        for row in centers:
            values = " ".join(f"{float(value):.9f}" for value in row)
            handle.write(f"{radius:.9f} {values}\n")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def generate(args: argparse.Namespace) -> dict[str, object]:
    data_path = args.data.resolve()
    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    dataset = args.dataset or data_path.stem.removesuffix("_data")

    data = read_data(data_path)
    qfd = qfd_matrix(data.shape[1]) if args.metric == "qfd" else None
    a_pool, b_pool, c_pool = far_apart_three_pools(
        data,
        args.seed + data.shape[1],
        args.metric,
        qfd,
        args.pool_size,
    )

    half = args.queries // 2
    third = args.queries // 3
    workloads = {
        "fixed_B": choose(data, b_pool, args.queries, args.seed + 1000),
        "jump_A_to_B": np.vstack(
            [
                choose(data, a_pool, half, args.seed + 2000),
                choose(data, b_pool, args.queries - half, args.seed + 3000),
            ]
        ).astype(np.float32),
        "drift_A_to_B": gradual_drift(
            data, a_pool, b_pool, args.queries, args.seed + 4000
        ),
        "three_jump_A_B_C": np.vstack(
            [
                choose(data, a_pool, third, args.seed + 5000),
                choose(data, b_pool, third, args.seed + 6000),
                choose(data, c_pool, args.queries - 2 * third, args.seed + 7000),
            ]
        ).astype(np.float32),
    }

    radius = args.radius
    if radius is None:
        radius = calibrate_radius(
            data,
            np.vstack(list(workloads.values())),
            args.selectivity,
            args.seed + 8000,
            args.metric,
            qfd,
        )

    files: dict[str, dict[str, object]] = {}
    for workload in WORKLOADS:
        path = output_dir / f"{dataset}_{workload}_q{args.queries}_s{args.seed}.txt"
        write_queries(path, workloads[workload], radius)
        files[workload] = {"path": path.name, "sha256": sha256(path)}

    manifest: dict[str, object] = {
        "dataset": dataset,
        "source_data": str(data_path),
        "objects": int(data.shape[0]),
        "dimensions": int(data.shape[1]),
        "metric": args.metric,
        "queries_per_workload": args.queries,
        "seed": args.seed,
        "pool_size": min(args.pool_size, int(data.shape[0])),
        "selectivity": args.selectivity if args.radius is None else None,
        "radius": float(radius),
        "workloads": files,
    }
    manifest_path = output_dir / f"{dataset}_workloads_q{args.queries}_s{args.seed}.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate fixed, jump, drift, and three-jump RECAST query files."
    )
    parser.add_argument("--data", type=Path, required=True, help="RECAST-format data file")
    parser.add_argument("--output", type=Path, required=True, help="output directory")
    parser.add_argument("--dataset", help="dataset name used in output file names")
    parser.add_argument("--metric", choices=("l2", "qfd"), default="l2")
    parser.add_argument("--queries", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--pool-size", type=int, default=5000)
    parser.add_argument("--selectivity", type=float, default=0.01)
    parser.add_argument(
        "--radius",
        type=float,
        help="use a fixed radius instead of calibrating from --selectivity",
    )
    args = parser.parse_args()
    if args.queries <= 0:
        parser.error("--queries must be positive")
    if args.pool_size <= 0:
        parser.error("--pool-size must be positive")
    if not 0.0 < args.selectivity <= 1.0:
        parser.error("--selectivity must be in (0, 1]")
    if args.radius is not None and args.radius < 0.0:
        parser.error("--radius must be non-negative")
    return args


def main() -> None:
    manifest = generate(parse_args())
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
