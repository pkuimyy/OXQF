#include "extension_name.hpp"

#include <algorithm>
#include <string_view>

namespace oxq::core::detail {

bool valid_extension_namespace(std::string_view value) noexcept {
  if (value.empty() || value.front() == '.' || value.back() == '.' || value.find('.') == value.npos) {
    return false;
  }
  bool at_segment_start = true;
  char previous = '\0';
  for (const char character : value) {
    if (character == '.') {
      if (at_segment_start || previous == '-') {
        return false;
      }
      at_segment_start = true;
    } else {
      const bool letter = character >= 'a' && character <= 'z';
      const bool digit = character >= '0' && character <= '9';
      if ((!letter && !digit && character != '-') || (at_segment_start && !letter)) {
        return false;
      }
      at_segment_start = false;
    }
    previous = character;
  }
  return !at_segment_start && previous != '-';
}

bool valid_extension_key(std::string_view value) noexcept {
  if (value.empty() || value.front() < 'a' || value.front() > 'z') {
    return false;
  }
  return std::ranges::all_of(value, [](char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '_';
  });
}

}  // namespace oxq::core::detail
