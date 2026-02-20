#pragma once

#include "roram/types.hpp"
#include <cstring>
#include <vector>

namespace roram {

// Physical block: data[B], logical address a, path tags p0..p_ell for sub-ORAMs R0..R_ell
struct Block {
  std::vector<uint8_t> data;  // B bytes
  uint64_t a;                 // logical address (INVALID_ADDR = dummy)
  std::vector<uint64_t> p;    // p.size() = ell+1; p[j] = leaf index in R_j

  Block() = default;
  Block(size_t data_len, int num_orams);

  bool valid() const { return a != INVALID_ADDR; }
  void set_dummy();
  size_t serialized_size(const Params& params) const;
  void serialize(uint8_t* out, const Params& params) const;
  void deserialize(const uint8_t* in, const Params& params);
};

// Bucket = Z blocks (fixed size; pad with dummies)
struct Bucket {
  std::vector<Block> blocks;

  explicit Bucket(int Z, size_t data_len, int num_orams);
  size_t serialized_size(const Params& params) const;
  void serialize(uint8_t* out, const Params& params) const;
  void deserialize(const uint8_t* in, const Params& params);
};

}  // namespace roram
