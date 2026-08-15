# Long-sequence workloads

This document gives the public inputs and generation procedure for the q10K
fixed and shift/revisit workloads. Datasets, generated queries, indexes, and
experiment results are not stored in this repository.

## Data sources

### SIFT1M

- Source: [TEXMEX SIFT corpus](ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz).
  The [TEXMEX corpus page](http://corpus-texmex.irisa.fr/) describes the
  collection.
- Source member: `sift/sift_base.fvecs`, containing one million 128-dimensional
  SIFT vectors. Every TEXMEX `fvecs` record starts with an int32 dimension and
  then stores 128 float32 coordinates.
- RECAST conversion: write the header `128 1000000 2`, followed by one
  whitespace-separated vector per line.
- Converted file SHA256 used for the reported experiment:
  `365fb75d9f4be7ee2ad20b9643600e037b8ae7adc3c2f9ac02ea19160cccf651`.

### GloVe 100D

- Source: [Stanford GloVe](https://nlp.stanford.edu/projects/glove/).
- Archive: [GloVe 2024 WikiGigaword 100D](https://downloads.cs.stanford.edu/nlp/data/wordvecs/glove.2024.wikigiga.100d.zip).
  Its SHA256 is
  `74adc1abefd41fe686fae7d92f37be7bdb93e3cf9e46ce0c93bdd5d982cc76e2`.
- Source member:
  `wiki_giga_2024_100_MFT20_vectors_seed_2024_alpha_0.75_eta_0.05.050_combined.txt`.
  Every row contains a token and 100 coordinates.
- RECAST conversion: discard the token, parse the first 1,200,000 valid rows as
  float32, and write the header `100 1200000 2` followed by one vector per line.
- Converted file SHA256 used for the reported experiment:
  `2a231cb01de9e5d6554e0c22d8e3147a32c61929fe1afcff11cbf13379f135ad`.

### Yambda

- Source: [Yandex Yambda on Hugging Face](https://huggingface.co/datasets/yandex/yambda),
  released under Apache 2.0.
- Inputs: `sequential_50m_listens.parquet` and the official audio embeddings
  parquet.
- Mapping used in the real interaction workload: retain organic events with
  `played_ratio_pct > 50`, order them by recorded time, join each listened item
  to its official 128-dimensional normalized audio embedding, and use that
  embedding as an exact range-query center. The event sequence is real; the L2
  radius is calibrated by the experiment and is not defined by Yambda.

## Frozen q10K generator

`scripts/generate_recast_long_stream.py` generates two exact range workloads
for SIFT1M and GloVe. The protocol uses seeds 1 and 2, a 1 percent target
selectivity, and exactly 10,000 queries.

- `fixed_B`: 10,000 centers sampled with replacement from the 5,000 database
  objects nearest to anchor B.
- `shift_revisit_A_B_A`: 3,500 centers from A, 3,000 from B, and 3,500 returning
  to A.
- A and B are obtained by farthest-point selection over a deterministic sample
  of 2,000 database objects. Each region contains the 5,000 database objects
  nearest to its anchor.
- Radii are frozen in the generator. They match the submitted fixed and jump
  workloads, so the extension changes duration and adds a revisit without
  tuning the radius after observing long-sequence results.

Example:

```bash
python3 scripts/generate_recast_long_stream.py \
  --dataset sift1m \
  --data data/highdim/sift1m_data.txt \
  --seed 1 \
  --out generated_queries/long_stream
```

The command writes both query files and a JSON manifest containing the data
SHA, query SHAs, radii, phase sizes, seed, and protocol identifier. All methods
must read the same generated query file. For cumulative evaluation, static
construction is charged once before query 1 and checkpoints are 1K, 2K, 5K,
and 10K.
