#pragma once

#include <cstdint>

namespace roram {

// Bit-reverse x using the low 'bits' bits (paper: leftmost = LSB).
// Example 3-bit: 0->0, 1->4, 2->2, 3->6, 4->1, 5->5, 6->3, 7->7
inline uint64_t bit_reverse(uint64_t x, int bits) {
  if (bits <= 0) return 0;
  uint64_t result = 0;
  for (int i = 0; i < bits; ++i) {
    if (x & (1ULL << i))
      result |= 1ULL << (bits - 1 - i);
  }
  return result;
}

// For path P(p) from root to leaf p, bucket index at level j is p mod 2^j
// (in natural index; on disk we store level j as buckets 0..2^j-1 in order,
// and path p touches bucket (p mod 2^j) at level j).
inline uint64_t path_bucket_at_level(uint64_t path_leaf, int level) {
  return path_leaf % (1ULL << level);
}

// Number of buckets at level j
inline uint64_t buckets_at_level(int j) {
  return 1ULL << j;
}

}  // namespace roram
