#include <oxq/convert/cbl_writer.hpp>

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] oxq::core::GameModel minimal_game() {
  oxq::core::GameModel game;
  game.uuid = *oxq::core::Uuid::parse("526fff92-a6c3-43c0-9b03-ae68ae4f8a73");
  game.metadata.title = "测试棋局";
  game.metadata.result = oxq::core::GameResult::red_win;
  game.initial_position.side_to_move = oxq::core::Side::red;
  game.initial_position.fullmove_number = 1;
  return game;
}

[[nodiscard]] bool has_code(const oxq::convert::ConversionReport& report,
                            oxq::convert::ConversionCode code) {
  for (const auto& diagnostic : report.diagnostics) {
    if (diagnostic.code == code) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  const std::vector games{minimal_game()};
  oxq::convert::CblWriteOptions options;
  options.library.name = "确定性测试库";

  const auto first_outcome = oxq::convert::preflight_cbl_write(games, options);
  const auto second_outcome = oxq::convert::preflight_cbl_write(games, options);
  if (!std::holds_alternative<oxq::convert::CblWritePlan>(first_outcome) ||
      !std::holds_alternative<oxq::convert::CblWritePlan>(second_outcome)) {
    return 1;
  }
  const auto& first = std::get<oxq::convert::CblWritePlan>(first_outcome);
  const auto& second = std::get<oxq::convert::CblWritePlan>(second_outcome);
  if (first.report.rejected || first.report.has_loss() ||
      first.report.source_game_count != 1 ||
      first.report.converted_game_count != 1 ||
      first.directory_capacity != 128 || first.record_sizes.size() != 1 ||
      first.record_sizes[0] != 0x8aa ||
      first.projected_file_size != 0x10440 + 128 * 0x114 + 4096 ||
      first.library_uuid != second.library_uuid ||
      !has_code(first.report,
                oxq::convert::ConversionCode::cbl_write_library_uuid_derived)) {
    return 2;
  }

  auto annotated = minimal_game();
  annotated.move_tree.nodes[0].annotations.push_back(
      {oxq::core::AnnotationKind::comment, false, "根注释", {}, {}});
  const std::vector annotated_games{annotated};
  const auto annotated_outcome =
      oxq::convert::preflight_cbl_write(annotated_games, options);
  const auto& annotated_plan =
      std::get<oxq::convert::CblWritePlan>(annotated_outcome);
  if (annotated_plan.report.has_loss() || annotated_plan.record_sizes[0] != 0x8aa + 10) {
    return 3;
  }

  auto lossy = minimal_game();
  lossy.metadata.tags = {"tag-a"};
  lossy.metadata.result = oxq::core::GameResult::unfinished;
  lossy.move_tree.nodes[0].annotations.push_back(
      {oxq::core::AnnotationKind::source_note, false, "source", "author", "zh"});
  const std::vector lossy_games{lossy};
  const auto lenient_outcome =
      oxq::convert::preflight_cbl_write(lossy_games, options);
  const auto& lenient = std::get<oxq::convert::CblWritePlan>(lenient_outcome);
  if (lenient.report.rejected || !lenient.report.has_loss() ||
      lenient.report.converted_game_count != 1 ||
      !has_code(lenient.report,
                oxq::convert::ConversionCode::cbl_write_metadata_unsupported) ||
      !has_code(lenient.report,
                oxq::convert::ConversionCode::cbl_write_result_normalized) ||
      !has_code(lenient.report,
                oxq::convert::ConversionCode::cbl_write_annotation_normalized)) {
    return 4;
  }

  options.mode = oxq::convert::ConversionMode::strict;
  const auto strict_outcome =
      oxq::convert::preflight_cbl_write(lossy_games, options);
  const auto& strict = std::get<oxq::convert::CblWritePlan>(strict_outcome);
  if (!strict.report.rejected || strict.report.converted_game_count != 0 ||
      !strict.report.has_loss()) {
    return 5;
  }

  options.mode = oxq::convert::ConversionMode::lenient;
  auto invalid = minimal_game();
  invalid.uuid = {};
  const auto invalid_outcome =
      oxq::convert::preflight_cbl_write(std::span{&invalid, 1U}, options);
  const auto& invalid_plan =
      std::get<oxq::convert::CblWritePlan>(invalid_outcome);
  if (!invalid_plan.report.rejected ||
      !has_code(invalid_plan.report,
                oxq::convert::ConversionCode::cbl_write_invalid_game_model)) {
    return 6;
  }

  auto too_long = minimal_game();
  too_long.metadata.title = std::string(64, 'x');
  const auto long_outcome =
      oxq::convert::preflight_cbl_write(std::span{&too_long, 1U}, options);
  const auto& long_plan = std::get<oxq::convert::CblWritePlan>(long_outcome);
  if (!long_plan.report.rejected ||
      !has_code(long_plan.report,
                oxq::convert::ConversionCode::cbl_write_text_too_long)) {
    return 7;
  }

  options.minimum_directory_capacity = 130;
  const std::vector two_games{minimal_game(), minimal_game()};
  const auto capacity_outcome =
      oxq::convert::preflight_cbl_write(two_games, options);
  if (std::get<oxq::convert::CblWritePlan>(capacity_outcome).directory_capacity != 130) {
    return 8;
  }

  options.limits.max_games = 1;
  if (!std::holds_alternative<oxq::convert::CblWriteError>(
          oxq::convert::preflight_cbl_write(two_games, options))) {
    return 9;
  }

  options.limits.max_games = 1'000'000;
  options.limits.max_output_bytes = 1024;
  const auto size_outcome = oxq::convert::preflight_cbl_write(games, options);
  if (!std::holds_alternative<oxq::convert::CblWriteError>(size_outcome) ||
      std::get<oxq::convert::CblWriteError>(size_outcome).code !=
          oxq::convert::CblWriteErrorCode::resource_limit) {
    return 10;
  }

  options.limits.max_output_bytes = 2U * 1024U * 1024U * 1024U;
  options.library.name = std::string{"\xc0\x80", 2};
  const auto utf8_outcome = oxq::convert::preflight_cbl_write(games, options);
  const auto& utf8_plan = std::get<oxq::convert::CblWritePlan>(utf8_outcome);
  if (!utf8_plan.report.rejected ||
      !has_code(utf8_plan.report,
                oxq::convert::ConversionCode::cbl_write_invalid_game_model)) {
    return 11;
  }

  options.library.name = "test";
  auto empty_tree = minimal_game();
  empty_tree.move_tree.nodes.clear();
  const auto tree_outcome =
      oxq::convert::preflight_cbl_write(std::span{&empty_tree, 1U}, options);
  const auto& tree_plan = std::get<oxq::convert::CblWritePlan>(tree_outcome);
  if (!tree_plan.report.rejected || tree_plan.record_sizes[0] != 0x8aa) {
    return 12;
  }
  return 0;
}
