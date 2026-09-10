#pragma once

#include <oxq/editor/error.hpp>
#include <oxq/format/document.hpp>

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace oxq::editor {

struct NodeSummary {
  format::NodeId id{0};
  std::optional<format::NodeId> parent;
  std::optional<format::Move> move;
  std::size_t child_count{0};
  std::size_t annotation_count{0};

  friend bool operator==(const NodeSummary&, const NodeSummary&) = default;
};

struct VariationGraphNode {
  NodeSummary summary;
  std::size_t depth{0};
  std::size_t sibling_index{0};
  bool main_variation{false};
  bool on_current_path{false};

  friend bool operator==(const VariationGraphNode&,
                         const VariationGraphNode&) = default;
};

struct VariationGraphEdge {
  format::NodeId parent{0};
  format::NodeId child{0};
  std::size_t sibling_index{0};
  bool main_variation{false};

  friend bool operator==(const VariationGraphEdge&,
                         const VariationGraphEdge&) = default;
};

struct VariationGraphQuery {
  format::NodeId from{0};
  std::optional<std::size_t> max_depth;
  std::optional<std::size_t> max_nodes;

  friend bool operator==(const VariationGraphQuery&,
                         const VariationGraphQuery&) = default;
};

struct VariationGraphProjection {
  format::NodeId from{0};
  std::vector<VariationGraphNode> nodes;
  std::vector<VariationGraphEdge> edges;
  bool truncated{false};

  friend bool operator==(const VariationGraphProjection&,
                         const VariationGraphProjection&) = default;
};

using NodeSummaryOutcome = std::variant<NodeSummary, EditorError>;
using NodePathOutcome = std::variant<std::vector<format::NodeId>, EditorError>;
using VariationGraphOutcome = std::variant<VariationGraphProjection, EditorError>;

}  // namespace oxq::editor
