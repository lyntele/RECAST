#!/usr/bin/env python3
"""Generate the frozen q10K fixed and shift/revisit workloads.

The input uses the public RECAST text format: ``dim n metric_id`` followed by
one floating-point vector per line. The generator writes exact range queries
and a manifest containing all identities needed for reproduction.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import random

import numpy as np


RADII = {
    "sift1m": {
        1: {"fixed_B": 310.501220703125, "shift_revisit_A_B_A": 262.133544921875},
        2: {"fixed_B": 242.6499481201172, "shift_revisit_A_B_A": 271.9172668457031},
    },
    "glove100": {
        1: {"fixed_B": 3.1082847118377686, "shift_revisit_A_B_A": 3.209174633026123},
        2: {"fixed_B": 3.481041669845581, "shift_revisit_A_B_A": 3.3926751613616943},
    },
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_data(path: Path) -> np.ndarray:
    with path.open() as handle:
        dim, count, _ = map(int, handle.readline().split())
        data = np.loadtxt(handle, dtype=np.float32)
    if data.shape != (count, dim):
        raise ValueError(f"{path}: got {data.shape}, expected {(count, dim)}")
    return data


def l2_batch(data: np.ndarray, center: np.ndarray) -> np.ndarray:
    return np.linalg.norm(data - center, axis=1)


def far_apart_pools(
    data: np.ndarray,
    seed: int,
    sample_size: int = 2000,
    pool_size: int = 5000,
) -> tuple[np.ndarray, np.ndarray]:
    rng = random.Random(seed)
    sample_ids = np.asarray(
        rng.sample(range(data.shape[0]), min(sample_size, data.shape[0])),
        dtype=np.int64,
    )
    first = int(sample_ids[0])
    sample = data[sample_ids]
    a_center = int(sample_ids[int(np.argmax(l2_batch(sample, data[first])))])
    b_center = int(sample_ids[int(np.argmax(l2_batch(sample, data[a_center])))])
    take = min(pool_size, data.shape[0])
    a_pool = np.argpartition(l2_batch(data, data[a_center]), take - 1)[:take]
    b_pool = np.argpartition(l2_batch(data, data[b_center]), take - 1)[:take]
    return a_pool, b_pool


def choose(data: np.ndarray, pool: np.ndarray, count: int, seed: int) -> np.ndarray:
    indices = np.random.default_rng(seed).choice(pool, size=count, replace=True)
    return data[indices].copy()


def write_queries(path: Path, centers: np.ndarray, radius: float) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as handle:
        handle.write(f"{len(centers)}\n")
        for row in centers:
            vector = " ".join(f"{float(value):.9f}" for value in row)
            handle.write(f"{radius:.9f} {vector}\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", choices=sorted(RADII), required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seed", type=int, choices=(1, 2), required=True)
    parser.add_argument("--queries", type=int, default=10000)
    args = parser.parse_args()
    if args.queries != 10000:
        raise ValueError("the frozen protocol uses exactly 10,000 queries")

    data = read_data(args.data)
    a_pool, b_pool = far_apart_pools(data, args.seed + data.shape[1])
    fixed = choose(data, b_pool, 10000, args.seed + 1000)
    shift_revisit = np.vstack(
        [
            choose(data, a_pool, 3500, args.seed + 2000),
            choose(data, b_pool, 3000, args.seed + 3000),
            choose(data, a_pool, 3500, args.seed + 7000),
        ]
    ).astype(np.float32)
    paths = {
        "fixed_B": args.out / f"{args.dataset}_fixed_B_seed{args.seed}_q10000.txt",
        "shift_revisit_A_B_A": (
            args.out
            / f"{args.dataset}_shift_revisit_A_B_A_seed{args.seed}_q10000.txt"
        ),
    }
    write_queries(paths["fixed_B"], fixed, RADII[args.dataset][args.seed]["fixed_B"])
    write_queries(
        paths["shift_revisit_A_B_A"],
        shift_revisit,
        RADII[args.dataset][args.seed]["shift_revisit_A_B_A"],
    )
    manifest = {
        "protocol": "recast-long-stream-v1-paper-radius",
        "dataset": args.dataset,
        "seed": args.seed,
        "query_count": 10000,
        "selectivity_target_fraction": 0.01,
        "data": str(args.data),
        "data_sha256": sha256(args.data),
        "regions": {"pool_size": 5000, "anchor_sample_size": 2000},
        "fixed_B": {
            "phase_counts": [10000],
            "radius": RADII[args.dataset][args.seed]["fixed_B"],
        },
        "shift_revisit_A_B_A": {
            "phase_counts": [3500, 3000, 3500],
            "radius": RADII[args.dataset][args.seed]["shift_revisit_A_B_A"],
        },
        "queries": {
            name: {"path": str(path), "sha256": sha256(path)}
            for name, path in paths.items()
        },
    }
    manifest_path = args.out / f"{args.dataset}_seed{args.seed}_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
