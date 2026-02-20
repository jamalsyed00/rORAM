#include "roram/crypto.hpp"

#ifdef RORAM_USE_OPENSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#endif

#include <stdexcept>
#include <algorithm>

namespace roram {

#ifdef RORAM_USE_OPENSSL
OpenSSLCrypto::OpenSSLCrypto(const std::vector<uint8_t>& key) : key_(key) {
  if (key_.size() != 16) throw std::runtime_error("OpenSSLCrypto: key must be 16 bytes");
}

void OpenSSLCrypto::ctr_crypt(uint8_t* data, size_t len, uint64_t block_id, bool do_encrypt) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
  uint8_t iv[16] = {0};
  memcpy(iv, &block_id, 8);
  if (EVP_CipherInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key_.data(), iv, do_encrypt) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EVP_CipherInit_ex failed");
  }
  int outl = 0;
  if (EVP_CipherUpdate(ctx, data, &outl, data, static_cast<int>(len)) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EVP_CipherUpdate failed");
  }
  EVP_CIPHER_CTX_free(ctx);
}

void OpenSSLCrypto::encrypt(uint8_t* data, size_t len, uint64_t block_id) {
  ctr_crypt(data, len, block_id, true);
}

void OpenSSLCrypto::decrypt(uint8_t* data, size_t len, uint64_t block_id) {
  ctr_crypt(data, len, block_id, false);
}

uint64_t OpenSSLCrypto::random_path(uint64_t N) {
  if (N == 0) return 0;
  uint64_t r;
  do {
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&r), 8) != 1)
      throw std::runtime_error("RAND_bytes failed");
  } while (r >= (UINT64_MAX - (UINT64_MAX % N)) || r >= N);
  return r % N;
}
#endif

uint64_t NoOpCrypto::random_path(uint64_t N) {
  if (N == 0) return 0;
  seed_ = seed_ * 6364136223846793005ULL + 1442695040888963407ULL;
  return seed_ % N;
}

}  // namespace roram
