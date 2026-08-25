#include "cbl/tree.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace oxq::convert::detail {
namespace {

constexpr std::uint16_t kNoChild = 0x0001;
constexpr std::uint16_t kHasSibling = 0x0002;
constexpr std::uint16_t kHasComment = 0x0004;

[[nodiscard]] CblError error(CblErrorCode code, std::size_t offset,
                             std::string field, std::string message,
                             const CblDirectoryEntryView& entry,
                             std::optional<std::uint64_t> expected = {},
                             std::optional<std::uint64_t> actual = {}) {
  return {code, offset, entry.physical_slot, std::move(field),
          std::move(message), expected, actual};
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::byte> input,
                                     std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[offset])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[offset + 1]))
             << 8U);
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> input,
                                     std::size_t offset) noexcept {
  std::uint32_t result = 0;
  for (unsigned index = 0; index < 4; ++index) {
    result |= static_cast<std::uint32_t>(
                  std::to_integer<std::uint8_t>(input[offset + index]))
              << (index * 8U);
  }
  return result;
}

void append_utf8(std::string& output, std::uint32_t code_point) {
  if (code_point <= 0x7fU) {
    output.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7ffU) {
    output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else if (code_point <= 0xffffU) {
    output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else {
    output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  }
}

[[nodiscard]] std::string normalize_newlines(std::string input) {
  std::string result;
  result.reserve(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    if (input[index] != '\r') {
      result.push_back(input[index]);
      continue;
    }
    result.push_back('\n');
    if (index + 1 < input.size() && input[index + 1] == '\n') {
      ++index;
    }
  }
  return result;
}

using CommentOutcome = std::variant<std::string, CblError>;

[[nodiscard]] CommentOutcome read_comment(
    std::span<const std::byte> record, std::size_t& cursor,
    const CblDirectoryEntryView& entry, const CblReaderLimits& limits) {
  const auto length_offset = cursor;
  if (cursor > record.size() || record.size() - cursor < 4) {
    return error(CblErrorCode::truncated_input,
                 entry.resource_offset + cursor, "comment_length",
                 "CBL comment length prefix is truncated", entry, 4,
                 cursor <= record.size() ? record.size() - cursor : 0);
  }
  const auto byte_length = static_cast<std::size_t>(read_u32(record, cursor));
  cursor += 4;
  if (byte_length > limits.max_comment_bytes) {
    return error(CblErrorCode::resource_limit,
                 entry.resource_offset + length_offset, "comment_length",
                 "CBL comment byte length limit exceeded", entry,
                 limits.max_comment_bytes, byte_length);
  }
  if (byte_length % 2 != 0) {
    return error(CblErrorCode::invalid_comment,
                 entry.resource_offset + length_offset, "comment_length",
                 "CBL UTF-16LE comment byte length must be even", entry, {},
                 byte_length);
  }
  if (cursor > record.size() || byte_length > record.size() - cursor) {
    return error(CblErrorCode::truncated_input,
                 entry.resource_offset + cursor, "comment_text",
                 "CBL comment extends beyond Record used_size", entry,
                 byte_length,
                 cursor <= record.size() ? record.size() - cursor : 0);
  }

  std::string result;
  result.reserve(byte_length / 2);
  for (std::size_t consumed = 0; consumed < byte_length; consumed += 2) {
    const auto first = read_u16(record, cursor + consumed);
    std::uint32_t code_point = first;
    if (first >= 0xd800U && first <= 0xdbffU) {
      if (consumed + 4 > byte_length) {
        return error(CblErrorCode::invalid_utf16,
                     entry.resource_offset + cursor + consumed,
                     "comment_text",
                     "high surrogate is truncated by the CBL comment boundary",
                     entry);
      }
      const auto second = read_u16(record, cursor + consumed + 2);
      if (second < 0xdc00U || second > 0xdfffU) {
        return error(CblErrorCode::invalid_utf16,
                     entry.resource_offset + cursor + consumed,
                     "comment_text",
                     "high surrogate is not followed by a low surrogate",
                     entry);
      }
      code_point = 0x10000U +
                   ((static_cast<std::uint32_t>(first) - 0xd800U) << 10U) +
                   (static_cast<std::uint32_t>(second) - 0xdc00U);
      consumed += 2;
    } else if (first >= 0xdc00U && first <= 0xdfffU) {
      return error(CblErrorCode::invalid_utf16,
                   entry.resource_offset + cursor + consumed, "comment_text",
                   "unpaired low surrogate in CBL comment", entry);
    }
    append_utf8(result, code_point);
  }
  cursor += byte_length;
  return normalize_newlines(std::move(result));
}

[[nodiscard]] std::uint8_t to_core_square(std::uint8_t disk_square) noexcept {
  const auto disk_rank = static_cast<std::uint8_t>(disk_square / 9);
  const auto file = static_cast<std::uint8_t>(disk_square % 9);
  return static_cast<std::uint8_t>((9 - disk_rank) * 9 + file);
}

}  // namespace

CblMoveTreeOutcome read_cbl_move_tree(const CblContainerView& container,
                                      const CblDirectoryEntryView& entry,
                                      const CblRecordView& record,
                                      const CblReaderLimits& limits) {
  if (entry.resource_offset > container.input.size() ||
      record.used_size > container.input.size() - entry.resource_offset ||
      record.node_stream_offset > record.used_size) {
    return error(CblErrorCode::truncated_input, entry.resource_offset,
                 "move_tree", "CBL move tree range is outside the Record",
                 entry);
  }
  const auto bytes =
      container.input.subspan(entry.resource_offset, record.used_size);
  std::size_t cursor = record.node_stream_offset;
  CblMoveTreeView result;
  result.source_controls.push_back(record.root_control);

  if ((record.root_control & kHasSibling) != 0) {
    return error(CblErrorCode::invalid_move_tree,
                 entry.resource_offset + 0x8a6, "root_control",
                 "CBL root cannot have a sibling", entry);
  }
  if ((record.root_control & kHasComment) != 0) {
    auto comment = read_comment(bytes, cursor, entry, limits);
    if (std::holds_alternative<CblError>(comment)) {
      return std::get<CblError>(std::move(comment));
    }
    result.tree.nodes[0].annotations.push_back(
        {core::AnnotationKind::comment, false,
         std::get<std::string>(std::move(comment)), {}, {}});
    ++result.comment_count;
  }

  struct PendingNode {
    std::size_t parent;
    std::size_t depth;
  };
  std::vector<PendingNode> pending;
  if ((record.root_control & kNoChild) == 0) {
    pending.push_back({0, 1});
  }

  while (!pending.empty()) {
    const auto task = pending.back();
    pending.pop_back();
    if (result.tree.nodes.size() - 1 >= limits.max_nodes) {
      return error(CblErrorCode::resource_limit,
                   entry.resource_offset + cursor, "move_node_count",
                   "CBL move node limit exceeded", entry, limits.max_nodes,
                   result.tree.nodes.size());
    }
    if (task.depth > limits.max_tree_depth) {
      return error(CblErrorCode::resource_limit,
                   entry.resource_offset + cursor, "move_tree_depth",
                   "CBL move tree depth limit exceeded", entry,
                   limits.max_tree_depth, task.depth);
    }
    if (cursor > bytes.size() || bytes.size() - cursor < 4) {
      return error(CblErrorCode::truncated_input,
                   entry.resource_offset + cursor, "move_node",
                   "CBL move node header is truncated", entry, 4,
                   cursor <= bytes.size() ? bytes.size() - cursor : 0);
    }
    const auto control = read_u16(bytes, cursor);
    const auto from = std::to_integer<std::uint8_t>(bytes[cursor + 2]);
    const auto to = std::to_integer<std::uint8_t>(bytes[cursor + 3]);
    if (from >= 90 || to >= 90) {
      return error(CblErrorCode::invalid_move_tree,
                   entry.resource_offset + cursor + (from >= 90 ? 2 : 3),
                   from >= 90 ? "move_from" : "move_to",
                   "CBL move square must be in 0..89", entry, 89,
                   from >= 90 ? from : to);
    }
    cursor += 4;

    core::MoveNode node;
    node.parent = task.parent;
    node.move = core::Move{to_core_square(from), to_core_square(to)};
    const auto node_index = result.tree.nodes.size();
    result.tree.nodes.push_back(std::move(node));
    result.tree.nodes[task.parent].children.push_back(node_index);
    result.source_controls.push_back(control);

    if ((control & kHasComment) != 0) {
      auto comment = read_comment(bytes, cursor, entry, limits);
      if (std::holds_alternative<CblError>(comment)) {
        return std::get<CblError>(std::move(comment));
      }
      result.tree.nodes[node_index].annotations.push_back(
          {core::AnnotationKind::comment, false,
           std::get<std::string>(std::move(comment)), {}, {}});
      ++result.comment_count;
    }

    // The stream is depth-first preorder. Push the sibling first so the LIFO
    // work list consumes the child subtree before returning to that sibling.
    if ((control & kHasSibling) != 0) {
      pending.push_back({task.parent, task.depth});
    }
    if ((control & kNoChild) == 0) {
      pending.push_back({node_index, task.depth + 1});
    }
  }

  if (cursor != bytes.size()) {
    return error(CblErrorCode::invalid_move_tree,
                 entry.resource_offset + cursor, "move_tree_closure",
                 "CBL move tree closed before Record used_size", entry,
                 bytes.size(), cursor);
  }
  return result;
}

}  // namespace oxq::convert::detail
