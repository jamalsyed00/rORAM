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
  if (r == 0) return 0;
  int i = 0;
  uint64_t p = 1;
  while (p < r) { p *= 2; ++i; }
  return i;
}

uint64_t Params::range_power2(uint64_t r) {
  if (r == 0) return 1;
  uint64_t p = 1;
  while (p < r) p *= 2;
  return p;
}

}  // namespace roram
