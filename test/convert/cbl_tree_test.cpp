#include "cbl/container.hpp"
#include "cbl/record.hpp"
#include "cbl/tree.hpp"

#include <oxq/convert/cbl_reader.hpp>
#include <oxq/core/validation.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using oxq::convert::detail::CblContainerView;
using oxq::convert::detail::CblDirectoryEntryView;
using oxq::convert::detail::CblMoveTreeView;
using oxq::convert::detail::CblRecordView;

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> characters{std::istreambuf_iterator<char>{stream},
                                     std::istreambuf_iterator<char>{}};
  std::vector<std::byte> result;
  result.reserve(characters.size());
  for (const char character : characters) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return result;
}

[[nodiscard]] std::vector<const CblDirectoryEntryView*> games(
    const CblContainerView& container) {
  std::vector<const CblDirectoryEntryView*> result;
  for (const auto& entry : container.entries) {
    if (entry.kind == oxq::convert::detail::CblResourceKind::live_game) {
      result.push_back(&entry);
    }
  }
  return result;
}

struct ParsedFirst {
  std::size_t resource_offset{};
  CblRecordView record;
  CblMoveTreeView tree;
};

[[nodiscard]] std::variant<ParsedFirst, oxq::convert::CblError> parse_first(
    const std::vector<std::byte>& bytes,
    const oxq::convert::CblReaderLimits& limits = {}) {
  auto container_result = oxq::convert::detail::inspect_cbl_container(bytes, limits);
  if (std::holds_alternative<oxq::convert::CblError>(container_result)) {
    return std::get<oxq::convert::CblError>(std::move(container_result));
  }
  auto container =
      std::get<oxq::convert::detail::CblContainerView>(std::move(container_result));
  std::size_t entry_index = 0;
  while (entry_index < container.entries.size() &&
         container.entries[entry_index].kind !=
             oxq::convert::detail::CblResourceKind::live_game) {
    ++entry_index;
  }
  if (entry_index == container.entries.size()) {
    return oxq::convert::CblError{oxq::convert::CblErrorCode::invalid_record,
                                  0, {}, "test", "no game", {}, {}};
  }
  const auto record_result = oxq::convert::detail::read_cbl_record(
      container, container.entries[entry_index]);
  if (std::holds_alternative<oxq::convert::CblError>(record_result)) {
    return std::get<oxq::convert::CblError>(record_result);
  }
  auto record = std::get<CblRecordView>(record_result);
  auto tree_result = oxq::convert::detail::read_cbl_move_tree(
      container, container.entries[entry_index], record, limits);
  if (std::holds_alternative<oxq::convert::CblError>(tree_result)) {
    return std::get<oxq::convert::CblError>(std::move(tree_result));
  }
  return ParsedFirst{container.entries[entry_index].resource_offset, std::move(record),
                     std::get<CblMoveTreeView>(std::move(tree_result))};
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset,
               std::uint32_t value) {
  for (unsigned index = 0; index < 4; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

[[nodiscard]] bool error_is(const std::variant<ParsedFirst, oxq::convert::CblError>& result,
                            oxq::convert::CblErrorCode code,
                            std::string_view field) {
  return std::holds_alternative<oxq::convert::CblError>(result) &&
         std::get<oxq::convert::CblError>(result).code == code &&
         std::get<oxq::convert::CblError>(result).field == field;
}

}  // namespace

int main() {
  const std::filesystem::path baseline{OXQF_GOLD_BASELINE_DIRECTORY};
  struct Expected {
    std::string_view file;
    std::vector<std::size_t> nodes_per_game;
    std::vector<std::size_t> comments_per_game;
  };
  const std::array expected{
      Expected{"cbl_01_game_empty.CBL", {0}, {0}},
      Expected{"cbl_02_one_ply.CBL", {1}, {0}},
      Expected{"cbl_03_two_plies.CBL", {2}, {0}},
      Expected{"cbl_04_mainline.CBL", {8}, {0}},
      Expected{"cbl_05_variation.CBL", {4}, {0}},
      Expected{"cbl_06_nested_variation.CBL", {6}, {0}},
      Expected{"cbl_07_comments.CBL", {4}, {5}},
      Expected{"cbl_08_metadata.CBL", {0}, {0}},
      Expected{"cbl_09_custom_position.CBL", {1}, {0}},
      Expected{"cbl_10_two_games.CBL", {1, 1}, {0, 0}},
      Expected{"cbl_11_nested_folders.CBL", {0, 0}, {0, 0}},
  };
  for (const auto& item : expected) {
    const auto bytes = read_file(baseline / item.file);
    const auto container_result =
        oxq::convert::detail::inspect_cbl_container(bytes);
    if (!std::holds_alternative<CblContainerView>(container_result)) {
      return 1;
    }
    const auto& container = std::get<CblContainerView>(container_result);
    const auto entries = games(container);
    if (entries.size() != item.nodes_per_game.size()) {
      return 2;
    }
    for (std::size_t index = 0; index < entries.size(); ++index) {
      const auto record_result =
          oxq::convert::detail::read_cbl_record(container, *entries[index]);
      if (!std::holds_alternative<CblRecordView>(record_result)) {
        return 3;
      }
      const auto& record = std::get<CblRecordView>(record_result);
      const auto tree_result = oxq::convert::detail::read_cbl_move_tree(
          container, *entries[index], record);
      if (!std::holds_alternative<CblMoveTreeView>(tree_result)) {
        return 4;
      }
      const auto& tree = std::get<CblMoveTreeView>(tree_result);
      if (tree.tree.nodes.size() != item.nodes_per_game[index] + 1 ||
          tree.source_controls.size() != tree.tree.nodes.size() ||
          tree.comment_count != item.comments_per_game[index]) {
        return 5;
      }
      oxq::core::GameModel game;
      game.uuid = record.guid;
      game.initial_position = record.position;
      game.move_tree = tree.tree;
      if (oxq::core::has_errors(oxq::core::validate(game))) {
        return 17;
      }
    }
  }

  const auto nested_result =
      parse_first(read_file(baseline / "cbl_06_nested_variation.CBL"));
  if (!std::holds_alternative<ParsedFirst>(nested_result)) {
    return 6;
  }
  const auto& nested = std::get<ParsedFirst>(nested_result).tree;
  const auto& nodes = nested.tree.nodes;
  if (nodes[0].children != std::vector<std::size_t>{1, 6} ||
      nodes[1].children != std::vector<std::size_t>{2, 5} ||
      nodes[2].children != std::vector<std::size_t>{3, 4} ||
      nodes[1].move != oxq::core::Move{25, 22} ||
      nodes[2].move != oxq::core::Move{88, 69} ||
      nodes[3].move != oxq::core::Move{7, 24} ||
      nodes[4].move != oxq::core::Move{29, 38} ||
      nodes[5].move != oxq::core::Move{70, 67} ||
      nodes[6].move != oxq::core::Move{7, 24} ||
      nested.source_controls[2] != 0x0102) {
    return 7;
  }

  const auto comments_result =
      parse_first(read_file(baseline / "cbl_07_comments.CBL"));
  if (!std::holds_alternative<ParsedFirst>(comments_result)) {
    return 8;
  }
  const auto& comments = std::get<ParsedFirst>(comments_result).tree.tree.nodes;
  if (comments[0].annotations[0].text != "COMMENT_ROOT_甲" ||
      comments[1].annotations[0].text != "COMMENT_MOVE_乙" ||
      comments[2].annotations[0].text != "COMMENT_MOVE_丙" ||
      comments[3].annotations[0].text != "COMMENT_VARIATION_丁" ||
      comments[4].annotations[0].text !=
          "COMMENT_MULTILINE_戊\n第二行 ABC 123\n第三行：标点，。！？" ||
      comments[0].children != std::vector<std::size_t>{1, 3} ||
      comments[1].children != std::vector<std::size_t>{2} ||
      comments[3].children != std::vector<std::size_t>{4}) {
    return 9;
  }

  auto invalid = read_file(baseline / "cbl_07_comments.CBL");
  auto valid = parse_first(invalid);
  if (!std::holds_alternative<ParsedFirst>(valid)) {
    return 10;
  }
  const auto record_offset = std::get<ParsedFirst>(valid).resource_offset;
  write_u32(invalid, record_offset + 0x8aa, 1);
  if (!error_is(parse_first(invalid),
                oxq::convert::CblErrorCode::invalid_comment,
                "comment_length")) {
    return 11;
  }

  invalid = read_file(baseline / "cbl_07_comments.CBL");
  write_u32(invalid, record_offset + 0x8aa, 4000);
  if (!error_is(parse_first(invalid),
                oxq::convert::CblErrorCode::truncated_input,
                "comment_text")) {
    return 12;
  }

  invalid = read_file(baseline / "cbl_02_one_ply.CBL");
  valid = parse_first(invalid);
  if (!std::holds_alternative<ParsedFirst>(valid)) {
    return 22;
  }
  const auto one_move_offset = std::get<ParsedFirst>(valid).resource_offset;
  invalid[one_move_offset + 0x8ac] = std::byte{90};
  if (!error_is(parse_first(invalid),
                oxq::convert::CblErrorCode::invalid_move_tree, "move_from")) {
    return 13;
  }

  invalid = read_file(baseline / "cbl_02_one_ply.CBL");
  invalid[one_move_offset + 0x8aa] = std::byte{0};
  if (!error_is(parse_first(invalid),
                oxq::convert::CblErrorCode::truncated_input, "move_node")) {
    return 14;
  }

  invalid = read_file(baseline / "cbl_02_one_ply.CBL");
  invalid[one_move_offset + 0x8a6] = std::byte{1};
  if (!error_is(parse_first(invalid),
                oxq::convert::CblErrorCode::invalid_move_tree,
                "move_tree_closure")) {
    return 18;
  }

  invalid = read_file(baseline / "cbl_02_one_ply.CBL");
  invalid[one_move_offset + 0x8a6] = std::byte{2};
  if (!error_is(parse_first(invalid),
                oxq::convert::CblErrorCode::invalid_move_tree,
                "root_control")) {
    return 19;
  }

  invalid = read_file(baseline / "cbl_07_comments.CBL");
  invalid[record_offset + 0x8ae] = std::byte{0};
  invalid[record_offset + 0x8af] = std::byte{0xd8};
  if (!error_is(parse_first(invalid),
                oxq::convert::CblErrorCode::invalid_utf16,
                "comment_text")) {
    return 20;
  }

  oxq::convert::CblReaderLimits limits;
  limits.max_nodes = 0;
  if (!error_is(parse_first(read_file(baseline / "cbl_02_one_ply.CBL"), limits),
                oxq::convert::CblErrorCode::resource_limit,
                "move_node_count")) {
    return 15;
  }
  limits.max_nodes = 10;
  limits.max_tree_depth = 1;
  if (!error_is(parse_first(read_file(baseline / "cbl_03_two_plies.CBL"), limits),
                oxq::convert::CblErrorCode::resource_limit,
                "move_tree_depth")) {
    return 16;
  }
  limits.max_tree_depth = 10;
  limits.max_comment_bytes = 10;
  if (!error_is(parse_first(read_file(baseline / "cbl_07_comments.CBL"), limits),
                oxq::convert::CblErrorCode::resource_limit,
                "comment_length")) {
    return 21;
  }
  return 0;
}
