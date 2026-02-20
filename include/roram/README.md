# rORAM Headers

Public API and core types for the rORAM C++ implementation.

## Files

| File | Purpose |
|------|---------|
| **types.hpp** | `Params` (N, L, Z, B, ℓ, h), `INVALID_ADDR`, `range_exponent` / `range_power2` |
| **bit_reverse.hpp** | `bit_reverse()`, `path_bucket_at_level()`, `buckets_at_level()` for tree layout |
| **block.hpp** | `Block` (data, a, p[0..ℓ]), `Bucket` (Z blocks), serialize/deserialize |
| **storage.hpp** | `StorageBackend`, `MemoryStorage`, `FileStorage` (read/write buckets, seek count) |
| **position_map.hpp** | `PositionMap` – maps range start to leaf index per sub-ORAM |
| **crypto.hpp** | `CryptoProvider`, `NoOpCrypto`; optional OpenSSL impl behind `RORAM_USE_OPENSSL` |
| **sub_oram.hpp** | `SubORAM` – `ReadRange(a)`, `BatchEvict(k)`, stash, position map for one tree R_i |
| **roram.hpp** | `rORAM` – `Access(a, r, op, D)`, `get_seek_count()`, ℓ+1 sub-ORAMs |

## Include path

Builds use `-Iinclude`, so include as:

```cpp
#include "roram/roram.hpp"
#include "roram/types.hpp"
```
