#include "roram/roram.hpp"
#include "roram/storage.hpp"
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace roram {

rORAM::rORAM(const Params& params, std::unique_ptr<CryptoProvider> crypto,
             bool use_memory_storage, const std::string& file_path, bool count_seeks)
    : params_(params), crypto_(std::move(crypto)) {
  int num_orams = params_.ell + 1;
  storages_.reserve(static_cast<size_t>(num_orams));
  sub_orams_.reserve(static_cast<size_t>(num_orams));
  for (int i = 0; i < num_orams; ++i) {
    if (use_memory_storage) {
      storages_.push_back(std::make_unique<MemoryStorage>(params_, crypto_.get()));
    } else {
      if (file_path.empty()) throw std::runtime_error("rORAM: file_path required for file storage");
      storages_.push_back(std::make_unique<FileStorage>(params_, file_path + "_tree" + std::to_string(i),
                                                        count_seeks, crypto_.get()));
    }
    sub_orams_.push_back(std::make_unique<SubORAM>(params_, i, storages_.back().get(), crypto_.get()));
  }
}

std::vector<std::vector<uint8_t>> rORAM::Access(uint64_t a, uint64_t r, const std::string& op,
                                                const std::vector<std::vector<uint8_t>>* D) {
  if (r == 0) return {};
  if (r > params_.L) throw std::runtime_error("rORAM::Access: r > L");
  if (a + r > params_.N) throw std::runtime_error("rORAM::Access: range out of bounds");
  int i = Params::range_exponent(r);
  if (i > params_.ell) i = params_.ell;
  uint64_t range_size = 1ULL << i;  // 2^i
  uint64_t a0 = (a / range_size) * range_size;
  uint64_t a1 = a0 + range_size;
  if (a1 > params_.N) a1 = a0;  // single range if at end

  SubORAM& Ri = *sub_orams_[static_cast<size_t>(i)];
  std::vector<Block> blocks_a0, blocks_a1;
  uint64_t p0_prime = 0, p1_prime = 0;
  Ri.ReadRange(a0, blocks_a0, p0_prime);
  if (a1 != a0)
    Ri.ReadRange(a1, blocks_a1, p1_prime);
  else
    p1_prime = p0_prime;

  std::vector<Block> all_blocks;
  all_blocks.reserve(blocks_a0.size() + blocks_a1.size());
  std::unordered_map<uint64_t, size_t> by_addr;
  by_addr.reserve(blocks_a0.size() + blocks_a1.size() + 8);
  for (const Block& b : blocks_a0) {
    by_addr.emplace(b.a, all_blocks.size());
    all_blocks.push_back(b);
  }
  for (const Block& b : blocks_a1) {
    if (by_addr.find(b.a) == by_addr.end()) {
      by_addr.emplace(b.a, all_blocks.size());
      all_blocks.push_back(b);
    }
  }
  std::sort(all_blocks.begin(), all_blocks.end(), [](const Block& x, const Block& y) { return x.a < y.a; });
  by_addr.clear();
  by_addr.reserve(all_blocks.size() + 8);
  for (size_t idx = 0; idx < all_blocks.size(); ++idx) by_addr.emplace(all_blocks[idx].a, idx);

  std::vector<std::unordered_map<uint64_t, uint64_t>> pm_cache(static_cast<size_t>(params_.ell + 1));
  for (Block& b : all_blocks) {
    // Keep path tags consistent across all sub-ORAMs, not only the active one.
    // This prevents stale copies from later being treated as current during merges.
    for (int j = 0; j <= params_.ell; ++j) {
      const uint64_t len_j = 1ULL << j;
      const uint64_t start_j = (b.a / len_j) * len_j;
      auto& cache_j = pm_cache[static_cast<size_t>(j)];
      uint64_t base_j = 0;
      auto itc = cache_j.find(start_j);
      if (itc != cache_j.end()) {
        base_j = itc->second;
      } else {
        base_j = sub_orams_[static_cast<size_t>(j)]->position_map().query(start_j);
        cache_j.emplace(start_j, base_j);
      }
      b.p[static_cast<size_t>(j)] = base_j + (b.a - start_j);
    }
    // Active level gets freshly sampled path starts from this access.
    if (b.a >= a0 && b.a < a0 + range_size)
      b.p[static_cast<size_t>(i)] = p0_prime + (b.a - a0);
    else if (b.a >= a1 && b.a < a1 + range_size)
      b.p[static_cast<size_t>(i)] = p1_prime + (b.a - a1);
  }

  if (op == "write" && D) {
    for (uint64_t addr = a; addr < a + r && addr < params_.N; ++addr) {
      size_t idx = addr - a;
      if (idx < D->size()) {
        auto it = by_addr.find(addr);
        if (it != by_addr.end() && all_blocks[it->second].data.size() == (*D)[idx].size())
          memcpy(all_blocks[it->second].data.data(), (*D)[idx].data(), (*D)[idx].size());
      }
    }
  }

  for (int j = 0; j <= params_.ell; ++j) {
    SubORAM& Rj = *sub_orams_[static_cast<size_t>(j)];
    auto& stash = Rj.stash();
    stash.erase(std::remove_if(stash.begin(), stash.end(),
      [a0, range_size](const Block& b) {
        return b.a >= a0 && b.a < a0 + 2 * range_size;
      }), stash.end());
    for (Block& b : all_blocks)
      stash.push_back(b);
    Rj.BatchEvict(2 * range_size, cnt_);
  }
  cnt_ += 2 * range_size;

  if (op == "read") {
    std::vector<std::vector<uint8_t>> result;
    result.reserve(r);
    for (uint64_t addr = a; addr < a + r; ++addr) {
      auto it = by_addr.find(addr);
      if (it != by_addr.end())
        result.push_back(all_blocks[it->second].data);
      else
        result.push_back(std::vector<uint8_t>(params_.B, 0));
    }
    return result;
  }
  return {};
}

uint64_t rORAM::get_seek_count() const {
  uint64_t total = 0;
  for (const auto& s : storages_)
    total += s->get_seek_count();
  return total;
}

}  // namespace roram
