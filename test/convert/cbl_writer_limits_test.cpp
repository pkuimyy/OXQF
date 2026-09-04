#include <oxq/convert/cbl_reader.hpp>
#include <oxq/convert/cbl_writer.hpp>

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] oxq::core::GameModel annotated_game(std::size_t characters) {
  oxq::core::GameModel game;
  game.uuid =
      *oxq::core::Uuid::parse("526fff92-a6c3-43c0-9b03-ae68ae4f8a73");
  game.metadata.result = oxq::core::GameResult::unknown;
  game.initial_position.fullmove_number = 1;
  if (characters != 0) {
    game.move_tree.nodes[0].annotations.push_back(
        {oxq::core::AnnotationKind::comment, false,
         std::string(characters, 'x'), {}, {}});
  }
  return game;
}

[[nodiscard]] oxq::convert::CblWriteOptions options() {
  oxq::convert::CblWriteOptions result;
  result.mode = oxq::convert::ConversionMode::strict;
  result.library.uuid =
      *oxq::core::Uuid::parse("4c5415f9-e3e7-4df6-8614-19ac28832ad1");
  return result;
}

[[nodiscard]] bool error_code(const oxq::convert::CblWriteOutcome& outcome,
                              oxq::convert::CblWriteErrorCode code) {
  const auto* error = std::get_if<oxq::convert::CblWriteError>(&outcome);
  return error != nullptr && error->code == code;
}

[[nodiscard]] const oxq::convert::CblWriteResult* result(
    const oxq::convert::CblWriteOutcome& outcome) {
  return std::get_if<oxq::convert::CblWriteResult>(&outcome);
}

}  // namespace

int main() {
  // 0x8aa + 4-byte comment length + 937 UTF-16 code units = 4096.
  const auto exact_block = annotated_game(937);
  auto write_options = options();
  const auto exact_outcome = oxq::convert::write_cbl(
      std::span<const oxq::core::GameModel>{&exact_block, 1}, write_options);
  const auto* exact = result(exact_outcome);
  if (exact == nullptr || exact->report.rejected ||
      exact->bytes.size() != 0x10440 + 128 * 0x114 + 4096) {
    return 1;
  }
  const auto exact_inspect = oxq::convert::inspect_cbl(exact->bytes);
  if (!std::holds_alternative<oxq::convert::CblLibraryInfo>(exact_inspect) ||
      std::get<oxq::convert::CblLibraryInfo>(exact_inspect).total_blocks != 1) {
    return 2;
  }

  const auto over_block = annotated_game(938);
  const auto over_outcome = oxq::convert::write_cbl(
      std::span<const oxq::core::GameModel>{&over_block, 1}, write_options);
  const auto* over = result(over_outcome);
  if (over == nullptr || over->report.rejected ||
      over->bytes.size() != 0x10440 + 128 * 0x114 + 8192) {
    return 3;
  }
  const auto over_inspect = oxq::convert::inspect_cbl(over->bytes);
  if (!std::holds_alternative<oxq::convert::CblLibraryInfo>(over_inspect) ||
      std::get<oxq::convert::CblLibraryInfo>(over_inspect).total_blocks != 2) {
    return 4;
  }

  write_options.limits.max_output_bytes = exact->bytes.size();
  if (result(oxq::convert::write_cbl(
          std::span<const oxq::core::GameModel>{&exact_block, 1},
          write_options)) == nullptr) {
    return 5;
  }
  --write_options.limits.max_output_bytes;
  if (!error_code(oxq::convert::write_cbl(
                      std::span<const oxq::core::GameModel>{&exact_block, 1},
                      write_options),
                  oxq::convert::CblWriteErrorCode::resource_limit)) {
    return 6;
  }

  write_options = options();
  write_options.limits.max_games = 0;
  if (!error_code(oxq::convert::write_cbl(
                      std::span<const oxq::core::GameModel>{&exact_block, 1},
                      write_options),
                  oxq::convert::CblWriteErrorCode::too_many_games)) {
    return 7;
  }
  write_options = options();
  write_options.limits.max_directory_entries = 127;
  const std::vector<oxq::core::GameModel> empty;
  if (!error_code(oxq::convert::write_cbl(empty, write_options),
                  oxq::convert::CblWriteErrorCode::resource_limit)) {
    return 8;
  }

  write_options = options();
  write_options.limits.max_record_bytes = 4095;
  const auto record_limited = oxq::convert::write_cbl(
      std::span<const oxq::core::GameModel>{&exact_block, 1}, write_options);
  if (result(record_limited) == nullptr ||
      !result(record_limited)->report.rejected ||
      !result(record_limited)->bytes.empty()) {
    return 9;
  }
  write_options = options();
  write_options.limits.max_comment_bytes = 1873;
  const auto comment_limited = oxq::convert::write_cbl(
      std::span<const oxq::core::GameModel>{&exact_block, 1}, write_options);
  if (result(comment_limited) == nullptr ||
      !result(comment_limited)->report.rejected ||
      !result(comment_limited)->bytes.empty()) {
    return 10;
  }

  if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t)) {
    write_options = options();
    write_options.minimum_directory_capacity =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U;
    write_options.limits.max_directory_entries =
        write_options.minimum_directory_capacity;
    if (!error_code(oxq::convert::write_cbl(empty, write_options),
                    oxq::convert::CblWriteErrorCode::resource_limit)) {
      return 11;
    }
  }
  return 0;
}
