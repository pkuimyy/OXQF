#include "cbl/container.hpp"

#include <oxq/convert/cbl_reader.hpp>
#include <oxq/convert/cbl_writer.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] oxq::core::GameModel game(std::string_view uuid,
                                        std::string title) {
  oxq::core::GameModel result;
  result.uuid = *oxq::core::Uuid::parse(uuid);
  result.metadata.title = std::move(title);
  result.metadata.result = oxq::core::GameResult::unknown;
  result.initial_position.side_to_move = oxq::core::Side::red;
  result.initial_position.fullmove_number = 1;
  return result;
}

[[nodiscard]] oxq::convert::CblWriteOptions options() {
  oxq::convert::CblWriteOptions result;
  result.mode = oxq::convert::ConversionMode::strict;
  result.library.uuid =
      *oxq::core::Uuid::parse("4c5415f9-e3e7-4df6-8614-19ac28832ad1");
  result.library.name = "M5 测试库😀";
  result.library.author = "Open Xiangqi";
  result.library.author_email = "test@example.org";
  result.library.created_at = "2026-09-04";
  result.library.modified_at = "2026-09-04T12:34:56+08:00";
  return result;
}

[[nodiscard]] const oxq::convert::CblWriteResult* write_result(
    const oxq::convert::CblWriteOutcome& outcome) {
  return std::get_if<oxq::convert::CblWriteResult>(&outcome);
}

}  // namespace

int main() {
  auto write_options = options();
  const std::vector<oxq::core::GameModel> no_games;
  const auto empty_outcome = oxq::convert::write_cbl(no_games, write_options);
  const auto* empty = write_result(empty_outcome);
  if (empty == nullptr || empty->report.rejected ||
      empty->directory_capacity != 128 ||
      empty->bytes.size() != 0x10440 + 128 * 0x114) {
    return 1;
  }
  const auto empty_inspect = oxq::convert::inspect_cbl(empty->bytes);
  if (!std::holds_alternative<oxq::convert::CblLibraryInfo>(empty_inspect)) {
    return 2;
  }
  const auto& empty_library =
      std::get<oxq::convert::CblLibraryInfo>(empty_inspect);
  if (empty_library.uuid != *write_options.library.uuid ||
      empty_library.name != write_options.library.name ||
      empty_library.author != write_options.library.author ||
      empty_library.author_email != write_options.library.author_email ||
      empty_library.created_at != write_options.library.created_at ||
      empty_library.modified_at != write_options.library.modified_at ||
      empty_library.live_game_count != 0 || empty_library.total_blocks != 0 ||
      empty_library.trailing_bytes != 0) {
    return 3;
  }
  const auto repeated_empty = oxq::convert::write_cbl(no_games, write_options);
  const auto* repeated_empty_result = write_result(repeated_empty);
  if (repeated_empty_result == nullptr ||
      empty->bytes != repeated_empty_result->bytes) {
    return 4;
  }

  auto first = game("526fff92-a6c3-43c0-9b03-ae68ae4f8a73", "分支棋局");
  first.initial_position.pieces = {
      {oxq::core::Side::red, oxq::core::PieceType::king, 4},
      {oxq::core::Side::black, oxq::core::PieceType::king, 85},
  };
  first.move_tree.nodes = {
      {{}, {}, {1, 3}, {{oxq::core::AnnotationKind::comment, false,
                         "根注释😀", {}, {}}}},
      {0, oxq::core::Move{4, 13}, {2}, {}},
      {1, oxq::core::Move{85, 76}, {},
       {{oxq::core::AnnotationKind::comment, false, "主线\n第二行", {}, {}}}},
      {0, oxq::core::Move{4, 5}, {}, {}},
  };
  auto second = game("a29ab94d-d463-4b46-8224-eb8d56abeddd", "长注释");
  second.move_tree.nodes[0].annotations.push_back(
      {oxq::core::AnnotationKind::comment, false, std::string(5'000, 'x'), {}, {}});
  const std::vector games{first, second};
  write_options.minimum_directory_capacity = 130;
  const auto outcome = oxq::convert::write_cbl(games, write_options);
  const auto* encoded = write_result(outcome);
  if (encoded == nullptr || encoded->report.rejected ||
      encoded->report.has_loss() || encoded->report.converted_game_count != 2 ||
      encoded->directory_capacity != 130) {
    return 5;
  }
  const auto repeated = oxq::convert::write_cbl(games, write_options);
  if (write_result(repeated) == nullptr ||
      write_result(repeated)->bytes != encoded->bytes) {
    return 6;
  }

  const auto container_outcome = oxq::convert::detail::inspect_cbl_container(
      encoded->bytes);
  if (!std::holds_alternative<oxq::convert::detail::CblContainerView>(
          container_outcome)) {
    return 7;
  }
  const auto& container =
      std::get<oxq::convert::detail::CblContainerView>(container_outcome);
  if (container.library.live_game_count != 2 ||
      container.library.total_blocks != 4 ||
      container.entries[0].display_index != 0 ||
      container.entries[1].display_index != 1 ||
      container.entries[0].uuid_text !=
          "{526FFF92-A6C3-43C0-9B03-AE68AE4F8A73}" ||
      container.entries[1].title != "长注释") {
    return 8;
  }
  for (std::size_t index = 0; index < 2; ++index) {
    const auto& entry = container.entries[index];
    const auto padding = encoded->bytes.begin() +
                         static_cast<std::ptrdiff_t>(entry.resource_offset +
                                                     entry.used_size);
    const auto end = encoded->bytes.begin() +
                     static_cast<std::ptrdiff_t>(entry.resource_offset +
                                                 entry.allocated_size);
    if (!std::ranges::all_of(padding, end,
                             [](const std::byte value) { return value == std::byte{}; })) {
      return 9;
    }
  }

  const auto read_outcome = oxq::convert::read_cbl(encoded->bytes);
  if (!std::holds_alternative<oxq::convert::CblReadResult>(read_outcome)) {
    return 10;
  }
  const auto& decoded = std::get<oxq::convert::CblReadResult>(read_outcome);
  if (decoded.games.size() != 2 || decoded.games[0].uuid != first.uuid ||
      decoded.games[1].uuid != second.uuid ||
      decoded.games[0].metadata.title != first.metadata.title ||
      decoded.games[0].initial_position != first.initial_position ||
      decoded.games[0].move_tree != first.move_tree ||
      decoded.games[1].move_tree != second.move_tree) {
    return 11;
  }

  auto lossy = game("82c0348c-2364-49e2-a69d-64e967715bfd", "lossy");
  lossy.metadata.tags.push_back("unsupported");
  const auto rejected = oxq::convert::write_cbl(
      std::span<const oxq::core::GameModel>{&lossy, 1}, write_options);
  const auto* rejected_result = write_result(rejected);
  if (rejected_result == nullptr || !rejected_result->report.rejected ||
      !rejected_result->report.has_loss() || !rejected_result->bytes.empty()) {
    return 12;
  }
  write_options.mode = oxq::convert::ConversionMode::lenient;
  const auto accepted = oxq::convert::write_cbl(
      std::span<const oxq::core::GameModel>{&lossy, 1}, write_options);
  const auto* accepted_result = write_result(accepted);
  if (accepted_result == nullptr || accepted_result->report.rejected ||
      !accepted_result->report.has_loss() || accepted_result->bytes.empty()) {
    return 13;
  }
  return 0;
}
