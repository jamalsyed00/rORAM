#include "roram/storage.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdexcept>
#include <cstring>

namespace roram {

uint64_t FileStorage::level_offset(int j) const {
  uint64_t off = 0;
  for (int i = 0; i < j; ++i)
    off += (1ULL << i) * bucket_storage_size_;
  return off;
}

void FileStorage::ensure_open() {
  if (fd_ >= 0) return;
  fd_ = open(path_.c_str(), O_RDWR | O_CREAT, 0666);
  if (fd_ < 0)
    throw std::runtime_error("FileStorage: open failed: " + path_);
  last_offset_ = UINT64_MAX;
}

FileStorage::FileStorage(const Params& params, const std::string& path, bool count_seeks,
                         CryptoProvider* crypto)
    : params_(params), tag_size_(crypto ? crypto->tag_size() : 0), crypto_(crypto), path_(path),
      count_seeks_(count_seeks), seek_count_(0), fd_(-1), last_offset_(UINT64_MAX) {
  Bucket b(params.Z, params.B, params.ell + 1);
  bucket_plain_size_ = b.serialized_size(params_);
  bucket_storage_size_ = bucket_plain_size_ + tag_size_;
  ensure_open();
  uint64_t total = 0;
  for (int j = 0; j <= params_.h; ++j)
    total += (1ULL << j) * bucket_storage_size_;
  if (lseek(fd_, 0, SEEK_END) < static_cast<off_t>(total)) {
    if (ftruncate(fd_, static_cast<off_t>(total)) != 0)
      throw std::runtime_error("FileStorage: ftruncate failed");
  }
}

FileStorage::~FileStorage() {
  if (fd_ >= 0) { close(fd_); fd_ = -1; }
}

void FileStorage::read_buckets(int level, uint64_t start_bucket, uint64_t count,
                              std::vector<Bucket>& out) {
  ensure_open();
  uint64_t off = level_offset(level) + start_bucket * bucket_storage_size_;
  if (count_seeks_ && off != last_offset_ && last_offset_ != UINT64_MAX)
    ++seek_count_;
  last_offset_ = off + count * bucket_storage_size_;

  out.resize(count, Bucket(params_.Z, params_.B, params_.ell + 1));
  std::vector<uint8_t> buf(count * bucket_storage_size_);
  ssize_t n = pread(fd_, buf.data(), buf.size(), static_cast<off_t>(off));
  if (n != static_cast<ssize_t>(buf.size()))
    throw std::runtime_error("FileStorage: pread failed");
  for (uint64_t i = 0; i < count; ++i) {
    uint8_t* bucket_ptr = buf.data() + i * bucket_storage_size_;
    uint64_t bucket_id = ((1ULL << level) - 1) + start_bucket + i;
    if (crypto_) crypto_->decrypt(bucket_ptr, bucket_plain_size_, bucket_id, bucket_ptr + bucket_plain_size_);
    out[i].deserialize(bucket_ptr, params_);
  }
}

void FileStorage::write_buckets(int level, uint64_t start_bucket,
                               const std::vector<Bucket>& buckets) {
  ensure_open();
  uint64_t off = level_offset(level) + start_bucket * bucket_storage_size_;
  if (count_seeks_ && off != last_offset_ && last_offset_ != UINT64_MAX)
    ++seek_count_;
  last_offset_ = off + buckets.size() * bucket_storage_size_;

  std::vector<uint8_t> buf(buckets.size() * bucket_storage_size_);
  for (size_t i = 0; i < buckets.size(); ++i) {
    uint8_t* bucket_ptr = buf.data() + i * bucket_storage_size_;
    buckets[i].serialize(bucket_ptr, params_);
    uint64_t bucket_id = ((1ULL << level) - 1) + start_bucket + static_cast<uint64_t>(i);
    if (crypto_) crypto_->encrypt(bucket_ptr, bucket_plain_size_, bucket_id, bucket_ptr + bucket_plain_size_);
  }
  ssize_t n = pwrite(fd_, buf.data(), buf.size(), static_cast<off_t>(off));
  if (n != static_cast<ssize_t>(buf.size()))
    throw std::runtime_error("FileStorage: pwrite failed");
}

}  // namespace roram
