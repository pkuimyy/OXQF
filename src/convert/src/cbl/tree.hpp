#pragma once

#include "cbl/record.hpp"

#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace oxq::convert::detail {

struct CblMoveTreeView {
  core::MoveTree tree;
  // Aligned with tree.nodes, including the root at index zero. These retain
  // opaque source bits without assigning unsupported chess semantics to them.
  std::vector<std::uint16_t> source_controls;
  std::size_t comment_count{};
};

using CblMoveTreeOutcome = std::variant<CblMoveTreeView, CblError>;

[[nodiscard]] CblMoveTreeOutcome read_cbl_move_tree(
    const CblContainerView& container,
    const CblDirectoryEntryView& entry,
    const CblRecordView& record,
    const CblReaderLimits& limits = {});

}  // namespace oxq::convert::detail
