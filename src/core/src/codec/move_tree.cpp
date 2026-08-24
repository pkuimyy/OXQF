#include "codec/move_tree.hpp"

#include "codec/binary.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace oxq::core::detail {
namespace {

constexpr std::uint32_t kMoveTreeSectionType = 3;
constexpr std::uint32_t kAnnotationSectionType = 4;
constexpr std::uint32_t kNoNode = 0xffffffffU;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kNodeRecordSize = 32;
constexpr std::size_t kAnnotationHeaderSize = 16;
constexpr std::size_t kAnnotationRecordSize = 24;

struct RawNode {
  std::uint32_t parent{};
  std::uint32_t first_child{};
  std::uint32_t next_sibling{};
  std::uint32_t first_annotation{};
  std::uint8_t from_square{};
  std::uint8_t to_square{};
  std::uint32_t ply{};
};

[[nodiscard]] CodecError tree_error(CodecErrorCode code, std::size_t offset, std::string field,
                                    std::string message,
                                    std::optional<std::uint64_t> expected = {},
                                    std::optional<std::uint64_t> actual = {}) {
  return {code, offset, kMoveTreeSectionType, std::move(field), std::move(message), expected,
          actual};
}

[[nodiscard]] CodecError annotation_error(std::size_t offset, std::string field,
                                          std::string message,
                                          std::optional<std::uint64_t> actual = {}) {
  return {CodecErrorCode::invalid_annotation, offset, kAnnotationSectionType, std::move(field),
          std::move(message), {}, actual};
}

[[nodiscard]] std::size_t node_offset(const SectionView& section, std::size_t index) noexcept {
  return section.offset + kHeaderSize + index * kNodeRecordSize;
}

}  // namespace

