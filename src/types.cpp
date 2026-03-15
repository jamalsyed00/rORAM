#include "roram/types.hpp"
#include <algorithm>
#include <cmath>

namespace roram {

Params::Params(uint64_t n, uint64_t l, int z, size_t b)
    : N(n), L(l), Z(z), B(b), ell(0), h(0) {
  if (L > 0) {
    uint64_t t = L;
    while (t > 1) { ++ell; t >>= 1; }
    if ((1ULL << ell) < L) ++ell;
  }
  if (N > 0) {
    uint64_t t = N;
    while (t > 1) { ++h; t >>= 1; }
    if ((1ULL << h) < N) ++h;
  }
}

int Params::range_exponent(uint64_t r) {
  // Opt 3: O(1) via count-leading-zeros (replaces O(log r) loop).
  // Returns ceil(log2(r)), i.e. the smallest i such that 2^i >= r.
  if (r <= 1) return 0;
  return 64 - __builtin_clzll(r - 1);
}

uint64_t Params::range_power2(uint64_t r) {
  // Opt 3: O(1) via bit shift from exponent.
  if (r <= 1) return 1;
  return 1ULL << (64 - __builtin_clzll(r - 1));
}

}  // namespace roram
