#include "roram/sub_oram.hpp"
#include "roram/bit_reverse.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cstring>

namespace roram {

SubORAM::SubORAM(const Params& params, int i, StorageBackend* storage, CryptoProvider* crypto)
    : params_(params), i_(i), storage_(storage), crypto_(crypto),
      pm_(params.N, i), stash_() {}

void SubORAM::merge_bucket_into_stash(std::vector<Block>& stash, const Bucket& bucket) {
  const uint64_t range_size = 1ULL << i_;
  for (const Block& b : bucket.blocks) {
    if (!b.valid()) continue;
    // Stale-copy check: discard blocks whose path tag no longer matches the
    // current position map.  Without this, a block re-assigned to a new path
    // can leave a ghost copy in the tree that later overwrites the live copy.
    uint64_t a0     = (b.a / range_size) * range_size;
    uint64_t offset = b.a - a0;
    if (b.p[static_cast<size_t>(i_)] != pm_.query(a0) + offset) continue;
    auto it = std::find_if(stash.begin(), stash.end(), [&b](const Block& s) { return s.a == b.a; });
    if (it != stash.end()) continue;  // keep existing (e.g. from higher level)
    stash.push_back(b);
  }
}

void SubORAM::merge_into_stash(const std::vector<Bucket>& buckets) {
  for (const Bucket& b : buckets)
    merge_bucket_into_stash(stash_, b);
}

void SubORAM::read_paths_level(uint64_t p, uint64_t count, int j, std::vector<Bucket>& out) {
  uint64_t n_buckets = num_buckets_at_level(j);
  uint64_t start = p % n_buckets;
  uint64_t num_needed = std::min(count, n_buckets);
  if (start + num_needed <= n_buckets) {
    storage_->read_buckets(j, start, num_needed, out);
  } else {
    std::vector<Bucket> part1, part2;
    storage_->read_buckets(j, start, n_buckets - start, part1);
    storage_->read_buckets(j, 0, num_needed - (n_buckets - start), part2);
    out = std::move(part1);
    out.insert(out.end(), part2.begin(), part2.end());
  }
}

void SubORAM::ReadRange(uint64_t a, std::vector<Block>& result, uint64_t& new_path_start) {
  const uint64_t range_len = 1ULL << i_;
  const uint64_t U_end = a + range_len;

  result.clear();
  std::unordered_set<uint64_t> seen;
  seen.reserve(static_cast<size_t>(range_len) * 2 + 8);
  for (const Block& b : stash_) {
    if (b.a >= a && b.a < U_end) {
      result.push_back(b);
      seen.insert(b.a);
    }
  }

  uint64_t p = pm_.query(a);
  new_path_start = crypto_->random_path(params_.N);
  pm_.update(a, new_path_start);

  for (int j = 0; j <= params_.h; ++j) {
    std::vector<Bucket> buckets;
    read_paths_level(p, range_len, j, buckets);
    for (const Bucket& bucket : buckets) {
      for (const Block& b : bucket.blocks) {
        if (!b.valid() || b.a < a || b.a >= U_end) continue;
        if (seen.find(b.a) == seen.end()) {
          result.push_back(b);
          seen.insert(b.a);
        }
      }
    }
  }

  // Synthesize zero-initialized blocks for any address in [a, U_end) not yet found.
  // Mirrors PathORAM's "create block on first access" behaviour.
  // Set p[i_] correctly so the stale-copy check passes on subsequent BatchEvict merges.
  for (uint64_t addr = a; addr < U_end; ++addr) {
    if (seen.find(addr) == seen.end()) {
      Block b(params_.B, params_.ell + 1);
      b.a = addr;
      b.p[static_cast<size_t>(i_)] = new_path_start + (addr - a);
      result.push_back(b);
      seen.insert(addr);
    }
  }

  std::sort(result.begin(), result.end(), [](const Block& x, const Block& y) { return x.a < y.a; });
}

void SubORAM::BatchEvict(uint64_t k, uint64_t cnt) {
  const int h = params_.h;
  const int Z = params_.Z;

  for (int j = 0; j <= h; ++j) {
    uint64_t n_buckets = num_buckets_at_level(j);
    uint64_t start = cnt % n_buckets;
    uint64_t num_needed = std::min(k, n_buckets);
    std::vector<Bucket> buckets;
    if (start + num_needed <= n_buckets) {
      storage_->read_buckets(j, start, num_needed, buckets);
    } else {
      std::vector<Bucket> part1, part2;
      storage_->read_buckets(j, start, n_buckets - start, part1);
      storage_->read_buckets(j, 0, num_needed - (n_buckets - start), part2);
      buckets = std::move(part1);
      buckets.insert(buckets.end(), part2.begin(), part2.end());
    }
    merge_into_stash(buckets);
  }

  for (int j = h; j >= 0; --j) {
    uint64_t n_buckets = num_buckets_at_level(j);
    uint64_t start = cnt % n_buckets;
    uint64_t num_needed = std::min(k, n_buckets);
    std::vector<Bucket> to_write(num_needed, Bucket(params_.Z, params_.B, params_.ell + 1));
    for (uint64_t i = 0; i < num_needed; ++i) {
      uint64_t path_idx = cnt + i;
      uint64_t r = path_idx % n_buckets;
      std::vector<Block> chosen;
      for (auto it = stash_.begin(); it != stash_.end(); ) {
        if (chosen.size() >= static_cast<size_t>(Z)) break;
        uint64_t block_path = it->p[static_cast<size_t>(i_)];
        if ((block_path % n_buckets) == r) {
          chosen.push_back(*it);
          it = stash_.erase(it);
        } else {
          ++it;
        }
      }
      for (size_t z = 0; z < chosen.size(); ++z)
        to_write[i].blocks[z] = chosen[z];
      for (size_t z = chosen.size(); z < static_cast<size_t>(Z); ++z)
        to_write[i].blocks[z].set_dummy();
    }
    if (start + num_needed <= n_buckets) {
      storage_->write_buckets(j, start, to_write);
    } else {
      uint64_t first_count = n_buckets - start;
      std::vector<Bucket> part1(to_write.begin(), to_write.begin() + static_cast<ptrdiff_t>(first_count));
      std::vector<Bucket> part2(to_write.begin() + static_cast<ptrdiff_t>(first_count), to_write.end());
      storage_->write_buckets(j, start, part1);
      storage_->write_buckets(j, 0, part2);
    }
  }
}

}  // namespace roram
