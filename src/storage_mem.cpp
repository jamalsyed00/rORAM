#include "roram/storage.hpp"

namespace roram {

uint64_t MemoryStorage::level_offset(int j) const {
  uint64_t off = 0;
  for (int i = 0; i < j; ++i)
    off += (1ULL << i) * bucket_byte_size_;
  return off;
}

MemoryStorage::MemoryStorage(const Params& params) : params_(params) {
  Bucket b(params.Z, params.B, params.ell + 1);
  bucket_byte_size_ = b.serialized_size(params_);
  level_data_.resize(static_cast<size_t>(params_.h + 1));
  for (int j = 0; j <= params_.h; ++j) {
    uint64_t num_buckets = 1ULL << j;
    level_data_[static_cast<size_t>(j)].resize(num_buckets * bucket_byte_size_, 0);
  }
}

void MemoryStorage::read_buckets(int level, uint64_t start_bucket, uint64_t count,
                                 std::vector<Bucket>& out) {
  uint64_t off = level_offset(level) + start_bucket * bucket_byte_size_;
  uint64_t request_size = count * bucket_byte_size_;
  if (last_offset_ != static_cast<uint64_t>(-1) && off != last_offset_)
    ++seek_count_;
  last_offset_ = off + request_size;

  out.resize(count, Bucket(params_.Z, params_.B, params_.ell + 1));
  std::vector<uint8_t>& data = level_data_[static_cast<size_t>(level)];
  for (uint64_t i = 0; i < count; ++i) {
    size_t pos = (start_bucket + i) * bucket_byte_size_;
    if (pos + bucket_byte_size_ > data.size()) break;
    out[i].deserialize(data.data() + pos, params_);
  }
}

void MemoryStorage::write_buckets(int level, uint64_t start_bucket,
                                  const std::vector<Bucket>& buckets) {
  uint64_t off = level_offset(level) + start_bucket * bucket_byte_size_;
  uint64_t request_size = buckets.size() * bucket_byte_size_;
  if (last_offset_ != static_cast<uint64_t>(-1) && off != last_offset_)
    ++seek_count_;
  last_offset_ = off + request_size;

  std::vector<uint8_t>& data = level_data_[static_cast<size_t>(level)];
  for (size_t i = 0; i < buckets.size(); ++i) {
    size_t pos = (start_bucket + i) * bucket_byte_size_;
    if (pos + bucket_byte_size_ > data.size()) break;
    buckets[i].serialize(data.data() + pos, params_);
  }
}

}  // namespace roram
