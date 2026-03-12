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

void OpenSSLCrypto::gcm_crypt(uint8_t* data, size_t len, uint64_t block_id, bool do_encrypt,
                              uint8_t* tag_out, const uint8_t* tag_in) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
  uint8_t iv[12] = {0};
  memcpy(iv, &block_id, 8);
  if (EVP_CipherInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr, do_encrypt) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EVP_CipherInit_ex(gcm) failed");
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(iv), nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EVP_CTRL_GCM_SET_IVLEN failed");
  }
  if (EVP_CipherInit_ex(ctx, nullptr, nullptr, key_.data(), iv, do_encrypt) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EVP_CipherInit_ex(key/iv) failed");
  }
  int outl = 0;
  if (EVP_CipherUpdate(ctx, data, &outl, data, static_cast<int>(len)) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("EVP_CipherUpdate failed");
  }
  if (!do_encrypt && tag_in) {
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t*>(tag_in)) != 1) {
      EVP_CIPHER_CTX_free(ctx);
      throw std::runtime_error("EVP_CTRL_GCM_SET_TAG failed");
    }
  }
  int finl = 0;
  if (EVP_CipherFinal_ex(ctx, data + outl, &finl) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("OpenSSLCrypto: GCM authentication failed");
  }
  if (do_encrypt && tag_out) {
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag_out) != 1) {
      EVP_CIPHER_CTX_free(ctx);
      throw std::runtime_error("EVP_CTRL_GCM_GET_TAG failed");
    }
  }
  EVP_CIPHER_CTX_free(ctx);
}

void OpenSSLCrypto::encrypt(uint8_t* data, size_t len, uint64_t block_id, uint8_t* tag_out) {
  gcm_crypt(data, len, block_id, true, tag_out, nullptr);
}

void OpenSSLCrypto::decrypt(uint8_t* data, size_t len, uint64_t block_id, const uint8_t* tag_in) {
  gcm_crypt(data, len, block_id, false, nullptr, tag_in);
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
