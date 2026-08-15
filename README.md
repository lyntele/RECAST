# RECAST

RECAST is a query-region adaptive index for exact similarity search.

The index is constructed during query processing. No offline build phase is required for RECAST itself.

## Build

Prebuilt binaries are included under `implementation/bin/`. Rebuild before benchmarking on a different machine because the default build uses `-march=native`.

```bash
make -C implementation clean
make -C implementation -j
```

Default compiler settings:

```text
g++ -O3 -std=c++14 -mavx -march=native
```

## Executables

```text
implementation/bin/recast_l2              exact range search, L2 distance
implementation/bin/recast_qfd             exact range search, QFD distance
implementation/bin/recast_knn_lite_l2     exact kNN, L2 distance
```

## Input Format

Data file:

```text
<dim> <n> 2
x_1[0] x_1[1] ... x_1[dim-1]
...
x_n[0] x_n[1] ... x_n[dim-1]
```

Range-query file:

```text
<num_queries>
<radius> q_1[0] q_1[1] ... q_1[dim-1]
...
```

The kNN executable uses the same query file format and ignores the radius field.

## Basic Usage

Exact range search with L2:

```bash
./implementation/bin/recast_l2 data.txt queries.txt
```

Exact range search with QFD:

```bash
./implementation/bin/recast_qfd data.txt queries.txt
```

Exact L2 kNN:

```bash
./implementation/bin/recast_knn_lite_l2 -k 10 data.txt queries.txt
```

Common range-search options:

```text
-t N        leaf threshold, default 128
-p K        per-region pivot storage budget, default 32
-Q K        active pivots per query, default 32
-K 0|1      keep parent paid evidence after split, default 1
-A 0|1      enable residual child, default 1
```

Common kNN options:

```text
-k K        number of nearest neighbors
-t N        leaf threshold, default 128
-h D        max recursive depth
-s N        reuse up to N previous top-k objects as initial candidates
```

The kNN executable is included as an exact-query extension. The primary implementation path is exact range search.

## Batch Runner

`scripts/run_recast_matrix.py` runs the range-search executable over a dataset/workload matrix. The script expects datasets and query workloads to be supplied by the caller.

```bash
python3 scripts/run_recast_matrix.py \
  --data-root data \
  --workload-root workloads \
  --datasets dataset1,dataset2 \
  --workloads fixed_B,jump_A_to_B \
  --seeds 1,2,3 \
  --queries 1000 \
  --jobs 8 \
  --out run_outputs/recast
```

Outputs:

```text
run_outputs/recast/summary_by_seed.tsv
run_outputs/recast/summary_aggregate.tsv
```

## Workload Names

The runner supports these workload names when matching query files:

```text
fixed_B
jump_A_to_B
drift_A_to_B
three_jump_A_B_C
```

## Long-sequence workloads

The repository includes a deterministic q10K generator for the SIFT1M and
GloVe fixed and shift/revisit workloads:

```bash
python3 scripts/generate_recast_long_stream.py --help
```

Dataset download links, conversion details, frozen radii, checksums, and a
reproduction example are provided in
[`docs/LONG_STREAM_WORKLOADS.md`](docs/LONG_STREAM_WORKLOADS.md).

## Repository Layout

```text
implementation/include/recast/recast_index.h    public API
implementation/apps/                            command-line frontends
implementation/src/recast_index.cpp             release translation unit
implementation/src/recast/                      implementation files
scripts/run_recast_matrix.py                    batch runner
scripts/generate_recast_long_stream.py          q10K workload generator
docs/LONG_STREAM_WORKLOADS.md                   datasets and reproduction
```

## Generated Files

The following paths are intentionally ignored by git:

```text
implementation/build/
data/
workloads/
run_outputs/
```
