#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace oxq::core::detail {

[[nodiscard]] constexpr bool checked_add(std::size_t left, std::size_t right,
                                         std::size_t& result) noexcept {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] constexpr bool checked_multiply(std::size_t left, std::size_t right,
                                              std::size_t& result) noexcept {
  if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

[[nodiscard]] constexpr bool contains(std::span<const std::byte> input, std::size_t offset,
                                      std::size_t length) noexcept {
  return offset <= input.size() && length <= input.size() - offset;
}

[[nodiscard]] inline std::uint16_t read_u16(std::span<const std::byte> input,
                                           std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[offset])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[offset + 1])) << 8U);
}

[[nodiscard]] inline std::uint32_t read_u32(std::span<const std::byte> input,
                                           std::size_t offset) noexcept {
  std::uint32_t result = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    result |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[offset + index]))
              << static_cast<unsigned>(index * 8U);
  }
  return result;
}

[[nodiscard]] inline std::uint64_t read_u64(std::span<const std::byte> input,
                                           std::size_t offset) noexcept {
  std::uint64_t result = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    result |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(input[offset + index]))
              << static_cast<unsigned>(index * 8U);
  }
  return result;
}

}  // namespace oxq::core::detail
