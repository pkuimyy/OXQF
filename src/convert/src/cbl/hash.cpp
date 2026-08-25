#include "cbl/hash.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace oxq::convert::detail {
namespace {

[[nodiscard]] constexpr std::uint32_t choose(std::uint32_t x, std::uint32_t y,
                                              std::uint32_t z) noexcept {
  return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr std::uint32_t majority(std::uint32_t x, std::uint32_t y,
                                                std::uint32_t z) noexcept {
  return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t read_be32(
    std::span<const std::uint8_t> input, std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(input[offset]) << 24U) |
         (static_cast<std::uint32_t>(input[offset + 1]) << 16U) |
         (static_cast<std::uint32_t>(input[offset + 2]) << 8U) |
         static_cast<std::uint32_t>(input[offset + 3]);
}

void write_be32(std::span<std::uint8_t> output, std::size_t offset,
                std::uint32_t value) noexcept {
  output[offset] = static_cast<std::uint8_t>(value >> 24U);
  output[offset + 1] = static_cast<std::uint8_t>(value >> 16U);
  output[offset + 2] = static_cast<std::uint8_t>(value >> 8U);
  output[offset + 3] = static_cast<std::uint8_t>(value);
}

class Sha256 {
 public:
  void update(std::span<const std::byte> input) {
    for (const auto value : input) {
      block_[block_size_++] = std::to_integer<std::uint8_t>(value);
      bit_count_ += 8;
      if (block_size_ == block_.size()) {
        transform();
        block_size_ = 0;
      }
    }
  }

  [[nodiscard]] std::array<std::uint8_t, 32> finish() {
    const auto source_bit_count = bit_count_;
    block_[block_size_++] = 0x80;
    if (block_size_ > 56) {
      while (block_size_ < block_.size()) {
        block_[block_size_++] = 0;
      }
      transform();
      block_size_ = 0;
    }
    while (block_size_ < 56) {
      block_[block_size_++] = 0;
    }
    for (unsigned index = 0; index < 8; ++index) {
      block_[63 - index] =
          static_cast<std::uint8_t>(source_bit_count >> (index * 8U));
    }
    transform();

    std::array<std::uint8_t, 32> result{};
    for (std::size_t index = 0; index < state_.size(); ++index) {
      write_be32(result, index * 4, state_[index]);
    }
    return result;
  }

 private:
  void transform() noexcept {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0; index < 16; ++index) {
      schedule[index] = read_be32(block_, index * 4);
    }
    for (std::size_t index = 16; index < schedule.size(); ++index) {
      const auto s0 = std::rotr(schedule[index - 15], 7) ^
                      std::rotr(schedule[index - 15], 18) ^
                      (schedule[index - 15] >> 3U);
      const auto s1 = std::rotr(schedule[index - 2], 17) ^
                      std::rotr(schedule[index - 2], 19) ^
                      (schedule[index - 2] >> 10U);
      schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];
    for (std::size_t index = 0; index < schedule.size(); ++index) {
      const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const auto temporary1 =
          h + sum1 + choose(e, f, g) + constants[index] + schedule[index];
      const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const auto temporary2 = sum0 + majority(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<std::uint8_t, 64> block_{};
  std::size_t block_size_{};
  std::uint64_t bit_count_{};
};

class Sha1 {
 public:
  void update(std::span<const std::uint8_t> input) {
    for (const auto value : input) {
      block_[block_size_++] = value;
      bit_count_ += 8;
      if (block_size_ == block_.size()) {
        transform();
        block_size_ = 0;
      }
    }
  }

  [[nodiscard]] std::array<std::uint8_t, 20> finish() {
    const auto source_bit_count = bit_count_;
    block_[block_size_++] = 0x80;
    if (block_size_ > 56) {
      while (block_size_ < block_.size()) {
        block_[block_size_++] = 0;
      }
      transform();
      block_size_ = 0;
    }
    while (block_size_ < 56) {
      block_[block_size_++] = 0;
    }
    for (unsigned index = 0; index < 8; ++index) {
      block_[63 - index] =
          static_cast<std::uint8_t>(source_bit_count >> (index * 8U));
    }
    transform();

    std::array<std::uint8_t, 20> result{};
    for (std::size_t index = 0; index < state_.size(); ++index) {
      write_be32(result, index * 4, state_[index]);
    }
    return result;
  }

 private:
  void transform() noexcept {
    std::array<std::uint32_t, 80> schedule{};
    for (std::size_t index = 0; index < 16; ++index) {
      schedule[index] = read_be32(block_, index * 4);
    }
    for (std::size_t index = 16; index < schedule.size(); ++index) {
      schedule[index] = std::rotl(schedule[index - 3] ^ schedule[index - 8] ^
                                      schedule[index - 14] ^ schedule[index - 16],
                                  1);
    }
    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    for (std::size_t index = 0; index < schedule.size(); ++index) {
      std::uint32_t function = 0;
      std::uint32_t constant = 0;
      if (index < 20) {
        function = choose(b, c, d);
        constant = 0x5a827999U;
      } else if (index < 40) {
        function = b ^ c ^ d;
        constant = 0x6ed9eba1U;
      } else if (index < 60) {
        function = majority(b, c, d);
        constant = 0x8f1bbcdcU;
      } else {
        function = b ^ c ^ d;
        constant = 0xca62c1d6U;
      }
      const auto temporary = std::rotl(a, 5) + function + e + constant +
                             schedule[index];
      e = d;
      d = c;
      c = std::rotl(b, 30);
      b = a;
      a = temporary;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
  }

  std::array<std::uint32_t, 5> state_{
      0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U, 0xc3d2e1f0U};
  std::array<std::uint8_t, 64> block_{};
  std::size_t block_size_{};
  std::uint64_t bit_count_{};
};

}  // namespace

std::string sha256_hex(std::span<const std::byte> input) {
  Sha256 hash;
  hash.update(input);
  const auto digest = hash.finish();
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(digest.size() * 2);
  for (const auto byte : digest) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  return result;
}

core::Uuid uuid_v5(const core::Uuid& name_space, std::string_view name) {
  Sha1 hash;
  hash.update(name_space.bytes);
  hash.update(std::span{reinterpret_cast<const std::uint8_t*>(name.data()),
                        name.size()});
  const auto digest = hash.finish();
  core::Uuid result;
  for (std::size_t index = 0; index < result.bytes.size(); ++index) {
    result.bytes[index] = digest[index];
  }
  result.bytes[6] = static_cast<std::uint8_t>((result.bytes[6] & 0x0fU) | 0x50U);
  result.bytes[8] = static_cast<std::uint8_t>((result.bytes[8] & 0x3fU) | 0x80U);
  return result;
}

}  // namespace oxq::convert::detail
