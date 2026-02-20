#include "roram/roram.hpp"
#include "roram/storage.hpp"
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace roram {

rORAM::rORAM(const Params& params, std::unique_ptr<CryptoProvider> crypto,
             bool use_memory_storage, const std::string& file_path, bool count_seeks)
    : params_(params), crypto_(std::move(crypto)) {
  int num_orams = params_.ell + 1;
  storages_.reserve(static_cast<size_t>(num_orams));
  sub_orams_.reserve(static_cast<size_t>(num_orams));
  for (int i = 0; i < num_orams; ++i) {
    if (use_memory_storage) {
      storages_.push_back(std::make_unique<MemoryStorage>(params_));
    } else {
      if (file_path.empty()) throw std::runtime_error("rORAM: file_path required for file storage");
      storages_.push_back(std::make_unique<FileStorage>(params_, file_path + "_tree" + std::to_string(i), count_seeks));
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
  for (const Block& b : blocks_a0) all_blocks.push_back(b);
  for (const Block& b : blocks_a1) {
    if (std::find_if(all_blocks.begin(), all_blocks.end(), [&b](const Block& x) { return x.a == b.a; }) == all_blocks.end())
      all_blocks.push_back(b);
  }
  std::sort(all_blocks.begin(), all_blocks.end(), [](const Block& x, const Block& y) { return x.a < y.a; });

  for (Block& b : all_blocks) {
    if (b.a >= a0 && b.a < a0 + range_size)
      b.p[static_cast<size_t>(i)] = p0_prime + (b.a - a0);
    else if (b.a >= a1 && b.a < a1 + range_size)
      b.p[static_cast<size_t>(i)] = p1_prime + (b.a - a1);
  }

  if (op == "write" && D) {
    for (uint64_t addr = a; addr < a + r && addr < params_.N; ++addr) {
      size_t idx = addr - a;
      if (idx < D->size()) {
        auto it = std::find_if(all_blocks.begin(), all_blocks.end(), [addr](const Block& x) { return x.a == addr; });
        if (it != all_blocks.end() && it->data.size() == (*D)[idx].size())
          memcpy(it->data.data(), (*D)[idx].data(), (*D)[idx].size());
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
      auto it = std::find_if(all_blocks.begin(), all_blocks.end(), [addr](const Block& x) { return x.a == addr; });
      if (it != all_blocks.end())
        result.push_back(it->data);
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
