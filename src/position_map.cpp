#include "roram/position_map.hpp"

namespace roram {

PositionMap::PositionMap(uint64_t N, int range_exp) : range_exp_(range_exp) {
  uint64_t stride = 1ULL << range_exp;
  uint64_t num_entries = (N + stride - 1) / stride;
  if (num_entries == 0) num_entries = 1;
  positions_.assign(num_entries, 0);
}

uint64_t PositionMap::query(uint64_t range_start) const {
  uint64_t idx = range_start >> range_exp_;
  if (idx >= positions_.size()) return 0;
  return positions_[idx];
}

void PositionMap::update(uint64_t range_start, uint64_t leaf_index) {
  uint64_t idx = range_start >> range_exp_;
  if (idx < positions_.size())
    positions_[idx] = leaf_index;
}

}  // namespace roram
