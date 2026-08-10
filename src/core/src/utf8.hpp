#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace oxq::core::detail {

[[nodiscard]] std::optional<std::size_t> first_invalid_utf8(std::string_view text) noexcept;

[[nodiscard]] inline bool valid_utf8(std::string_view text) noexcept {
  return !first_invalid_utf8(text).has_value();
}

}  // namespace oxq::core::detail
