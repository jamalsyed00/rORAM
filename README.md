# rORAM C++ Implementation

C++ implementation of **rORAM: Efficient Range ORAM with O(log² N) Locality** (Chakraborti et al., NDSS 2019). Optimized for range queries on sequential logical blocks with minimal disk seeks.

## Features

- **Core rORAM**: ℓ+1 Path-ORAM–style sub-ORAMs (R₀…R_ℓ), bit-reversed tree layout, locality-sensitive block mapping, distributed position map
- **Path ORAM baseline**: dedicated `PathORAM` implementation (`L=1`) with explicit position map + stash
- **Storage**: In-memory and file-backed backends with optional seek counting
- **Crypto boundary**: bucket-level crypto hooks at storage serialization boundary (NoOp by default, OpenSSL AES-GCM when enabled)
- **CLI**: init, read, write, bench, and **rORAM vs Path ORAM** comparison with seek penalty and CSV output

## Build

```bash
make          # builds libroram.a and roram_main
make tests_basic
make clean    # remove object files and binaries
```

Requires a C++17 compiler. Optional: use CMake via `CMakeLists.txt`.

### OpenSSL Build (AES-GCM + auth-tag tests)

Prerequisites (macOS/Homebrew):

```bash
brew install cmake openssl
```

```bash
cmake -S . -B build/openssl -DRORAM_USE_OPENSSL=ON -DOPENSSL_ROOT_DIR="$(brew --prefix openssl)"
cmake --build build/openssl -j
./build/openssl/tests_basic
```

If OpenSSL is not found, install development headers/libraries and re-run CMake.

## Usage

| Command | Description |
|--------|-------------|
| `./roram_main init N L [Z] [B]` | Print parameters for N blocks, max range L |
| `./roram_main read N L a r` | Read range [a, a+r) |
| `./roram_main write N L a r` | Write range [a, a+r) with zeros |
| `./roram_main bench N L [trials]` | Benchmark rORAM range reads (powers of 2) |
| `./roram_main compare [options]` | rORAM vs Path ORAM; see below |

### Compare (rORAM vs Path ORAM)

```bash
# In-memory, 5 trials, default N=65536 L=8192
./roram_main compare

# With simulated seek cost (shows crossover: rORAM wins for larger ranges)
./roram_main compare --N 4096 --L 256 --trials 5 --seek-penalty-us 50

# File-backed storage, CSV output
./roram_main compare --N 65536 --L 8192 --file /tmp/roram_bench --csv results.csv
```

**Options**: `--N`, `--L`, `--trials`, `--seek-penalty-us`, `--file`, `--csv`

Output columns: `range_size`, `scheme`, `mean_ms`, `p50_ms`, `p95_ms`, `time_per_block_ms`, `logical_B`, `mean_seeks`, `ci_low`, `ci_high`.

## Tests

```bash
make roram_main tests_basic
./tests_basic
```

## Benchmark Matrix (RAM / SSD / HDD)

Use the script below to produce advisor-ready CSVs for multiple media:

```bash
./scripts/benchmark_matrix.sh \
  --N 65536 --L 8192 --trials 5 --seek-penalty-us 50 \
  --outdir bench_out \
  --ssd-path /Volumes/YourSSD/roram_bench \
  --hdd-path /Volumes/YourHDD/roram_bench
```

For RAM-only runs, omit `--ssd-path` and `--hdd-path`.

## NDSS'19-Style Replication (SSD)

The paper's Section VII evaluates:
1) query access time (random range queries, synchronous issue)
2) query throughput (queries/sec) over varying range sizes/workloads

Use this script to reproduce the same style of measurements in this codebase:

```bash
./scripts/replicate_ndss2019_ssd.sh \
  --N 4096 --L 256 --trials 5 \
  --seek-penalty-us 0 \
  --outdir /tmp/ndss_replica \
  --backing-prefix /tmp/ndss_replica/device

./scripts/replicate_ndss2019_ssd.sh \
  --N 4096 --L 256 --trials 5 \
  --seek-penalty-us 50 \
  --outdir /tmp/ndss_replica \
  --backing-prefix /tmp/ndss_replica/device
```

Outputs:
- `ndss_access_time_*.csv`: direct benchmark output
- `ndss_throughput_*.csv`: derived from query latency (`queries/sec`, `MB/s`)

Notes:
- The paper's full setup uses a 16GB logical database (`2^22` blocks of 4KB) and additional FileBench workloads.
- For local developer runs, use scaled `N`/`L` to stay within disk limits.

### Throughput Workload Replication (Figure 7-style)

The CLI also supports trace-driven synchronous throughput on one long-lived ORAM instance:

```bash
./roram_main workload \
  --mode fileserver \
  --queries 400 \
  --N 4096 --L 256 \
  --seek-penalty-us 50 \
  --file /tmp/ndss_workload/device \
  --csv /tmp/ndss_workload/fileserver.csv
```

To run `sequential`, `fileserver`, and `videoserver` in one shot:

```bash
./scripts/replicate_ndss2019_workload.sh \
  --N 4096 --L 256 --queries 400 \
  --seek-penalty-us 50 \
  --outdir /tmp/ndss_workload \
  --backing-prefix /tmp/ndss_workload/device
```

## Layout

- **include/roram/** – Headers (types, block, storage, position_map, sub_oram, roram, crypto, bit_reverse)
- **src/** – Implementation (.cpp) and `main.cpp` CLI

See [include/roram/README.md](include/roram/README.md) and [src/README.md](src/README.md) for module details.

## References

- Paper: NDSS 2019, “rORAM: Efficient Range ORAM with O(log² N) Locality”
- Java reference: [github.com/anrinch/rORAM](https://github.com/anrinch/rORAM)

## License

Research/educational use. Add a LICENSE file as needed for your project.
