#pragma once

#include "roram/types.hpp"
#include "roram/block.hpp"
#include "roram/storage.hpp"
#include "roram/position_map.hpp"
#include "roram/crypto.hpp"
#include <memory>
#include <vector>

namespace roram {

// Single sub-ORAM R_i: supports ReadRange(a) for range [a, a+2^i) and BatchEvict(k).
// Uses locality-aware layout: at level j, bucket index r is at offset r (consecutive on disk).
class SubORAM {
 public:
  SubORAM(const Params& params, int i, StorageBackend* storage, CryptoProvider* crypto);
  // ReadRange: a must be multiple of 2^i. Returns blocks in [a, a+2^i) and new path p' for start.
  void ReadRange(uint64_t a, std::vector<Block>& result, uint64_t& new_path_start);
  // BatchEvict(k): evict next k paths (using global cnt); caller must advance cnt after.
  void BatchEvict(uint64_t k, uint64_t cnt);
  // Merge blocks from tree into stash (for BatchEvict read phase). Replace by address.
  void merge_into_stash(const std::vector<Bucket>& buckets);
  // Stash access for rORAM Access protocol
  std::vector<Block>& stash() { return stash_; }
  const std::vector<Block>& stash() const { return stash_; }
  PositionMap& position_map() { return pm_; }
  int range_exp() const { return i_; }

 private:
  Params params_;
  int i_;                          // this sub-ORAM index; range size = 2^i_
  StorageBackend* storage_;
  CryptoProvider* crypto_;
  PositionMap pm_;
  std::vector<Block> stash_;

  uint64_t num_buckets_at_level(int j) const { return 1ULL << j; }
  void read_paths_level(uint64_t p, uint64_t count, int j, std::vector<Bucket>& out);
  void write_paths_level(uint64_t cnt, uint64_t k, int j, const std::vector<Bucket>& buckets);
};

}  // namespace roram
