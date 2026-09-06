#include <oxq/core/game_model.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace oxq::core {
namespace {

constexpr std::array<std::size_t, 4> kHyphenPositions{8, 13, 18, 23};

[[nodiscard]] constexpr int hex_value(char character) noexcept {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

}  // namespace

MoveTree::MoveTree() {
  nodes.push_back(MoveNode{});
  nodes.front().id = 0;
  rebuildIndex();
}

bool MoveTree::rebuildIndex() const {
  index_by_id_.clear();
  index_by_id_.reserve(nodes.size());
  NodeId maximum_id = 0;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const auto [iterator, inserted] = index_by_id_.emplace(nodes[index].id, index);
    static_cast<void>(iterator);
    if (!inserted) {
      index_by_id_.clear();
      return false;
    }
    maximum_id = std::max(maximum_id, nodes[index].id);
  }
  if (maximum_id == std::numeric_limits<NodeId>::max()) {
    next_node_id_ = maximum_id;
  } else {
    next_node_id_ = std::max(next_node_id_, maximum_id + 1U);
  }
  return true;
}

MoveNode* MoveTree::findNode(NodeId id) {
  if (!rebuildIndex()) {
    return nullptr;
  }
  const auto iterator = index_by_id_.find(id);
  return iterator == index_by_id_.end() ? nullptr : &nodes[iterator->second];
}

const MoveNode* MoveTree::findNode(NodeId id) const {
  if (!rebuildIndex()) {
    return nullptr;
  }
  const auto iterator = index_by_id_.find(id);
  return iterator == index_by_id_.end() ? nullptr : &nodes[iterator->second];
}

std::optional<std::size_t> MoveTree::storageIndex(NodeId id) const {
  const auto iterator = index_by_id_.find(id);
  return iterator == index_by_id_.end() ? std::nullopt : std::optional{iterator->second};
}

NodeId MoveTree::allocateNodeId() {
  if (next_node_id_ == std::numeric_limits<NodeId>::max()) {
    throw std::overflow_error("MoveTree NodeId space exhausted");
  }
  return next_node_id_++;
}

NodeId MoveTree::addNode(NodeId parent, Move move) {
  if (!validateInvariants() || index_by_id_.find(parent) == index_by_id_.end()) {
    throw std::invalid_argument("MoveTree parent NodeId does not exist");
  }
  const NodeId id = allocateNodeId();
  MoveNode node;
  node.id = id;
  node.parent = parent;
  node.move = move;
  nodes.push_back(std::move(node));
  nodes[index_by_id_.at(parent)].children.push_back(id);
  if (!rebuildIndex()) {
    throw std::logic_error("MoveTree index rebuild failed after addNode");
  }
  return id;
}

bool MoveTree::removeNode(NodeId id) {
  if (!validateInvariants() || id == 0) {
    return false;
  }
  const auto target = index_by_id_.find(id);
  if (target == index_by_id_.end()) {
    return false;
  }
  const auto parent_id = nodes[target->second].parent;
  if (!parent_id.has_value()) {
    return false;
  }
  const auto parent = index_by_id_.find(*parent_id);
  if (parent == index_by_id_.end()) {
    return false;
  }

  std::unordered_set<NodeId> removed;
  std::vector<NodeId> pending{id};
  while (!pending.empty()) {
    const NodeId current = pending.back();
    pending.pop_back();
    if (!removed.insert(current).second) {
      continue;
    }
    const auto current_index = index_by_id_.find(current);
    if (current_index == index_by_id_.end()) {
      return false;
    }
    pending.insert(pending.end(), nodes[current_index->second].children.begin(),
                   nodes[current_index->second].children.end());
  }

  auto& siblings = nodes[parent->second].children;
  siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
  nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&removed](const MoveNode& node) {
                return removed.contains(node.id);
              }),
              nodes.end());
  return rebuildIndex();
}

bool MoveTree::restoreNode(MoveNode node) {
  if (node.id == 0 || !node.parent.has_value() || !node.children.empty() ||
      !validateInvariants() || index_by_id_.contains(node.id) ||
      !index_by_id_.contains(*node.parent)) {
    return false;
  }
  const NodeId id = node.id;
  const NodeId parent = *node.parent;
  nodes.push_back(std::move(node));
  nodes[index_by_id_.at(parent)].children.push_back(id);
  return rebuildIndex();
}

bool MoveTree::validateInvariants() const {
  if (!rebuildIndex() || nodes.empty() || nodes.front().id != 0 ||
      nodes.front().parent.has_value() || nodes.front().move.has_value()) {
    return false;
  }
  std::vector<std::size_t> incoming(nodes.size(), 0);
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    const auto mapped = index_by_id_.find(node.id);
    if (mapped == index_by_id_.end() || mapped->second != index) {
      return false;
    }
    if (node.parent.has_value()) {
      const auto parent = index_by_id_.find(*node.parent);
      if (parent == index_by_id_.end()) {
        return false;
      }
      const auto& siblings = nodes[parent->second].children;
      if (std::ranges::find(siblings, node.id) == siblings.end()) {
        return false;
      }
    }
    std::unordered_set<NodeId> sibling_ids;
    sibling_ids.reserve(node.children.size());
    for (const NodeId child : node.children) {
      const auto child_index = index_by_id_.find(child);
      if (child_index == index_by_id_.end() ||
          !sibling_ids.insert(child).second ||
          nodes[child_index->second].parent != std::optional<NodeId>{node.id}) {
        return false;
      }
      ++incoming[child_index->second];
    }
  }
  if (incoming.front() != 0) {
    return false;
  }
  for (std::size_t index = 1; index < incoming.size(); ++index) {
    if (incoming[index] != 1) {
      return false;
    }
  }

  std::vector<bool> reachable(nodes.size(), false);
  std::vector<NodeId> pending{0};
  while (!pending.empty()) {
    const NodeId current = pending.back();
    pending.pop_back();
    const auto current_index = index_by_id_.find(current);
    if (current_index == index_by_id_.end() || reachable[current_index->second]) {
      continue;
    }
    reachable[current_index->second] = true;
    pending.insert(pending.end(), nodes[current_index->second].children.begin(),
                   nodes[current_index->second].children.end());
  }
  for (const bool is_reachable : reachable) {
    if (!is_reachable) {
      return false;
    }
  }
  return true;
}

bool Uuid::is_nil() const noexcept {
  return std::ranges::all_of(bytes, [](std::uint8_t byte) { return byte == 0; });
}

std::string Uuid::to_string() const {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(36);
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10) {
      result.push_back('-');
    }
    const auto byte = bytes[index];
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  return result;
}

std::optional<Uuid> Uuid::parse(std::string_view text) noexcept {
  if (text.size() != 36) {
    return std::nullopt;
  }
  for (const auto position : kHyphenPositions) {
    if (text[position] != '-') {
      return std::nullopt;
    }
  }

  Uuid result;
  std::size_t source = 0;
  for (auto& byte : result.bytes) {
    if (source < text.size() && text[source] == '-') {
      ++source;
    }
    if (source + 1 >= text.size()) {
      return std::nullopt;
    }
    const int high = hex_value(text[source]);
    const int low = hex_value(text[source + 1]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    byte = static_cast<std::uint8_t>((high << 4) | low);
    source += 2;
  }
  return source == text.size() ? std::optional<Uuid>{result} : std::nullopt;
}

}  // namespace oxq::core
