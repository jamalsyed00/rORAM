#pragma once

#include "roram/types.hpp"
#include "roram/block.hpp"
#include "roram/storage.hpp"
#include "roram/sub_oram.hpp"
#include "roram/crypto.hpp"
#include <memory>
#include <vector>
#include <string>

namespace roram {

class rORAM {
 public:
  rORAM(const Params& params, std::unique_ptr<CryptoProvider> crypto,
        bool use_memory_storage = true, const std::string& file_path = "",
        bool count_seeks = false);
  ~rORAM() = default;

  // Access range [a, a+r): op is "read" or "write". For write, D provides new data for [a, a+r).
  // Returns read data when op is read (size r blocks).
  std::vector<std::vector<uint8_t>> Access(uint64_t a, uint64_t r, const std::string& op,
                                           const std::vector<std::vector<uint8_t>>* D = nullptr);
  uint64_t get_seek_count() const;

 private:
  Params params_;
  std::unique_ptr<CryptoProvider> crypto_;
  std::vector<std::unique_ptr<StorageBackend>> storages_;
  std::vector<std::unique_ptr<SubORAM>> sub_orams_;
  uint64_t cnt_{0};  // global eviction counter
};

}  // namespace roram
