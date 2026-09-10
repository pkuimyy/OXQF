#include <oxq/editor/editor_session.hpp>
#include <oxq/editor/variation_graph.hpp>

#include <variant>
#include <vector>

namespace {

using oxq::editor::EditorError;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::editor::SessionOptions;
using oxq::editor::SessionSnapshot;
using oxq::editor::VariationGraphProjection;
using oxq::editor::VariationGraphQuery;
using oxq::format::GameDocument;
using oxq::format::Move;
using oxq::format::NodeId;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Side;

[[nodiscard]] GameDocument branched_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000040");
  document.initial_position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::black, PieceType::rook, 89},
  };
  const auto main = document.move_tree.addNode(0, Move{0, 9});
  static_cast<void>(document.move_tree.addNode(main, Move{89, 80}));
  const auto branch = document.move_tree.addNode(0, Move{0, 18});
  static_cast<void>(document.move_tree.addNode(branch, Move{89, 71}));
  return document;
}

[[nodiscard]] std::vector<NodeId> ids(const VariationGraphProjection& graph) {
  std::vector<NodeId> result;
  for (const auto& node : graph.nodes) {
    result.push_back(node.summary.id);
  }
  return result;
}

}  // namespace

int main() {
  SessionOptions options;
  options.validation_policy = oxq::editor::ValidationPolicy::structural;
  auto opened = oxq::editor::open_document(branched_document(), options);
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(opened));
  if (session.checkout(4, 0).has_value()) {
    return 2;
  }

  const auto path = session.path_to(4);
  if (!std::holds_alternative<std::vector<NodeId>>(path) ||
      std::get<std::vector<NodeId>>(path) != std::vector<NodeId>({0, 3, 4})) {
    return 3;
  }
  const auto summary = session.node_summary(3);
  if (!std::holds_alternative<oxq::editor::NodeSummary>(summary) ||
      std::get<oxq::editor::NodeSummary>(summary).parent != 0 ||
      std::get<oxq::editor::NodeSummary>(summary).child_count != 1) {
    return 4;
  }

  const auto projected = session.variation_graph();
  if (!std::holds_alternative<VariationGraphProjection>(projected)) {
    return 5;
  }
  const auto& graph = std::get<VariationGraphProjection>(projected);
  if (ids(graph) != std::vector<NodeId>({0, 1, 2, 3, 4}) ||
      graph.edges.size() != 4 || graph.truncated ||
      !graph.nodes[0].on_current_path || graph.nodes[1].on_current_path ||
      graph.nodes[2].on_current_path || !graph.nodes[3].on_current_path ||
      !graph.nodes[4].on_current_path || !graph.nodes[1].main_variation ||
      graph.nodes[3].main_variation || graph.nodes[3].sibling_index != 1 ||
      graph.edges[2].parent != 0 || graph.edges[2].child != 3 ||
      graph.edges[2].main_variation) {
    return 6;
  }

  const auto subtree = session.variation_graph(VariationGraphQuery{3, {}, {}});
  if (!std::holds_alternative<VariationGraphProjection>(subtree) ||
      ids(std::get<VariationGraphProjection>(subtree)) !=
          std::vector<NodeId>({3, 4}) ||
      std::get<VariationGraphProjection>(subtree).nodes.front().main_variation ||
      std::get<VariationGraphProjection>(subtree).edges.size() != 1) {
    return 7;
  }

  const auto depth_limited =
      session.variation_graph(VariationGraphQuery{0, 1, {}});
  if (!std::holds_alternative<VariationGraphProjection>(depth_limited) ||
      ids(std::get<VariationGraphProjection>(depth_limited)) !=
          std::vector<NodeId>({0, 1, 3}) ||
      !std::get<VariationGraphProjection>(depth_limited).truncated) {
    return 8;
  }
  const auto count_limited =
      session.variation_graph(VariationGraphQuery{0, {}, 2});
  if (!std::holds_alternative<VariationGraphProjection>(count_limited) ||
      ids(std::get<VariationGraphProjection>(count_limited)) !=
          std::vector<NodeId>({0, 1}) ||
      !std::get<VariationGraphProjection>(count_limited).truncated ||
      std::get<VariationGraphProjection>(count_limited).edges.size() != 1) {
    return 9;
  }

  const auto snapshot = session.snapshot();
  if (!std::holds_alternative<SessionSnapshot>(snapshot) ||
      std::get<SessionSnapshot>(snapshot).current.id != 4 ||
      ids(std::get<SessionSnapshot>(snapshot).variation_graph) !=
          std::vector<NodeId>({0, 1, 2, 3, 4})) {
    return 10;
  }

  const auto missing_path = session.path_to(99);
  const auto missing_graph =
      session.variation_graph(VariationGraphQuery{99, {}, {}});
  if (!std::holds_alternative<EditorError>(missing_path) ||
      std::get<EditorError>(missing_path).code !=
          EditorErrorCode::node_not_found ||
      !std::holds_alternative<EditorError>(missing_graph) ||
      std::get<EditorError>(missing_graph).code !=
          EditorErrorCode::node_not_found) {
    return 11;
  }

  options.max_projection_nodes = 2;
  auto limited_opened = oxq::editor::open_document(branched_document(), options);
  auto limited = std::get<EditorSession>(std::move(limited_opened));
  const auto configured_limit =
      limited.variation_graph(VariationGraphQuery{0, {}, 100});
  if (!std::holds_alternative<VariationGraphProjection>(configured_limit) ||
      std::get<VariationGraphProjection>(configured_limit).nodes.size() != 2 ||
      !std::get<VariationGraphProjection>(configured_limit).truncated) {
    return 12;
  }
  return 0;
}
