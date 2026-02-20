# rORAM C++ Implementation

C++ implementation of **rORAM: Efficient Range ORAM with O(log² N) Locality** (Chakraborti et al., NDSS 2019). Optimized for range queries on sequential logical blocks with minimal disk seeks.

## Features

- **Core rORAM**: ℓ+1 Path-ORAM–style sub-ORAMs (R₀…R_ℓ), bit-reversed tree layout, locality-sensitive block mapping, distributed position map
- **Storage**: In-memory and file-backed backends with optional seek counting
- **CLI**: init, read, write, bench, and **rORAM vs Path ORAM** comparison with seek penalty and CSV output

## Build

```bash
make          # builds libroram.a and roram_main
make clean    # remove object files and binaries
```

Requires a C++17 compiler. Optional: use CMake via `CMakeLists.txt`.

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

Output columns: `range_size`, `scheme`, `total_ms`, `time_per_block_ms`, `mean_seeks`, `ci_low`, `ci_high`.

## Layout

- **include/roram/** – Headers (types, block, storage, position_map, sub_oram, roram, crypto, bit_reverse)
- **src/** – Implementation (.cpp) and `main.cpp` CLI

See [include/roram/README.md](include/roram/README.md) and [src/README.md](src/README.md) for module details.

## References

- Paper: NDSS 2019, “rORAM: Efficient Range ORAM with O(log² N) Locality”
- Java reference: [github.com/anrinch/rORAM](https://github.com/anrinch/rORAM)

## License

Research/educational use. Add a LICENSE file as needed for your project.
