#include <oxq/editor/editor_session.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <unordered_set>
#include <utility>
#include <vector>

namespace oxq::editor {
namespace {

[[nodiscard]] EditorError missing_node(format::NodeId node) {
  EditorError error;
  error.code = EditorErrorCode::node_not_found;
  error.message = "node " + std::to_string(node) + " does not exist";
  error.node_id = node;
  return error;
}

[[nodiscard]] NodeSummary summarize(const format::MoveNode& node) {
  return NodeSummary{node.id, node.parent, node.move, node.children.size(),
                     node.annotations.size()};
}

struct PendingNode {
  format::NodeId id{0};
  std::size_t depth{0};
  std::size_t sibling_index{0};
};

}  // namespace

NodeSummaryOutcome EditorSession::node_summary(format::NodeId node) const {
  const auto* found = document_.move_tree.findNode(node);
  if (found == nullptr) {
    return missing_node(node);
  }
  return summarize(*found);
}

NodePathOutcome EditorSession::path_to(format::NodeId node) const {
  if (document_.move_tree.findNode(node) == nullptr) {
    return missing_node(node);
  }
  try {
    std::vector<format::NodeId> path;
    auto current = node;
    while (true) {
      path.push_back(current);
      const auto* found = document_.move_tree.findNode(current);
      if (found == nullptr) {
        EditorError error;
        error.code = EditorErrorCode::internal_invariant;
        error.message = "node path contains a missing parent";
        error.node_id = current;
        return error;
      }
      if (!found->parent.has_value()) {
        break;
      }
      current = *found->parent;
    }
    std::ranges::reverse(path);
    return path;
  } catch (const std::bad_alloc&) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "not enough memory to build the node path";
    error.node_id = node;
    return error;
  }
}

VariationGraphOutcome EditorSession::variation_graph(
    VariationGraphQuery query) const {
  const auto* root = document_.move_tree.findNode(query.from);
  if (root == nullptr) {
    return missing_node(query.from);
  }

  try {
    const auto current_path_outcome = path_to(state_.current_node);
    if (std::holds_alternative<EditorError>(current_path_outcome)) {
      return std::get<EditorError>(current_path_outcome);
    }
    const auto& current_path =
        std::get<std::vector<format::NodeId>>(current_path_outcome);
    std::unordered_set<format::NodeId> current_nodes(current_path.begin(),
                                                     current_path.end());

    const auto requested_nodes = query.max_nodes.value_or(
        std::numeric_limits<std::size_t>::max());
    const auto node_limit = std::min(requested_nodes,
                                     options_.max_projection_nodes);
    const auto depth_limit = query.max_depth.value_or(
        std::numeric_limits<std::size_t>::max());

    VariationGraphProjection projection;
    projection.from = query.from;
    std::size_t root_sibling_index = 0;
    if (root->parent.has_value()) {
      const auto* parent = document_.move_tree.findNode(*root->parent);
      if (parent == nullptr) {
        EditorError error;
        error.code = EditorErrorCode::internal_invariant;
        error.message = "variation projection root has a missing parent";
        error.node_id = query.from;
        return error;
      }
      const auto sibling = std::ranges::find(parent->children, query.from);
      if (sibling == parent->children.end()) {
        EditorError error;
        error.code = EditorErrorCode::internal_invariant;
        error.message = "variation projection root is absent from its parent";
        error.node_id = query.from;
        return error;
      }
      root_sibling_index = static_cast<std::size_t>(
          std::distance(parent->children.begin(), sibling));
    }
    std::vector<PendingNode> pending{{query.from, 0, root_sibling_index}};
    while (!pending.empty()) {
      if (projection.nodes.size() >= node_limit) {
        projection.truncated = true;
        break;
      }
      const auto item = pending.back();
      pending.pop_back();
      const auto* node = document_.move_tree.findNode(item.id);
      if (node == nullptr) {
        EditorError error;
        error.code = EditorErrorCode::internal_invariant;
        error.message = "variation projection contains a missing node";
        error.node_id = item.id;
        return error;
      }
      projection.nodes.push_back(VariationGraphNode{
          summarize(*node), item.depth, item.sibling_index,
          !node->parent.has_value() || item.sibling_index == 0,
          current_nodes.contains(item.id)});
      if (item.depth >= depth_limit) {
        projection.truncated = projection.truncated || !node->children.empty();
        continue;
      }
      for (std::size_t index = node->children.size(); index > 0; --index) {
        const auto sibling_index = index - 1;
        pending.push_back(
            PendingNode{node->children[sibling_index], item.depth + 1,
                        sibling_index});
      }
    }

    // The local root has no edge, while every other projected node does. Edges
    // are sorted into the same preorder as their child nodes for stable UI diffs.
    if (projection.nodes.size() > 1) {
      projection.edges.reserve(projection.nodes.size() - 1);
      for (std::size_t index = 1; index < projection.nodes.size(); ++index) {
        const auto& node = projection.nodes[index];
        projection.edges.push_back(VariationGraphEdge{
            *node.summary.parent, node.summary.id, node.sibling_index,
            node.main_variation});
      }
    }
    return projection;
  } catch (const std::bad_alloc&) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "not enough memory to build the variation projection";
    error.node_id = query.from;
    return error;
  }
}

}  // namespace oxq::editor
