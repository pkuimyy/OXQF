#pragma once

#include <string>
#include <string_view>

namespace oxq::core::detail {

[[nodiscard]] std::string normalize_nfc(std::string_view text);
[[nodiscard]] bool is_nfc(std::string_view text);

}  // namespace oxq::core::detail
