#include "roram/block.hpp"

namespace roram {

Block::Block(size_t data_len, int num_orams) : data(data_len, 0), a(INVALID_ADDR), p(num_orams, 0) {}

void Block::set_dummy() {
  a = INVALID_ADDR;
  std::fill(data.begin(), data.end(), 0);
  std::fill(p.begin(), p.end(), 0);
}

size_t Block::serialized_size(const Params& params) const {
  return params.B + 8 + (params.ell + 1) * 8;
}

void Block::serialize(uint8_t* out, const Params& params) const {
  size_t off = 0;
  memcpy(out + off, data.data(), params.B);
  off += params.B;
  memcpy(out + off, &a, 8);
  off += 8;
  for (size_t i = 0; i < p.size(); ++i) {
    memcpy(out + off, &p[i], 8);
    off += 8;
  }
}

void Block::deserialize(const uint8_t* in, const Params& params) {
  size_t off = 0;
  data.assign(in + off, in + off + params.B);
  off += params.B;
  memcpy(&a, in + off, 8);
  off += 8;
  for (size_t i = 0; i < p.size(); ++i) {
    memcpy(&p[i], in + off, 8);
    off += 8;
  }
}

Bucket::Bucket(int Z, size_t data_len, int num_orams) : blocks(Z, Block(data_len, num_orams)) {}

size_t Bucket::serialized_size(const Params& params) const {
  return blocks.empty() ? 0 : blocks[0].serialized_size(params) * blocks.size();
}

void Bucket::serialize(uint8_t* out, const Params& params) const {
  size_t block_size = blocks.empty() ? 0 : blocks[0].serialized_size(params);
  for (size_t i = 0; i < blocks.size(); ++i)
    blocks[i].serialize(out + i * block_size, params);
}

void Bucket::deserialize(const uint8_t* in, const Params& params) {
  size_t block_size = blocks.empty() ? 0 : blocks[0].serialized_size(params);
  for (size_t i = 0; i < blocks.size(); ++i)
    blocks[i].deserialize(in + i * block_size, params);
}

}  // namespace roram
