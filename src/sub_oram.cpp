#include "roram/sub_oram.hpp"
#include "roram/bit_reverse.hpp"
#include <algorithm>
#include <unordered_map>
#include <cstring>

namespace roram {

SubORAM::SubORAM(const Params& params, int i, StorageBackend* storage, CryptoProvider* crypto)
    : params_(params), i_(i), storage_(storage), crypto_(crypto),
      pm_(params.N, i), stash_() {}

static void merge_bucket_into_stash(std::vector<Block>& stash, const Bucket& bucket, int oram_index) {
  for (const Block& b : bucket.blocks) {
    if (!b.valid()) continue;
    auto it = std::find_if(stash.begin(), stash.end(), [&b](const Block& s) { return s.a == b.a; });
    if (it != stash.end()) continue;  // keep existing (e.g. from higher level)
    stash.push_back(b);
  }
}

void SubORAM::merge_into_stash(const std::vector<Bucket>& buckets) {
  for (const Bucket& b : buckets)
    merge_bucket_into_stash(stash_, b, i_);
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
  for (const Block& b : stash_) {
    if (b.a >= a && b.a < U_end)
      result.push_back(b);
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
        auto in_result = std::find_if(result.begin(), result.end(), [&b](const Block& r) { return r.a == b.a; });
        if (in_result == result.end())
          result.push_back(b);
      }
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
    storage_->write_buckets(j, start, to_write);
  }
}

}  // namespace roram
