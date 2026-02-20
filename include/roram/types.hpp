#pragma once

#include <cstdint>
#include <cstddef>

namespace roram {

// Invalid logical address for dummy blocks (not in [0, N))
constexpr uint64_t INVALID_ADDR = UINT64_MAX;

// rORAM parameters (N = number of blocks, L = max range size, Z = bucket capacity)
struct Params {
  uint64_t N;   // number of logical blocks
  uint64_t L;   // max range size (e.g. 1 << 14)
  int Z;        // blocks per bucket (>= 3)
  size_t B;     // data bytes per block (e.g. 4096)
  int ell;      // ceil(log2(L)); number of sub-ORAMs = ell + 1
  int h;        // tree height = ceil(log2(N)); leaves = N

  Params(uint64_t n, uint64_t l, int z, size_t b);

  // Range size exponent for a request of size r: i s.t. 2^{i-1} < r <= 2^i
  static int range_exponent(uint64_t r);
  // Power-of-two range size for request size r: 2^i
  static uint64_t range_power2(uint64_t r);
};

}  // namespace roram
