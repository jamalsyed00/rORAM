#pragma once

#include "roram/types.hpp"
#include "roram/block.hpp"
#include "roram/storage.hpp"
#include "roram/crypto.hpp"
#include <memory>
#include <string>
#include <vector>

namespace roram {

class PathORAM {
 public:
  PathORAM(const Params& params, std::unique_ptr<CryptoProvider> crypto,
           bool use_memory_storage = true, const std::string& file_path = "",
           bool count_seeks = false);
  ~PathORAM() = default;

  std::vector<uint8_t> Access(uint64_t block_id, const std::string& op,
                              const std::vector<uint8_t>* write_data = nullptr);
  uint64_t get_seek_count() const;
  uint64_t debug_position(uint64_t block_id) const;

 private:
  Params params_;
  std::unique_ptr<CryptoProvider> crypto_;
  std::unique_ptr<StorageBackend> storage_;
  std::vector<uint64_t> position_map_;
  std::vector<Block> stash_;

  void read_path_into_stash(uint64_t leaf);
  void evict_path(uint64_t leaf);
  static bool block_on_path(uint64_t block_leaf, uint64_t access_leaf, int level, int h);
};

}  // namespace roram
