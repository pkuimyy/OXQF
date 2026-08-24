#pragma once

#include "codec/container.hpp"

#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <span>
#include <variant>

namespace oxq::core::detail {

struct PositionView {
  Position value;
  bool canonical_order{true};
};

using PositionResult = std::variant<PositionView, CodecError>;

[[nodiscard]] PositionResult read_position(
    std::span<const std::byte> input,
    const ContainerView& container);

}  // namespace oxq::core::detail
