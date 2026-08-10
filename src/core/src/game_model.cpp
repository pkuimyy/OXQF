#include <oxq/core/game_model.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace oxq::core {
namespace {

constexpr std::array<std::size_t, 4> kHyphenPositions{8, 13, 18, 23};

[[nodiscard]] constexpr int hex_value(char character) noexcept {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

}  // namespace

bool Uuid::is_nil() const noexcept {
  return std::ranges::all_of(bytes, [](std::uint8_t byte) { return byte == 0; });
}

std::string Uuid::to_string() const {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(36);
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10) {
      result.push_back('-');
    }
    const auto byte = bytes[index];
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  return result;
}

std::optional<Uuid> Uuid::parse(std::string_view text) noexcept {
  if (text.size() != 36) {
    return std::nullopt;
  }
  for (const auto position : kHyphenPositions) {
    if (text[position] != '-') {
      return std::nullopt;
    }
  }

  Uuid result;
  std::size_t source = 0;
  for (auto& byte : result.bytes) {
    if (source < text.size() && text[source] == '-') {
      ++source;
    }
    if (source + 1 >= text.size()) {
      return std::nullopt;
    }
    const int high = hex_value(text[source]);
    const int low = hex_value(text[source + 1]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    byte = static_cast<std::uint8_t>((high << 4) | low);
    source += 2;
  }
  return source == text.size() ? std::optional<Uuid>{result} : std::nullopt;
}

}  // namespace oxq::core
