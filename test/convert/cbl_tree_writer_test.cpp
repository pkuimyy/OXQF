#include "cbl/container.hpp"
#include "cbl/record.hpp"
#include "cbl/tree.hpp"
#include "cbl/tree_writer.hpp"

#include <oxq/convert/cbl_writer.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] oxq::core::GameModel branching_game() {
  oxq::core::GameModel game;
  game.uuid = *oxq::core::Uuid::parse("526fff92-a6c3-43c0-9b03-ae68ae4f8a73");
  game.metadata.result = oxq::core::GameResult::unknown;
  game.initial_position.fullmove_number = 1;
  game.move_tree.nodes = {
      {{}, {}, {1, 4}, {{oxq::core::AnnotationKind::comment, false,
                         "根注释😀", {}, {}}}},
      {0, oxq::core::Move{10, 20}, {2, 3},
       {{oxq::core::AnnotationKind::comment, false, "主线", {}, {}}}},
      {1, oxq::core::Move{20, 30}, {}, {}},
      {1, oxq::core::Move{20, 31}, {},
       {{oxq::core::AnnotationKind::comment, false, "变化\n第二行", {}, {}}}},
      {0, oxq::core::Move{11, 21}, {5}, {}},
      {4, oxq::core::Move{21, 32}, {}, {}},
  };
  game.metadata.extensions["org.openxiangqi.cbl"]["source_controls"] =
      std::vector<std::string>{"0100", "0200", "0300", "0400", "0500", "0100"};
  return game;
}

struct DecodedRecord {
  oxq::convert::detail::CblRecordView record;
  oxq::convert::detail::CblMoveTreeView tree;
};

[[nodiscard]] std::variant<DecodedRecord, oxq::convert::CblError> decode(
    const std::vector<std::byte>& bytes) {
  oxq::convert::detail::CblContainerView container;
  container.input = bytes;
  oxq::convert::detail::CblDirectoryEntryView entry;
  entry.used_size = static_cast<std::uint32_t>(bytes.size());
  entry.allocated_size = bytes.size();
  entry.kind = oxq::convert::detail::CblResourceKind::live_game;
  auto record_outcome = oxq::convert::detail::read_cbl_record(container, entry);
  if (std::holds_alternative<oxq::convert::CblError>(record_outcome)) {
    return std::get<oxq::convert::CblError>(std::move(record_outcome));
  }
  auto record =
      std::get<oxq::convert::detail::CblRecordView>(std::move(record_outcome));
  auto tree_outcome =
      oxq::convert::detail::read_cbl_move_tree(container, entry, record);
  if (std::holds_alternative<oxq::convert::CblError>(tree_outcome)) {
    return std::get<oxq::convert::CblError>(std::move(tree_outcome));
  }
  return DecodedRecord{
      std::move(record),
      std::get<oxq::convert::detail::CblMoveTreeView>(std::move(tree_outcome))};
}

[[nodiscard]] oxq::convert::CblWritePlan preflight(
    const oxq::core::GameModel& game,
    oxq::convert::ConversionMode mode = oxq::convert::ConversionMode::strict) {
  oxq::convert::CblWriteOptions options;
  options.mode = mode;
  options.library.uuid =
      *oxq::core::Uuid::parse("4c5415f9-e3e7-4df6-8614-19ac28832ad1");
  return std::get<oxq::convert::CblWritePlan>(
      oxq::convert::preflight_cbl_write(
          std::span<const oxq::core::GameModel>{&game, 1}, options));
}

}  // namespace

int main() {
  const auto game = branching_game();
  const auto plan = preflight(game);
  if (plan.report.rejected || plan.report.has_loss()) {
    return 1;
  }
  const auto first = oxq::convert::detail::encode_cbl_record(game);
  const auto second = oxq::convert::detail::encode_cbl_record(game);
  if (first != second || first.size() != plan.record_sizes[0]) {
    return 2;
  }
  const auto decoded_outcome = decode(first);
  if (!std::holds_alternative<DecodedRecord>(decoded_outcome)) {
    return 3;
  }
  const auto& decoded = std::get<DecodedRecord>(decoded_outcome);
  if (decoded.tree.tree != game.move_tree || decoded.tree.comment_count != 3) {
    return 4;
  }
  constexpr std::array<std::uint16_t, 6> expected_controls{
      0x0104, 0x0206, 0x0303, 0x0405, 0x0500, 0x0101};
  if (decoded.tree.source_controls.size() != expected_controls.size()) {
    return 5;
  }
  for (std::size_t index = 0; index < expected_controls.size(); ++index) {
    if (decoded.tree.source_controls[index] != expected_controls[index]) {
      return 6;
    }
  }

  auto normalized = branching_game();
  normalized.move_tree.nodes[0].annotations[0].text = "a\r\nb\rc";
  normalized.move_tree.nodes[0].annotations.push_back(
      {oxq::core::AnnotationKind::comment, false, "d", {}, {}});
  const auto normalized_plan =
      preflight(normalized, oxq::convert::ConversionMode::lenient);
  if (normalized_plan.report.rejected || !normalized_plan.report.has_loss()) {
    return 7;
  }
  const auto normalized_bytes =
      oxq::convert::detail::encode_cbl_record(normalized);
  if (normalized_bytes.size() != normalized_plan.record_sizes[0]) {
    return 8;
  }
  const auto normalized_decoded = decode(normalized_bytes);
  if (std::get<DecodedRecord>(normalized_decoded)
          .tree.tree.nodes[0].annotations[0].text != "a\nb\nc\n\nd") {
    return 9;
  }

  auto deep = branching_game();
  deep.metadata.extensions.clear();
  deep.move_tree.nodes.clear();
  constexpr std::size_t depth = 20'000;
  deep.move_tree.nodes.resize(depth + 1U);
  for (std::size_t index = 1; index <= depth; ++index) {
    deep.move_tree.nodes[index].parent = index - 1U;
    deep.move_tree.nodes[index].move = oxq::core::Move{
        static_cast<std::uint8_t>((index - 1U) % 89U),
        static_cast<std::uint8_t>(index % 89U)};
    deep.move_tree.nodes[index - 1U].children.push_back(index);
  }
  const auto deep_plan = preflight(deep);
  if (deep_plan.report.rejected) {
    return 10;
  }
  const auto deep_bytes = oxq::convert::detail::encode_cbl_record(deep);
  if (deep_bytes.size() != deep_plan.record_sizes[0]) {
    return 11;
  }
  const auto deep_decoded = decode(deep_bytes);
  if (!std::holds_alternative<DecodedRecord>(deep_decoded) ||
      std::get<DecodedRecord>(deep_decoded).tree.tree.nodes.size() != depth + 1U) {
    return 12;
  }
  return 0;
}
