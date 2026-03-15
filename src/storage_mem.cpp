#include "roram/storage.hpp"
#include <cstring>

namespace roram {

uint64_t MemoryStorage::level_offset(int j) const {
  // Opt 2: O(1) lookup via precomputed table (replaces O(h) loop).
  return level_offsets_[static_cast<size_t>(j)];
}

MemoryStorage::MemoryStorage(const Params& params, CryptoProvider* crypto)
    : params_(params), tag_size_(crypto ? crypto->tag_size() : 0), crypto_(crypto) {
  Bucket b(params.Z, params.B, params.ell + 1);
  bucket_plain_size_ = b.serialized_size(params_);
  bucket_storage_size_ = bucket_plain_size_ + tag_size_;
  // Opt 2: precompute cumulative byte offsets for each level.
  // level_offsets_[j] = sum_{k=0}^{j-1} 2^k * bucket_storage_size_ = (2^j - 1) * bucket_storage_size_
  level_offsets_.resize(static_cast<size_t>(params_.h + 2), 0);
  for (int j = 1; j <= params_.h + 1; ++j)
    level_offsets_[static_cast<size_t>(j)] =
        level_offsets_[static_cast<size_t>(j - 1)] + (1ULL << (j - 1)) * bucket_storage_size_;
  level_data_.resize(static_cast<size_t>(params_.h + 1));
  for (int j = 0; j <= params_.h; ++j) {
    uint64_t num_buckets = 1ULL << j;
    level_data_[static_cast<size_t>(j)].resize(num_buckets * bucket_storage_size_, 0);
  }
  // Opt 4: allocate scratch buffer once; reused for every bucket read.
  scratch_.resize(bucket_storage_size_, 0);
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
    // Opt 4: reuse pre-allocated scratch buffer; no heap alloc per bucket.
    std::memcpy(scratch_.data(), data.data() + pos, bucket_storage_size_);
    uint8_t* bucket_ptr = scratch_.data();
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
