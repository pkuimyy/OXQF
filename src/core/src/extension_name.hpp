#pragma once

#include <string_view>

namespace oxq::core::detail {

[[nodiscard]] bool valid_extension_namespace(std::string_view value) noexcept;
[[nodiscard]] bool valid_extension_key(std::string_view value) noexcept;

}  // namespace oxq::core::detail
