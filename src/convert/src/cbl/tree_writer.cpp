#include "cbl/tree_writer.hpp"

#include "cbl/record_writer.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oxq::convert::detail {
namespace {

constexpr std::string_view kExtensionNamespace = "org.openxiangqi.cbl";

void append_u16(std::vector<std::byte>& output, std::uint16_t value) {
  output.push_back(static_cast<std::byte>(value & 0xffU));
  output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void write_u32(std::span<std::byte> output, std::size_t offset,
               std::uint32_t value) noexcept {
  for (unsigned index = 0; index < 4; ++index) {
    output[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

[[nodiscard]] std::uint32_t next_code_point(std::string_view text,
                                            std::size_t& cursor) noexcept {
  const auto first = static_cast<unsigned char>(text[cursor++]);
  if (first < 0x80U) {
    return first;
  }
  std::size_t continuation_count = 0;
  std::uint32_t result = 0;
  if ((first & 0xe0U) == 0xc0U) {
    continuation_count = 1;
    result = first & 0x1fU;
  } else if ((first & 0xf0U) == 0xe0U) {
    continuation_count = 2;
    result = first & 0x0fU;
  } else {
    continuation_count = 3;
    result = first & 0x07U;
  }
  for (std::size_t index = 0; index < continuation_count; ++index) {
    result = (result << 6U) |
             (static_cast<unsigned char>(text[cursor++]) & 0x3fU);
  }
  return result;
}

void append_code_point(std::vector<std::byte>& output,
                       std::uint32_t code_point) {
  if (code_point <= 0xffffU) {
    append_u16(output, static_cast<std::uint16_t>(code_point));
    return;
  }
  const auto value = code_point - 0x10000U;
  append_u16(output, static_cast<std::uint16_t>(0xd800U + (value >> 10U)));
  append_u16(output,
             static_cast<std::uint16_t>(0xdc00U + (value & 0x3ffU)));
}

void append_normalized_utf16(std::vector<std::byte>& output,
                             std::string_view text) {
  for (std::size_t cursor = 0; cursor < text.size();) {
    if (text[cursor] == '\r') {
      append_u16(output, 0x000aU);
      ++cursor;
      if (cursor < text.size() && text[cursor] == '\n') {
        ++cursor;
      }
      continue;
    }
    append_code_point(output, next_code_point(text, cursor));
  }
}

void append_comment(std::vector<std::byte>& output,
                    const std::vector<core::Annotation>& annotations) {
  const auto length_offset = output.size();
  output.resize(output.size() + 4U);
  const auto text_offset = output.size();
  for (std::size_t index = 0; index < annotations.size(); ++index) {
    if (index != 0) {
      append_u16(output, 0x000aU);
      append_u16(output, 0x000aU);
    }
    append_normalized_utf16(output, annotations[index].text);
  }
  write_u32(output, length_offset,
            static_cast<std::uint32_t>(output.size() - text_offset));
}

[[nodiscard]] const std::vector<std::string>* source_controls(
    const core::GameModel& game) noexcept {
  const auto name_space = game.metadata.extensions.find(kExtensionNamespace);
  if (name_space == game.metadata.extensions.end()) {
    return nullptr;
  }
  const auto property = name_space->second.find("source_controls");
  if (property == name_space->second.end() ||
      !std::holds_alternative<std::vector<std::string>>(property->second)) {
    return nullptr;
  }
  const auto& controls =
      std::get<std::vector<std::string>>(property->second);
  return controls.size() == game.move_tree.nodes.size() ? &controls : nullptr;
}

[[nodiscard]] std::uint16_t opaque_control(
    const std::vector<std::string>* controls, std::size_t node_index) noexcept {
  if (controls == nullptr || (*controls)[node_index].size() != 4U) {
    return 0;
  }
  std::uint16_t value = 0;
  const auto& text = (*controls)[node_index];
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                      value, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return 0;
  }
  return value & 0xfff8U;
}

[[nodiscard]] std::uint8_t disk_square(std::uint8_t core_square) noexcept {
  const auto rank = static_cast<std::uint8_t>(core_square / 9U);
  const auto file = static_cast<std::uint8_t>(core_square % 9U);
  return static_cast<std::uint8_t>((9U - rank) * 9U + file);
}

}  // namespace

std::vector<std::byte> encode_cbl_record(const core::GameModel& game) {
  auto output = encode_cbl_record_prefix(game);
  if (!game.move_tree.nodes[0].annotations.empty()) {
    append_comment(output, game.move_tree.nodes[0].annotations);
  }

  struct PendingNode {
    std::size_t node_index;
    bool has_sibling;
  };
  std::vector<PendingNode> pending;
  const auto& root_children = game.move_tree.nodes[0].children;
  for (std::size_t position = root_children.size(); position > 0; --position) {
    pending.push_back({root_children[position - 1U], position < root_children.size()});
  }

  const auto* controls = source_controls(game);
  while (!pending.empty()) {
    const auto task = pending.back();
    pending.pop_back();
    const auto& node = game.move_tree.nodes[task.node_index];
    std::uint16_t control = opaque_control(controls, task.node_index);
    if (node.children.empty()) {
      control |= 0x0001U;
    }
    if (task.has_sibling) {
      control |= 0x0002U;
    }
    if (!node.annotations.empty()) {
      control |= 0x0004U;
    }
    append_u16(output, control);
    output.push_back(static_cast<std::byte>(disk_square(node.move->from_square)));
    output.push_back(static_cast<std::byte>(disk_square(node.move->to_square)));
    if (!node.annotations.empty()) {
      append_comment(output, node.annotations);
    }

    for (std::size_t position = node.children.size(); position > 0; --position) {
      pending.push_back(
          {node.children[position - 1U], position < node.children.size()});
    }
  }
  return output;
}

}  // namespace oxq::convert::detail