MoveTreeResult read_move_tree(std::span<const std::byte> input, const ContainerView& container,
                              const AnnotationView& annotations, const MoveTreeLimits& limits) {
  const auto section = std::ranges::find(container.sections, kMoveTreeSectionType,
                                         &SectionView::type);
  if (section == container.sections.end()) {
    return tree_error(CodecErrorCode::invalid_section_table, 0, "section_type",
                      "MOVE_TREE Section is missing");
  }
  if (!contains(input, section->offset, section->size)) {
    return tree_error(CodecErrorCode::section_out_of_range, section->offset, "section_range",
                      "MOVE_TREE Section extends beyond input");
  }
  const auto payload = input.subspan(section->offset, section->size);
  if (payload.size() < kHeaderSize) {
    return tree_error(CodecErrorCode::invalid_tree, section->offset, "header",
                      "MOVE_TREE is shorter than its 16-byte header", kHeaderSize,
                      payload.size());
  }

  const auto version = read_u16(payload, 0);
  if (version != 1) {
    return tree_error(CodecErrorCode::unsupported_version, section->offset, "section_version",
                      "unsupported MOVE_TREE Section version", 1, version);
  }
  const auto header_size = read_u16(payload, 2);
  if (header_size != kHeaderSize) {
    return tree_error(CodecErrorCode::invalid_tree, section->offset + 2, "header_size",
                      "MOVE_TREE header_size must be 16", kHeaderSize, header_size);
  }
  const auto record_size = read_u16(payload, 4);
  if (record_size != kNodeRecordSize) {
    return tree_error(CodecErrorCode::invalid_tree, section->offset + 4, "node_record_size",
                      "MOVE_TREE node_record_size must be 32", kNodeRecordSize, record_size);
  }
  const auto tree_flags = read_u16(payload, 6);
  if (tree_flags != 0) {
    return tree_error(CodecErrorCode::invalid_tree, section->offset + 6, "tree_flags",
                      "MOVE_TREE flags must be zero in v1", 0, tree_flags);
  }
  const auto count = static_cast<std::size_t>(read_u32(payload, 8));
  if (count == 0) {
    return tree_error(CodecErrorCode::invalid_tree, section->offset + 8, "node_count",
                      "MOVE_TREE must contain a root node", 1, 0);
  }
  if (count > limits.max_nodes) {
    return tree_error(CodecErrorCode::resource_limit, section->offset + 8, "node_count",
                      "node count limit exceeded", limits.max_nodes, count);
  }
  const auto root_index = read_u32(payload, 12);
  if (root_index != 0) {
    return tree_error(CodecErrorCode::invalid_tree, section->offset + 12, "root_index",
                      "MOVE_TREE root_index must be zero", 0, root_index);
  }

  std::size_t records_size = 0;
  std::size_t expected_size = 0;
  if (!checked_multiply(count, kNodeRecordSize, records_size) ||
      !checked_add(kHeaderSize, records_size, expected_size)) {
    return tree_error(CodecErrorCode::integer_overflow, section->offset + 8, "node_count",
                      "MOVE_TREE size calculation overflowed", {}, count);
  }
  if (payload.size() != expected_size) {
    return tree_error(CodecErrorCode::invalid_tree, section->offset + 8, "node_count",
                      "MOVE_TREE size does not match node_count", expected_size, payload.size());
  }

  std::vector<RawNode> raw;
  raw.reserve(count);
  DecodedMoveTree result;
  result.value.nodes.clear();
  result.value.nodes.resize(count);
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t cursor = kHeaderSize + index * kNodeRecordSize;
    RawNode node;
    node.parent = read_u32(payload, cursor);
    node.first_child = read_u32(payload, cursor + 4);
    node.next_sibling = read_u32(payload, cursor + 8);
    node.first_annotation = read_u32(payload, cursor + 12);
    node.from_square = std::to_integer<std::uint8_t>(payload[cursor + 16]);
    node.to_square = std::to_integer<std::uint8_t>(payload[cursor + 17]);
    const auto move_kind = std::to_integer<std::uint8_t>(payload[cursor + 18]);
    const auto move_flags = std::to_integer<std::uint8_t>(payload[cursor + 19]);
    node.ply = read_u32(payload, cursor + 20);
    const auto node_flags = read_u32(payload, cursor + 24);
    const auto reserved = read_u32(payload, cursor + 28);

    const auto valid_node_index = [count](std::uint32_t value) {
      return value == kNoNode || value < count;
    };
    if (!valid_node_index(node.parent)) {
      return tree_error(CodecErrorCode::invalid_tree, section->offset + cursor, "parent_index",
                        "parent_index is outside the Node table", count, node.parent);
    }
    if (!valid_node_index(node.first_child)) {
      return tree_error(CodecErrorCode::invalid_tree, section->offset + cursor + 4,
                        "first_child_index", "first_child_index is outside the Node table", count,
                        node.first_child);
    }
    if (!valid_node_index(node.next_sibling)) {
      return tree_error(CodecErrorCode::invalid_tree, section->offset + cursor + 8,
                        "next_sibling_index", "next_sibling_index is outside the Node table", count,
                        node.next_sibling);
    }
    if (node.first_annotation > annotations.records.size()) {
      return tree_error(CodecErrorCode::invalid_annotation, section->offset + cursor + 12,
                        "first_annotation_id",
                        "first_annotation_id is outside the Annotation table",
                        annotations.records.size(), node.first_annotation);
    }
    if (node_flags != 0) {
      return tree_error(CodecErrorCode::invalid_tree, section->offset + cursor + 24, "node_flags",
                        "Node flags must be zero in v1", 0, node_flags);
    }
    if (reserved != 0) {
      return tree_error(CodecErrorCode::invalid_tree, section->offset + cursor + 28, "reserved",
                        "Node reserved bytes must be zero", 0, reserved);
    }

    if (index == 0) {
      if (node.parent != kNoNode || node.next_sibling != kNoNode || node.ply != 0 ||
          node.from_square != 0xffU || node.to_square != 0xffU || move_kind != 0xffU ||
          move_flags != 0xffU) {
        return tree_error(CodecErrorCode::invalid_tree, section->offset + cursor, "root_node",
                          "root must have no parent or sibling, ply zero, and an all-FF Move");
      }
    } else {
      if (node.parent == kNoNode) {
        return tree_error(CodecErrorCode::invalid_tree, section->offset + cursor, "parent_index",
                          "non-root Node must have a parent");
      }
      if (node.from_square > 89 || node.to_square > 89 ||
          node.from_square == node.to_square || move_kind != 0 || move_flags != 0) {
        return tree_error(CodecErrorCode::invalid_move, section->offset + cursor + 16, "move",
                          "non-root Move must use distinct squares in 0..89, kind zero, and flags zero");
      }
      result.value.nodes[index].parent = node.parent;
      result.value.nodes[index].move = Move{node.from_square, node.to_square};
    }
    raw.push_back(node);
  }

  std::vector<std::size_t> incoming(count, 0);
  std::vector<std::size_t> sibling_seen(count, std::numeric_limits<std::size_t>::max());
  for (std::size_t parent = 0; parent < count; ++parent) {
    std::uint32_t child = raw[parent].first_child;
    while (child != kNoNode) {
      if (sibling_seen[child] == parent) {
        return tree_error(CodecErrorCode::invalid_tree, node_offset(*section, child) + 8,
                          "next_sibling_index", "child/sibling chain contains a cycle", {}, child);
      }
      sibling_seen[child] = parent;
      if (child == 0) {
        return tree_error(CodecErrorCode::invalid_tree, node_offset(*section, parent) + 4,
                          "first_child_index", "root Node cannot occur in a child chain");
      }
      if (++incoming[child] != 1) {
        return tree_error(CodecErrorCode::invalid_tree, node_offset(*section, child),
                          "parent_index", "Node occurs in more than one child chain", {}, child);
      }
      if (raw[child].parent != parent) {
        return tree_error(CodecErrorCode::invalid_tree, node_offset(*section, child),
                          "parent_index", "parent_index disagrees with the child/sibling chain",
                          parent, raw[child].parent);
      }
      if (static_cast<std::uint64_t>(raw[child].ply) !=
          static_cast<std::uint64_t>(raw[parent].ply) + 1U) {
        return tree_error(CodecErrorCode::invalid_tree, node_offset(*section, child) + 20, "ply",
                          "child ply must equal parent ply plus one",
                          static_cast<std::uint64_t>(raw[parent].ply) + 1U, raw[child].ply);
      }
      result.value.nodes[parent].children.push_back(child);
      child = raw[child].next_sibling;
    }
  }
  for (std::size_t index = 1; index < count; ++index) {
    if (incoming[index] != 1) {
      return tree_error(CodecErrorCode::invalid_tree, node_offset(*section, index), "parent_index",
                        "every non-root Node must occur in exactly one child chain", 1,
                        incoming[index]);
    }
  }

  struct Frame {
    std::size_t node{};
    std::size_t next_child{};
    std::size_t depth{};
  };
  std::vector<std::uint8_t> color(count, 0);
  std::vector<Frame> stack{{0, 0, 0}};
  color[0] = 1;
  std::size_t preorder_index = 0;
  while (!stack.empty()) {
    auto& frame = stack.back();
    if (frame.next_child == 0) {
      if (frame.node != preorder_index++) {
        result.canonical_order = false;
      }
      if (frame.depth > limits.max_tree_depth) {
        return tree_error(CodecErrorCode::resource_limit,
                          node_offset(*section, frame.node) + 20, "ply",
                          "tree depth limit exceeded", limits.max_tree_depth, frame.depth);
      }
    }
    if (frame.next_child == result.value.nodes[frame.node].children.size()) {
      color[frame.node] = 2;
      stack.pop_back();
      continue;
    }
    const auto child = result.value.nodes[frame.node].children[frame.next_child++];
    if (color[child] == 1) {
      return tree_error(CodecErrorCode::invalid_tree, node_offset(*section, child), "parent_index",
                        "Move Tree contains a cycle", {}, child);
    }
    if (color[child] == 0) {
      color[child] = 1;
      stack.push_back({child, 0, frame.depth + 1});
    }
  }
  for (std::size_t index = 0; index < count; ++index) {
    if (color[index] == 0) {
      return tree_error(CodecErrorCode::invalid_tree, node_offset(*section, index), "parent_index",
                        "Node is not reachable from the root", {}, index);
    }
  }

  const auto annotation_section = std::ranges::find(container.sections, kAnnotationSectionType,
                                                    &SectionView::type);
  const std::size_t annotation_base =
      annotation_section == container.sections.end() ? 0 : annotation_section->offset;
  const auto annotation_next_offset = [annotation_base](std::size_t index) {
    return annotation_base + kAnnotationHeaderSize + index * kAnnotationRecordSize + 4;
  };
  std::vector<std::size_t> annotation_owner(
      annotations.records.size(), std::numeric_limits<std::size_t>::max());
  for (std::size_t node = 0; node < count; ++node) {
    std::uint32_t annotation_id = raw[node].first_annotation;
    while (annotation_id != 0) {
      const std::size_t annotation_index = annotation_id - 1U;
      if (annotation_owner[annotation_index] != std::numeric_limits<std::size_t>::max()) {
        return annotation_error(annotation_next_offset(annotation_index), "next_annotation_id",
                                "Annotation chain contains a cycle or is shared by multiple Nodes",
                                annotation_id);
      }
      annotation_owner[annotation_index] = node;
      const auto& record = annotations.records[annotation_index];
      if (node == 0 && record.value.before_move) {
        return annotation_error(annotation_next_offset(annotation_index) + 6, "flags",
                                "root Node Annotation cannot set BEFORE_MOVE", annotation_id);
      }
      result.value.nodes[node].annotations.push_back(record.value);
      annotation_id = record.next_annotation_id;
    }
  }
  for (std::size_t index = 0; index < annotation_owner.size(); ++index) {
    if (annotation_owner[index] == std::numeric_limits<std::size_t>::max()) {
      return annotation_error(annotation_next_offset(index) - 4, "annotation_id",
                              "Annotation is not reachable from any Node", index + 1U);
    }
  }
  return result;
}

}  // namespace oxq::core::detail
