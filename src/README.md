# rORAM Sources

Implementation files and CLI for the rORAM C++ project.

## Files

| File | Purpose |
|------|---------|
| **types.cpp** | `Params` constructor, `range_exponent`, `range_power2` |
| **block.cpp** | Block/Bucket serialize, deserialize, dummy handling |
| **crypto.cpp** | `NoOpCrypto::random_path`; OpenSSL encrypt/decrypt when `RORAM_USE_OPENSSL` |
| **position_map.cpp** | `PositionMap` query/update by range start |
| **storage_mem.cpp** | `MemoryStorage` – in-memory buckets, seek counting |
| **storage_file.cpp** | `FileStorage` – file-backed buckets, optional seek counting |
| **sub_oram.cpp** | `SubORAM::ReadRange`, `SubORAM::BatchEvict`, stash merge |
| **roram.cpp** | `rORAM` constructor, `Access()` (two ReadRanges + BatchEvict on all trees) |
| **main.cpp** | CLI: init, read, write, bench, compare (rORAM vs Path ORAM) |

## Build

From project root:

```bash
make          # libroram.a + roram_main
make roram_main
```

All sources are compiled with `-I../include` (or `-Iinclude` from root).
