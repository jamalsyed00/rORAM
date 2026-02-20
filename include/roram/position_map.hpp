#pragma once

#include "roram/types.hpp"
#include <cstdint>
#include <vector>

namespace roram {

// Position map for sub-ORAM R_i: maps range start address (multiple of 2^i) to leaf index.
// Client-side: array of size ceil(N/2^i). Index for range_start is range_start >> i.
class PositionMap {
 public:
  // N = number of blocks, range_exp = i (range length 2^i)
  PositionMap(uint64_t N, int range_exp);
  uint64_t query(uint64_t range_start) const;
  void update(uint64_t range_start, uint64_t leaf_index);

 private:
  int range_exp_;
  std::vector<uint64_t> positions_;
};

}  // namespace roram
