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
  virtual size_t tag_size() const { return 0; }
  virtual void encrypt(uint8_t* data, size_t len, uint64_t block_id, uint8_t* tag_out) = 0;
  virtual void decrypt(uint8_t* data, size_t len, uint64_t block_id, const uint8_t* tag_in) = 0;
  virtual uint64_t random_path(uint64_t N) = 0;  // uniform in [0, N)
};

// OpenSSL-based implementation (AES-128-GCM, RAND_bytes)
class OpenSSLCrypto : public CryptoProvider {
 public:
  explicit OpenSSLCrypto(const std::vector<uint8_t>& key);
  size_t tag_size() const override { return 16; }
  void encrypt(uint8_t* data, size_t len, uint64_t block_id, uint8_t* tag_out) override;
  void decrypt(uint8_t* data, size_t len, uint64_t block_id, const uint8_t* tag_in) override;
  uint64_t random_path(uint64_t N) override;

 private:
  std::vector<uint8_t> key_;
  void gcm_crypt(uint8_t* data, size_t len, uint64_t block_id, bool encrypt,
                 uint8_t* tag_out, const uint8_t* tag_in);
};

// No-op crypto for testing (no encryption, deterministic RNG)
class NoOpCrypto : public CryptoProvider {
 public:
  NoOpCrypto() : seed_(0) {}
  void encrypt(uint8_t* data, size_t len, uint64_t, uint8_t*) override { (void)data; (void)len; }
  void decrypt(uint8_t* data, size_t len, uint64_t, const uint8_t*) override { (void)data; (void)len; }
  uint64_t random_path(uint64_t N) override;

 private:
  uint64_t seed_;
};

}  // namespace roram
