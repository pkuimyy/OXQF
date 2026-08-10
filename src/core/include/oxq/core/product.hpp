#pragma once

#include <string_view>

namespace oxq::core {

[[nodiscard]] std::string_view product_name() noexcept;
[[nodiscard]] std::string_view product_version() noexcept;

}  // namespace oxq::core
