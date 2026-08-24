#pragma once

#include <oxq/core/game_model.hpp>

#include <optional>
#include <string_view>

namespace oxq::core::detail {

[[nodiscard]] std::optional<DatePrecision> date_time_precision(
    std::string_view text) noexcept;

}  // namespace oxq::core::detail
