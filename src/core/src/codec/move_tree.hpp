#pragma once

#include "codec/annotation.hpp"
#include "codec/container.hpp"

#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <span>
#include <variant>

namespace oxq::core::detail {

struct MoveTreeLimits {
  std::size_t max_nodes{10'000'000};
  std::size_t max_tree_depth{1'000'000};
};

struct DecodedMoveTree {
  MoveTree value;
  bool canonical_order{true};
};

using MoveTreeResult = std::variant<DecodedMoveTree, CodecError>;

[[nodiscard]] MoveTreeResult read_move_tree(
    std::span<const std::byte> input,
    const ContainerView& container,
    const AnnotationView& annotations,
    const MoveTreeLimits& limits = {});

}  // namespace oxq::core::detail
