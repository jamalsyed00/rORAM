#pragma once

#include "roram/types.hpp"
#include "roram/block.hpp"
#include <memory>
#include <string>
#include <vector>

namespace roram {

// Abstract storage: read/write buckets by (level, bucket_index). Level j has 2^j buckets.
class StorageBackend {
 public:
  virtual ~StorageBackend() = default;
  virtual void read_buckets(int level, uint64_t start_bucket, uint64_t count,
                            std::vector<Bucket>& out) = 0;
  virtual void write_buckets(int level, uint64_t start_bucket,
                             const std::vector<Bucket>& buckets) = 0;
  virtual uint64_t bucket_byte_size() const = 0;
  // Optional: increment seek count when read/write is non-sequential
  virtual uint64_t get_seek_count() const { return 0; }
};

// In-memory storage: one contiguous buffer per level; optionally counts seeks (non-sequential access)
class MemoryStorage : public StorageBackend {
 public:
  MemoryStorage(const Params& params);
  void read_buckets(int level, uint64_t start_bucket, uint64_t count,
                    std::vector<Bucket>& out) override;
  void write_buckets(int level, uint64_t start_bucket,
                    const std::vector<Bucket>& buckets) override;
  uint64_t bucket_byte_size() const override { return bucket_byte_size_; }
  uint64_t get_seek_count() const override { return seek_count_; }

 private:
  Params params_;
  uint64_t bucket_byte_size_;
  std::vector<std::vector<uint8_t>> level_data_;
  mutable uint64_t seek_count_{0};
  mutable uint64_t last_offset_{static_cast<uint64_t>(-1)};  // byte after last request
  uint64_t level_offset(int j) const;
};

// File-backed storage: single file or one file per level; optional seek counting
class FileStorage : public StorageBackend {
 public:
  FileStorage(const Params& params, const std::string& path, bool count_seeks = false);
  ~FileStorage();
  void read_buckets(int level, uint64_t start_bucket, uint64_t count,
                    std::vector<Bucket>& out) override;
  void write_buckets(int level, uint64_t start_bucket,
                    const std::vector<Bucket>& buckets) override;
  uint64_t bucket_byte_size() const override { return bucket_byte_size_; }
  uint64_t get_seek_count() const override { return seek_count_; }

 private:
  Params params_;
  uint64_t bucket_byte_size_;
  std::string path_;
  bool count_seeks_;
  mutable uint64_t seek_count_;
  int fd_;
  uint64_t last_offset_;
  uint64_t level_offset(int j) const;
  void ensure_open();
};

}  // namespace roram
