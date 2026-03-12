#include "roram/path_oram.hpp"
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace roram {

PathORAM::PathORAM(const Params& params, std::unique_ptr<CryptoProvider> crypto,
                   bool use_memory_storage, const std::string& file_path, bool count_seeks)
    : params_(params), crypto_(std::move(crypto)) {
  if (params_.L != 1) {
    throw std::runtime_error("PathORAM: expected L=1");
  }
  if (params_.N == 0) {
    throw std::runtime_error("PathORAM: N must be > 0");
  }
  position_map_.assign(params_.N, 0);
  for (uint64_t a = 0; a < params_.N; ++a) {
    position_map_[a] = crypto_->random_path(params_.N);
  }
  if (use_memory_storage) {
    storage_ = std::make_unique<MemoryStorage>(params_, crypto_.get());
  } else {
    if (file_path.empty()) throw std::runtime_error("PathORAM: file_path required for file storage");
    storage_ = std::make_unique<FileStorage>(params_, file_path, count_seeks, crypto_.get());
  }
}

bool PathORAM::block_on_path(uint64_t block_leaf, uint64_t access_leaf, int level, int h) {
  (void)h;
  return (block_leaf % (1ULL << level)) == (access_leaf % (1ULL << level));
}

void PathORAM::read_path_into_stash(uint64_t leaf) {
  for (int level = 0; level <= params_.h; ++level) {
    uint64_t bucket = leaf % (1ULL << level);
    std::vector<Bucket> fetched;
    storage_->read_buckets(level, bucket, 1, fetched);
    for (const Block& b : fetched[0].blocks) {
      if (!b.valid()) continue;
      auto it = std::find_if(stash_.begin(), stash_.end(), [&b](const Block& x) { return x.a == b.a; });
      if (it == stash_.end()) stash_.push_back(b);
    }
  }
}

void PathORAM::evict_path(uint64_t leaf) {
  for (int level = params_.h; level >= 0; --level) {
    Bucket out(params_.Z, params_.B, params_.ell + 1);
    size_t inserted = 0;
    for (auto it = stash_.begin(); it != stash_.end() && inserted < static_cast<size_t>(params_.Z);) {
      uint64_t b_leaf = it->p[0];
      if (block_on_path(b_leaf, leaf, level, params_.h)) {
        out.blocks[inserted++] = *it;
        it = stash_.erase(it);
      } else {
        ++it;
      }
    }
    for (size_t i = inserted; i < out.blocks.size(); ++i) out.blocks[i].set_dummy();
    uint64_t bucket = leaf % (1ULL << level);
    std::vector<Bucket> one{out};
    storage_->write_buckets(level, bucket, one);
  }
}

std::vector<uint8_t> PathORAM::Access(uint64_t block_id, const std::string& op,
                                      const std::vector<uint8_t>* write_data) {
  if (block_id >= params_.N) throw std::runtime_error("PathORAM::Access: block_id out of bounds");
  if (op != "read" && op != "write") throw std::runtime_error("PathORAM::Access: op must be read/write");
  uint64_t old_leaf = position_map_[block_id];
  uint64_t new_leaf = crypto_->random_path(params_.N);
  position_map_[block_id] = new_leaf;

  read_path_into_stash(old_leaf);

  auto it = std::find_if(stash_.begin(), stash_.end(), [block_id](const Block& b) { return b.a == block_id; });
  if (it == stash_.end()) {
    Block b(params_.B, params_.ell + 1);
    b.a = block_id;
    b.p[0] = old_leaf;
    stash_.push_back(b);
    it = stash_.end() - 1;
  }

  std::vector<uint8_t> result = it->data;
  if (op == "write") {
    if (!write_data || write_data->size() != params_.B)
      throw std::runtime_error("PathORAM::Access: write_data must have size B");
    std::memcpy(it->data.data(), write_data->data(), params_.B);
    result = it->data;
  }
  it->p[0] = new_leaf;

  evict_path(old_leaf);
  return result;
}

uint64_t PathORAM::get_seek_count() const {
  return storage_->get_seek_count();
}

uint64_t PathORAM::debug_position(uint64_t block_id) const {
  if (block_id >= position_map_.size()) throw std::runtime_error("PathORAM::debug_position: block_id out of bounds");
  return position_map_[block_id];
}

}  // namespace roram
