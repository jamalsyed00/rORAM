#include "roram/storage.hpp"
#include <cstring>

namespace roram {

uint64_t MemoryStorage::level_offset(int j) const {
  uint64_t off = 0;
  for (int i = 0; i < j; ++i)
    off += (1ULL << i) * bucket_storage_size_;
  return off;
}

MemoryStorage::MemoryStorage(const Params& params, CryptoProvider* crypto)
    : params_(params), tag_size_(crypto ? crypto->tag_size() : 0), crypto_(crypto) {
  Bucket b(params.Z, params.B, params.ell + 1);
  bucket_plain_size_ = b.serialized_size(params_);
  bucket_storage_size_ = bucket_plain_size_ + tag_size_;
  level_data_.resize(static_cast<size_t>(params_.h + 1));
  for (int j = 0; j <= params_.h; ++j) {
    uint64_t num_buckets = 1ULL << j;
    level_data_[static_cast<size_t>(j)].resize(num_buckets * bucket_storage_size_, 0);
  }
}

void MemoryStorage::read_buckets(int level, uint64_t start_bucket, uint64_t count,
                                 std::vector<Bucket>& out) {
  uint64_t off = level_offset(level) + start_bucket * bucket_storage_size_;
  uint64_t request_size = count * bucket_storage_size_;
  if (last_offset_ != static_cast<uint64_t>(-1) && off != last_offset_)
    ++seek_count_;
  last_offset_ = off + request_size;

  out.resize(count, Bucket(params_.Z, params_.B, params_.ell + 1));
  std::vector<uint8_t>& data = level_data_[static_cast<size_t>(level)];
  for (uint64_t i = 0; i < count; ++i) {
    size_t pos = (start_bucket + i) * bucket_storage_size_;
    if (pos + bucket_storage_size_ > data.size()) break;
    std::vector<uint8_t> bucket_buf(bucket_storage_size_, 0);
    std::memcpy(bucket_buf.data(), data.data() + pos, bucket_storage_size_);
    uint8_t* bucket_ptr = bucket_buf.data();
    uint64_t bucket_id = ((1ULL << level) - 1) + start_bucket + i;
    if (crypto_) crypto_->decrypt(bucket_ptr, bucket_plain_size_, bucket_id, bucket_ptr + bucket_plain_size_);
    out[i].deserialize(bucket_ptr, params_);
  }
}

void MemoryStorage::write_buckets(int level, uint64_t start_bucket,
                                  const std::vector<Bucket>& buckets) {
  uint64_t off = level_offset(level) + start_bucket * bucket_storage_size_;
  uint64_t request_size = buckets.size() * bucket_storage_size_;
  if (last_offset_ != static_cast<uint64_t>(-1) && off != last_offset_)
    ++seek_count_;
  last_offset_ = off + request_size;

  std::vector<uint8_t>& data = level_data_[static_cast<size_t>(level)];
  for (size_t i = 0; i < buckets.size(); ++i) {
    size_t pos = (start_bucket + i) * bucket_storage_size_;
    if (pos + bucket_storage_size_ > data.size()) break;
    uint8_t* bucket_ptr = data.data() + pos;
    buckets[i].serialize(bucket_ptr, params_);
    uint64_t bucket_id = ((1ULL << level) - 1) + start_bucket + static_cast<uint64_t>(i);
    if (crypto_) crypto_->encrypt(bucket_ptr, bucket_plain_size_, bucket_id, bucket_ptr + bucket_plain_size_);
  }
}

}  // namespace roram
