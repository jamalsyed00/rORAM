#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>

namespace roram {

// Abstract crypto: encrypt/decrypt blocks, and RNG for path sampling
class CryptoProvider {
 public:
  virtual ~CryptoProvider() = default;
  virtual void encrypt(uint8_t* data, size_t len, uint64_t block_id) = 0;
  virtual void decrypt(uint8_t* data, size_t len, uint64_t block_id) = 0;
  virtual uint64_t random_path(uint64_t N) = 0;  // uniform in [0, N)
};

// OpenSSL-based implementation (AES-128-CTR, RAND_bytes)
class OpenSSLCrypto : public CryptoProvider {
 public:
  explicit OpenSSLCrypto(const std::vector<uint8_t>& key);
  void encrypt(uint8_t* data, size_t len, uint64_t block_id) override;
  void decrypt(uint8_t* data, size_t len, uint64_t block_id) override;
  uint64_t random_path(uint64_t N) override;

 private:
  std::vector<uint8_t> key_;
  void ctr_crypt(uint8_t* data, size_t len, uint64_t block_id, bool encrypt);
};

// No-op crypto for testing (no encryption, deterministic RNG)
class NoOpCrypto : public CryptoProvider {
 public:
  NoOpCrypto() : seed_(0) {}
  void encrypt(uint8_t* data, size_t len, uint64_t) override { (void)data; (void)len; }
  void decrypt(uint8_t* data, size_t len, uint64_t) override { (void)data; (void)len; }
  uint64_t random_path(uint64_t N) override;

 private:
  uint64_t seed_;
};

}  // namespace roram
