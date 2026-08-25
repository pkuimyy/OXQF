#pragma once

#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace oxq::convert::detail {

[[nodiscard]] std::string sha256_hex(std::span<const std::byte> input);
[[nodiscard]] core::Uuid uuid_v5(const core::Uuid& name_space,
                                 std::string_view name);

}  // namespace oxq::convert::detail
