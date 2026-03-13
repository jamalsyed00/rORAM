#include "roram/block.hpp"
#include "roram/path_oram.hpp"
#include "roram/position_map.hpp"
#include "roram/roram.hpp"
#include "roram/crypto.hpp"
#include "roram/storage.hpp"
#include "roram/types.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<uint8_t> make_data(size_t n, uint8_t seed) {
  std::vector<uint8_t> out(n, 0);
  for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(seed + i);
  return out;
}

static void expect_throw(const std::function<void()>& fn) {
  bool threw = false;
  try {
    fn();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
}

static bool eq_block(const roram::Block& a, const roram::Block& b) {
  return a.a == b.a && a.data == b.data && a.p == b.p;
}

static void test_params_helpers() {
  roram::Params p(17, 9, 4, 64);
  assert(p.ell == 4);
  assert(p.h == 5);
  assert(roram::Params::range_exponent(1) == 0);
  assert(roram::Params::range_exponent(9) == 4);
  assert(roram::Params::range_power2(1) == 1);
  assert(roram::Params::range_power2(9) == 16);
}

static void test_block_bucket_roundtrip() {
  roram::Params p(32, 8, 4, 64);
  roram::Block b(p.B, p.ell + 1);
  b.a = 7;
  b.data = make_data(p.B, 13);
  for (size_t i = 0; i < b.p.size(); ++i) b.p[i] = static_cast<uint64_t>(i + 10);

  std::vector<uint8_t> buf_block(b.serialized_size(p), 0);
  b.serialize(buf_block.data(), p);
  roram::Block b2(p.B, p.ell + 1);
  b2.deserialize(buf_block.data(), p);
  assert(eq_block(b, b2));

  roram::Bucket bucket(p.Z, p.B, p.ell + 1);
  bucket.blocks[0] = b;
  bucket.blocks[1].a = 3;
  bucket.blocks[1].data = make_data(p.B, 99);
  bucket.blocks[1].p[0] = 42;
  bucket.blocks[2].set_dummy();
  bucket.blocks[3].set_dummy();
  std::vector<uint8_t> buf_bucket(bucket.serialized_size(p), 0);
  bucket.serialize(buf_bucket.data(), p);
  roram::Bucket out(p.Z, p.B, p.ell + 1);
  out.deserialize(buf_bucket.data(), p);
  assert(eq_block(bucket.blocks[0], out.blocks[0]));
  assert(eq_block(bucket.blocks[1], out.blocks[1]));
}

static void test_position_map_basic() {
  roram::PositionMap pm(16, 2);  // stride 4
  assert(pm.query(0) == 0);
  pm.update(0, 9);
  pm.update(4, 12);
  assert(pm.query(0) == 9);
  assert(pm.query(4) == 12);
  assert(pm.query(1000) == 0);
}

static void test_memory_storage_roundtrip() {
  roram::Params p(32, 8, 4, 64);
  roram::MemoryStorage storage(p);
  roram::Bucket in(p.Z, p.B, p.ell + 1);
  in.blocks[0].a = 5;
  in.blocks[0].data = make_data(p.B, 21);
  in.blocks[0].p[0] = 7;
  std::vector<roram::Bucket> write_vec{in};
  storage.write_buckets(2, 1, write_vec);

  std::vector<roram::Bucket> out;
  storage.read_buckets(2, 1, 1, out);
  assert(out.size() == 1);
  assert(eq_block(in.blocks[0], out[0].blocks[0]));
}

static void test_file_storage_roundtrip_and_seeks() {
  roram::Params p(32, 8, 4, 64);
  std::string path = "/tmp/roram_tests_storage.bin";
  std::remove(path.c_str());
  roram::FileStorage storage(p, path, true);
  roram::Bucket b0(p.Z, p.B, p.ell + 1);
  b0.blocks[0].a = 11;
  b0.blocks[0].data = make_data(p.B, 2);
  std::vector<roram::Bucket> write_vec{b0};
  storage.write_buckets(1, 0, write_vec);
  storage.write_buckets(3, 2, write_vec);

  std::vector<roram::Bucket> out;
  storage.read_buckets(1, 0, 1, out);
  assert(out.size() == 1);
  assert(eq_block(b0.blocks[0], out[0].blocks[0]));
  assert(storage.get_seek_count() > 0);
  std::remove(path.c_str());
}

static void test_path_oram_write_read() {
  roram::Params params(16, 1, 4, 64);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::PathORAM poram(params, std::move(crypto), true);
  auto w = make_data(params.B, 7);
  poram.Access(3, "write", &w);
  auto r = poram.Access(3, "read");
  assert(r == w);
}

static void test_position_map_updates() {
  roram::Params params(1024, 1, 4, 32);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::PathORAM poram(params, std::move(crypto), true);
  uint64_t before = poram.debug_position(9);
  poram.Access(9, "read");
  uint64_t after = poram.debug_position(9);
  assert(before != after);
}

static void test_path_oram_overwrite() {
  roram::Params params(32, 1, 4, 64);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::PathORAM poram(params, std::move(crypto), true);
  auto first = make_data(params.B, 1);
  auto second = make_data(params.B, 55);
  poram.Access(10, "write", &first);
  poram.Access(10, "write", &second);
  assert(poram.Access(10, "read") == second);
}

static void test_stash_resilience_hot_blocks() {
  roram::Params params(64, 1, 4, 32);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::PathORAM poram(params, std::move(crypto), true);

  auto hot = make_data(params.B, 22);
  auto cold = make_data(params.B, 91);
  poram.Access(5, "write", &hot);
  poram.Access(21, "write", &cold);
  for (int i = 0; i < 100; ++i) {
    poram.Access(5, "read");
    poram.Access(21, "read");
  }
  assert(poram.Access(5, "read") == hot);
  assert(poram.Access(21, "read") == cold);
}

static void test_backend_parity() {
  roram::Params params(32, 1, 4, 64);
  std::string path = "/tmp/roram_tests_backend.bin";
  std::remove(path.c_str());

  auto mem_crypto = std::make_unique<roram::NoOpCrypto>();
  auto file_crypto = std::make_unique<roram::NoOpCrypto>();
  roram::PathORAM mem(params, std::move(mem_crypto), true);
  roram::PathORAM file(params, std::move(file_crypto), false, path, true);

  for (uint64_t a = 0; a < params.N; ++a) {
    auto w = make_data(params.B, static_cast<uint8_t>(a));
    mem.Access(a, "write", &w);
    file.Access(a, "write", &w);
  }
  for (uint64_t a = 0; a < params.N; ++a) {
    auto mr = mem.Access(a, "read");
    auto fr = file.Access(a, "read");
    assert(mr == fr);
  }
  std::remove(path.c_str());
}

static void test_path_oram_errors() {
  roram::Params params(16, 1, 4, 32);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::PathORAM poram(params, std::move(crypto), true);
  auto ok = make_data(params.B, 4);
  auto bad = make_data(params.B - 1, 7);
  expect_throw([&]() { poram.Access(100, "read"); });
  expect_throw([&]() { poram.Access(1, "invalid-op"); });
  expect_throw([&]() { poram.Access(1, "write", nullptr); });
  expect_throw([&]() { poram.Access(1, "write", &bad); });
  poram.Access(1, "write", &ok);
}

static void test_roram_boundaries() {
  roram::Params params(16, 8, 4, 32);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::rORAM ram(params, std::move(crypto), true);
  auto d = std::vector<std::vector<uint8_t>>(8, make_data(params.B, 11));
  ram.Access(0, 8, "write", &d);
  auto out = ram.Access(0, 8, "read");
  assert(out.size() == 8);
  auto tail = ram.Access(15, 1, "read");
  assert(tail.size() == 1);
}

static void test_roram_errors_and_seek_counter() {
  roram::Params params(32, 8, 4, 32);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::rORAM ram(params, std::move(crypto), true);
  expect_throw([&]() { ram.Access(0, 9, "read"); });
  expect_throw([&]() { ram.Access(31, 2, "read"); });

  auto empty = ram.Access(0, 0, "read");
  assert(empty.empty());

  std::string path = "/tmp/roram_tests_roram_file";
  auto crypto_file = std::make_unique<roram::NoOpCrypto>();
  roram::rORAM ram_file(params, std::move(crypto_file), false, path, true);
  ram_file.Access(0, 1, "read");
  ram_file.Access(2, 2, "read");
  assert(ram_file.get_seek_count() > 0);
}

// Randomized reference-model correctness test.
// Maintains a ground-truth array and verifies every rORAM read matches it.
static void test_roram_reference_model_random() {
  // Parameters: small N & B for speed; L covers range sizes 1..16.
  const uint64_t N = 128;
  const uint64_t L = 16;
  const int      Z = 4;
  const size_t   B = 32;
  const int    OPS = 300;

  roram::Params params(N, L, Z, B);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::rORAM ram(params, std::move(crypto), true);

  // Ground truth: indexed by logical address.
  std::vector<std::vector<uint8_t>> ref(N, std::vector<uint8_t>(B, 0));

  // Simple deterministic PRNG (xorshift64).
  uint64_t rng = 0xdeadbeef12345678ULL;
  auto next_rng = [&]() -> uint64_t {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return rng;
  };

  // Range sizes to test (powers of 2, all ≤ L).
  const uint64_t range_sizes[] = {1, 2, 4, 8, 16};
  const int num_rsizes = 5;

  for (int op = 0; op < OPS; ++op) {
    uint64_t r   = range_sizes[next_rng() % num_rsizes];
    uint64_t max_a = (N > r) ? (N - r) : 0;
    uint64_t a   = (max_a > 0) ? (next_rng() % max_a) : 0;
    bool do_write = (next_rng() % 2 == 0);

    if (do_write) {
      // Build write data deterministic on (op, a).
      std::vector<std::vector<uint8_t>> D(r, std::vector<uint8_t>(B));
      for (uint64_t k = 0; k < r; ++k) {
        uint8_t seed = static_cast<uint8_t>((op * 7 + a + k) & 0xFF);
        for (size_t b = 0; b < B; ++b)
          D[k][b] = static_cast<uint8_t>((seed + b) & 0xFF);
        ref[a + k] = D[k];
      }
      ram.Access(a, r, "write", &D);
    } else {
      auto result = ram.Access(a, r, "read");
      assert(result.size() == r);
      for (uint64_t k = 0; k < r; ++k) {
        assert(result[k] == ref[a + k]);
      }
    }
  }
}

static void test_cli_smoke() {
  int rc1 = std::system("./roram_main read 16 8 0 1 >/dev/null");
  int rc2 = std::system("./roram_main write 16 8 0 1 >/dev/null");
  int rc3 = std::system("./roram_main compare --N 16 --L 8 --trials 1 >/dev/null");
  std::string trace = "/tmp/roram_workload_trace.csv";
  {
    std::ofstream out(trace);
    out << "op,a,r\n";
    out << "read,0,1\n";
    out << "write,4,2\n";
    out << "read,4,2\n";
  }
  int rc4 = std::system("./roram_main workload --N 16 --L 8 --trace /tmp/roram_workload_trace.csv --csv /tmp/roram_workload_trace_out.csv >/dev/null");
  assert(rc1 == 0);
  assert(rc2 == 0);
  assert(rc3 == 0);
  assert(rc4 == 0);
}

static void test_noop_encrypt_roundtrip() {
  roram::NoOpCrypto crypto;
  std::vector<uint8_t> buf = make_data(64, 5);
  std::vector<uint8_t> original = buf;
  crypto.encrypt(buf.data(), buf.size(), 3, nullptr);
  crypto.decrypt(buf.data(), buf.size(), 3, nullptr);
  assert(buf == original);
}

#ifdef RORAM_USE_OPENSSL
static void test_gcm_roundtrip_and_tamper() {
  std::vector<uint8_t> key(16, 0x2a);
  roram::OpenSSLCrypto crypto(key);
  std::vector<uint8_t> buf = make_data(64, 3);
  std::vector<uint8_t> original = buf;
  std::vector<uint8_t> tag(crypto.tag_size(), 0);
  crypto.encrypt(buf.data(), buf.size(), 10, tag.data());
  crypto.decrypt(buf.data(), buf.size(), 10, tag.data());
  assert(buf == original);

  crypto.encrypt(buf.data(), buf.size(), 11, tag.data());
  tag[0] ^= 0x01;
  bool failed = false;
  try {
    crypto.decrypt(buf.data(), buf.size(), 11, tag.data());
  } catch (const std::runtime_error&) {
    failed = true;
  }
  assert(failed);
}
#endif

int main() {
  test_params_helpers();
  test_block_bucket_roundtrip();
  test_position_map_basic();
  test_memory_storage_roundtrip();
  test_file_storage_roundtrip_and_seeks();
  test_path_oram_write_read();
  test_position_map_updates();
  test_path_oram_overwrite();
  test_stash_resilience_hot_blocks();
  test_backend_parity();
  test_path_oram_errors();
  test_roram_boundaries();
  test_roram_errors_and_seek_counter();
  test_roram_reference_model_random();
  test_noop_encrypt_roundtrip();
#ifdef RORAM_USE_OPENSSL
  test_gcm_roundtrip_and_tamper();
#endif
  test_cli_smoke();
  std::cout << "All tests passed\n";
  return 0;
}
