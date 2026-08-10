#include "codec/crc32c.hpp"

namespace oxq::core::detail {

std::uint32_t crc32c(std::span<const std::byte> input) noexcept {
  std::uint32_t crc = 0xffffffffU;
  for (const auto value : input) {
    crc ^= std::to_integer<std::uint8_t>(value);
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ ((crc & 1U) == 0 ? 0U : 0x82f63b78U);
    }
  }
  return crc ^ 0xffffffffU;
}

}  // namespace oxq::core::detail
