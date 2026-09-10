#pragma once

#include <oxq/format/document.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace oxq::editor {

struct InsertMoveCommand {
  format::NodeId parent{0};
  format::Move move;
  std::optional<std::size_t> sibling_index;

  friend bool operator==(const InsertMoveCommand&, const InsertMoveCommand&) = default;
};

struct DeleteSubtreeCommand {
  format::NodeId node{0};

  friend bool operator==(const DeleteSubtreeCommand&, const DeleteSubtreeCommand&) = default;
};

struct ReplaceMoveCommand {
  format::NodeId node{0};
  format::Move move;

  friend bool operator==(const ReplaceMoveCommand&, const ReplaceMoveCommand&) = default;
};

struct ReorderVariationCommand {
  format::NodeId node{0};
  std::size_t target_index{0};

  friend bool operator==(const ReorderVariationCommand&,
                         const ReorderVariationCommand&) = default;
};

struct SetAnnotationsCommand {
  format::NodeId node{0};
  std::vector<format::Annotation> annotations;

  friend bool operator==(const SetAnnotationsCommand&,
                         const SetAnnotationsCommand&) = default;
};

struct SetMetadataCommand {
  format::GameMetadata metadata;

  friend bool operator==(const SetMetadataCommand&, const SetMetadataCommand&) = default;
};

using Command = std::variant<InsertMoveCommand, DeleteSubtreeCommand,
                             ReplaceMoveCommand, ReorderVariationCommand,
                             SetAnnotationsCommand, SetMetadataCommand>;

struct ChangeSet {
  std::uint64_t before_revision{0};
  std::uint64_t after_revision{0};
  std::vector<format::NodeId> inserted;
  std::vector<format::NodeId> removed;
  std::vector<format::NodeId> updated;
  std::vector<format::NodeId> reordered_parents;
  bool metadata_changed{false};
  bool selection_changed{false};

  friend bool operator==(const ChangeSet&, const ChangeSet&) = default;
};

struct CommandResult {
  std::uint64_t revision{0};
  ChangeSet changes;
  std::optional<format::NodeId> created_node;

  friend bool operator==(const CommandResult&, const CommandResult&) = default;
};

}  // namespace oxq::editor
