#include "utf8.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace oxq::core::detail {
namespace {

[[nodiscard]] bool continuation(std::uint8_t byte) noexcept {
  return (byte & 0xc0U) == 0x80U;
}

}  // namespace

std::optional<std::size_t> first_invalid_utf8(std::string_view text) noexcept {
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<std::uint8_t>(bytes[index]);
    if (first <= 0x7fU) {
      ++index;
      continue;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
      if (index + 1 >= text.size() || !continuation(bytes[index + 1])) {
        return index;
      }
      index += 2;
      continue;
    }
    if (first >= 0xe0U && first <= 0xefU) {
      if (index + 2 >= text.size() || !continuation(bytes[index + 2])) {
        return index;
      }
      const auto second = static_cast<std::uint8_t>(bytes[index + 1]);
      const bool valid_second =
          (first == 0xe0U && second >= 0xa0U && second <= 0xbfU) ||
          (first == 0xedU && second >= 0x80U && second <= 0x9fU) ||
          (((first >= 0xe1U && first <= 0xecU) || (first >= 0xeeU && first <= 0xefU)) &&
           continuation(second));
      if (!valid_second) {
        return index;
      }
      index += 3;
      continue;
    }
    if (first >= 0xf0U && first <= 0xf4U) {
      if (index + 3 >= text.size() || !continuation(bytes[index + 2]) ||
          !continuation(bytes[index + 3])) {
        return index;
      }
      const auto second = static_cast<std::uint8_t>(bytes[index + 1]);
      const bool valid_second =
          (first == 0xf0U && second >= 0x90U && second <= 0xbfU) ||
          (first >= 0xf1U && first <= 0xf3U && continuation(second)) ||
          (first == 0xf4U && second >= 0x80U && second <= 0x8fU);
      if (!valid_second) {
        return index;
      }
      index += 4;
      continue;
    }
    return index;
  }
  return std::nullopt;
}

}  // namespace oxq::core::detail
