#include <oxq/convert/cbl_reader.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/validation.hpp>
#include <oxq/core/writer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::size_t kDirectoryOffset = 0x10440;
constexpr std::size_t kDirectoryEntrySize = 0x114;
constexpr std::size_t kFirstRecordOffset = 0x18e40;

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

void write_u32(std::vector<std::byte>& bytes, std::size_t offset,
               std::uint32_t value) {
  for (unsigned index = 0; index < 4; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void write_utf16_ascii(std::vector<std::byte>& bytes, std::size_t offset,
                       std::size_t size, std::string_view value) {
  for (std::size_t index = 0; index < size; ++index) {
    bytes[offset + index] = std::byte{0};
  }
  for (std::size_t index = 0; index < value.size(); ++index) {
    bytes[offset + index * 2] = static_cast<std::byte>(value[index]);
  }
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

[[nodiscard]] const std::string& string_extension(
    const oxq::core::GameModel& game, std::string_view key) {
  return std::get<std::string>(
      game.metadata.extensions.at("org.openxiangqi.cbl").at(std::string{key}));
}

}  // namespace

int main() {
  const std::filesystem::path baseline{OXQF_GOLD_BASELINE_DIRECTORY};
  struct Expected {
    std::string_view file;
    std::vector<std::string_view> titles;
    std::vector<std::size_t> move_nodes;
  };
  const std::array expected{
      Expected{"cbl_00_empty.CBL", {}, {}},
      Expected{"cbl_01_game_empty.CBL", {"GAME_EMPTY_01"}, {0}},
      Expected{"cbl_02_one_ply.CBL", {"GAME_TEST_02"}, {1}},
      Expected{"cbl_03_two_plies.CBL", {"GAME_TEST_03"}, {2}},
      Expected{"cbl_04_mainline.CBL", {"GAME_TEST_04"}, {8}},
      Expected{"cbl_05_variation.CBL", {"GAME_TEST_05"}, {4}},
      Expected{"cbl_06_nested_variation.CBL", {"GAME_TEST_06"}, {6}},
      Expected{"cbl_07_comments.CBL", {"GAME_TEST_07"}, {4}},
      Expected{"cbl_08_metadata.CBL", {"TITLE_01_测试棋局"}, {0}},
      Expected{"cbl_09_custom_position.CBL", {"GAME_TEST_09"}, {1}},
      Expected{"cbl_10_two_games.CBL", {"GAME_A_01", "GAME_B_02"}, {1, 1}},
      Expected{"cbl_11_nested_folders.CBL", {"GAME_A", "GAME_B"}, {0, 0}},
  };

  for (const auto& item : expected) {
    const auto outcome = oxq::convert::read_cbl(read_file(baseline / item.file));
    if (!std::holds_alternative<oxq::convert::CblReadResult>(outcome)) {
      return 1;
    }
    const auto& result = std::get<oxq::convert::CblReadResult>(outcome);
    if (result.report.rejected || result.report.has_loss() ||
        result.games.size() != item.titles.size() ||
        result.report.source_game_count != item.titles.size() ||
        result.report.converted_game_count != item.titles.size()) {
      return 2;
    }
    for (std::size_t index = 0; index < result.games.size(); ++index) {
      const auto& game = result.games[index];
      if (!game.metadata.title.has_value() ||
          *game.metadata.title != item.titles[index] ||
          game.move_tree.nodes.size() != item.move_nodes[index] + 1 ||
          oxq::core::has_errors(oxq::core::validate(game)) ||
          game.metadata.provenance.source_format != "CBL" ||
          game.metadata.provenance.source_format_version != "3" ||
          !game.metadata.provenance.source_library_id.has_value() ||
          !game.metadata.provenance.source_library_name.has_value() ||
          game.metadata.provenance.source_record_id != game.uuid.to_string() ||
          string_extension(game, "record_guid") != game.uuid.to_string()) {
        return 3;
      }
      const auto& controls = std::get<std::vector<std::string>>(
          game.metadata.extensions.at("org.openxiangqi.cbl").at("source_controls"));
      if (controls.size() != game.move_tree.nodes.size()) {
        return 4;
      }
      const auto written = oxq::core::write_oxq(game);
      if (!std::holds_alternative<std::vector<std::byte>>(written)) {
        return 21;
      }
      const auto reread = oxq::core::read_oxq(
          std::get<std::vector<std::byte>>(written));
      if (!std::holds_alternative<oxq::core::ReaderResult>(reread) ||
          std::get<oxq::core::ReaderResult>(reread).game != game) {
        return 22;
      }
    }
  }

  const auto metadata_outcome = oxq::convert::read_cbl(
      read_file(baseline / "cbl_08_metadata.CBL"));
  const auto& metadata =
      std::get<oxq::convert::CblReadResult>(metadata_outcome).games[0].metadata;
  if (metadata.red_player.name != "RED_02_红棋手" ||
      metadata.black_player.name != "BLACK_03_黑棋手" ||
      metadata.red_player.rating != 2101 || metadata.black_player.rating != 2202 ||
      metadata.event.name != "EVENT_04_测试赛" ||
      metadata.event.location != "LOCATION_05_上海" ||
      metadata.event.round != "ROUND_06" || metadata.event.group != "GROUP_09" ||
      metadata.event.board_number != "BOARD_10" ||
      metadata.event.start_time != "2024-02-03" ||
      metadata.event.date_precision != oxq::core::DatePrecision::day ||
      metadata.result != oxq::core::GameResult::red_win ||
      metadata.referee != "ARBITER_08" ||
      metadata.provenance.source_uri != "SOURCE_11" ||
      metadata.opening.name.has_value() || metadata.opening.code.has_value()) {
    return 5;
  }

  const auto custom_outcome = oxq::convert::read_cbl(
      read_file(baseline / "cbl_09_custom_position.CBL"));
  const auto& custom =
      std::get<oxq::convert::CblReadResult>(custom_outcome).games[0];
  if (custom.initial_position.pieces.size() != 3 ||
      custom.move_tree.nodes[1].move != oxq::core::Move{31, 30}) {
    return 6;
  }

  const auto comments_outcome = oxq::convert::read_cbl(
      read_file(baseline / "cbl_07_comments.CBL"));
  const auto& comments =
      std::get<oxq::convert::CblReadResult>(comments_outcome).games[0].move_tree.nodes;
  if (comments[0].annotations[0].text != "COMMENT_ROOT_甲" ||
      comments[4].annotations[0].text !=
          "COMMENT_MULTILINE_戊\n第二行 ABC 123\n第三行：标点，。！？") {
    return 7;
  }

  auto reordered = read_file(baseline / "cbl_10_two_games.CBL");
  write_u32(reordered, kDirectoryOffset + 4, 2);
  write_u32(reordered, kDirectoryOffset + kDirectoryEntrySize + 4, 1);
  const auto reordered_outcome = oxq::convert::read_cbl(reordered);
  const auto& reordered_games =
      std::get<oxq::convert::CblReadResult>(reordered_outcome).games;
  if (reordered_games[0].metadata.title != "GAME_B_02" ||
      reordered_games[1].metadata.title != "GAME_A_01") {
    return 8;
  }

  auto duplicate_index = read_file(baseline / "cbl_10_two_games.CBL");
  write_u32(duplicate_index, kDirectoryOffset + 4, 0);
  write_u32(duplicate_index, kDirectoryOffset + kDirectoryEntrySize + 4, 0);
  const auto duplicate_outcome = oxq::convert::read_cbl(duplicate_index);
  const auto& duplicate =
      std::get<oxq::convert::CblReadResult>(duplicate_outcome);
  if (duplicate.games[0].metadata.title != "GAME_A_01" ||
      duplicate.games[1].metadata.title != "GAME_B_02" ||
      !has_code(duplicate.report,
                oxq::convert::ConversionCode::cbl_duplicate_display_index)) {
    return 14;
  }

  const auto folders_outcome = oxq::convert::read_cbl(
      read_file(baseline / "cbl_11_nested_folders.CBL"));
  const auto& folders =
      std::get<oxq::convert::CblReadResult>(folders_outcome).games;
  if ((!folders[0].metadata.provenance.source_category.has_value() ||
       *folders[0].metadata.provenance.source_category != "FOLDER_B_CHILD") &&
      (!folders[1].metadata.provenance.source_category.has_value() ||
       *folders[1].metadata.provenance.source_category != "FOLDER_B_CHILD")) {
    return 15;
  }

  const auto original_outcome = oxq::convert::read_cbl(
      read_file(baseline / "cbl_01_game_empty.CBL"));
  const auto original_uuid =
      std::get<oxq::convert::CblReadResult>(original_outcome).games[0].uuid;

  auto uuid_mismatch = read_file(baseline / "cbl_01_game_empty.CBL");
  uuid_mismatch[kDirectoryOffset + 0x16] ^= std::byte{1};
  const auto mismatch_outcome = oxq::convert::read_cbl(uuid_mismatch);
  const auto& mismatch = std::get<oxq::convert::CblReadResult>(mismatch_outcome);
  if (mismatch.games[0].uuid != original_uuid ||
      !has_code(mismatch.report, oxq::convert::ConversionCode::cbl_uuid_mismatch)) {
    return 9;
  }

  auto directory_invalid = read_file(baseline / "cbl_01_game_empty.CBL");
  directory_invalid[kDirectoryOffset + 0x14] = std::byte{0};
  directory_invalid[kDirectoryOffset + 0x15] = std::byte{0};
  const auto directory_invalid_outcome = oxq::convert::read_cbl(directory_invalid);
  const auto& directory_fallback =
      std::get<oxq::convert::CblReadResult>(directory_invalid_outcome);
  if (directory_fallback.games[0].uuid != original_uuid ||
      !has_code(directory_fallback.report,
                oxq::convert::ConversionCode::cbl_directory_uuid_invalid)) {
    return 16;
  }

  auto record_invalid = read_file(baseline / "cbl_01_game_empty.CBL");
  for (std::size_t index = 0; index < 16; ++index) {
    record_invalid[kFirstRecordOffset + 0x14 + index] = std::byte{0};
  }
  const auto fallback_outcome = oxq::convert::read_cbl(record_invalid);
  const auto& fallback = std::get<oxq::convert::CblReadResult>(fallback_outcome);
  if (fallback.games[0].uuid != original_uuid ||
      !has_code(fallback.report,
                oxq::convert::ConversionCode::cbl_record_uuid_invalid)) {
    return 10;
  }

  record_invalid[kDirectoryOffset + 0x14] = std::byte{0};
  record_invalid[kDirectoryOffset + 0x15] = std::byte{0};
  const auto derived_outcome = oxq::convert::read_cbl(record_invalid);
  const auto& derived = std::get<oxq::convert::CblReadResult>(derived_outcome);
  if (derived.games[0].uuid.to_string() !=
          "35b17297-6f89-5adc-965a-0f9324ce1805" ||
      !has_code(derived.report, oxq::convert::ConversionCode::cbl_uuid_derived) ||
      derived.games[0].metadata.provenance.source_record_id !=
          "35b17297-6f89-5adc-965a-0f9324ce1805") {
    return 11;
  }

  auto normalized = read_file(baseline / "cbl_01_game_empty.CBL");
  write_u32(normalized, kFirstRecordOffset + 0x844, 0);
  const auto lenient_outcome = oxq::convert::read_cbl(normalized);
  const auto& lenient = std::get<oxq::convert::CblReadResult>(lenient_outcome);
  if (lenient.report.rejected || !lenient.report.has_loss() ||
      lenient.games[0].initial_position.fullmove_number != 1 ||
      string_extension(lenient.games[0], "source_fullmove_number") != "0") {
    return 12;
  }
  oxq::convert::CblReadOptions strict_options;
  strict_options.mode = oxq::convert::ConversionMode::strict;
  const auto strict_outcome = oxq::convert::read_cbl(normalized, strict_options);
  const auto& strict = std::get<oxq::convert::CblReadResult>(strict_outcome);
  if (!strict.report.rejected || !strict.report.has_loss() ||
      strict.report.source_game_count != 1 || strict.report.converted_game_count != 0 ||
      !strict.games.empty()) {
    return 13;
  }

  auto multiple_result = read_file(baseline / "cbl_01_game_empty.CBL");
  write_u32(multiple_result, kFirstRecordOffset + 0x81c, 4);
  const auto multiple_outcome = oxq::convert::read_cbl(multiple_result);
  const auto& multiple =
      std::get<oxq::convert::CblReadResult>(multiple_outcome);
  if (!multiple.report.has_loss() ||
      multiple.games[0].metadata.result != oxq::core::GameResult::unknown ||
      string_extension(multiple.games[0], "result") != "4" ||
      !has_code(multiple.report,
                oxq::convert::ConversionCode::cbl_multiple_result)) {
    return 17;
  }
  const auto multiple_strict_outcome =
      oxq::convert::read_cbl(multiple_result, strict_options);
  const auto& multiple_strict =
      std::get<oxq::convert::CblReadResult>(multiple_strict_outcome);
  if (!multiple_strict.report.rejected || !multiple_strict.games.empty()) {
    return 18;
  }

  auto invalid_metadata = read_file(baseline / "cbl_08_metadata.CBL");
  write_utf16_ascii(invalid_metadata, kFirstRecordOffset + 0x374, 0x40,
                    "2024-02-30");
  write_utf16_ascii(invalid_metadata, kFirstRecordOffset + 0x4f4, 0x20,
                    "21x");
  write_u32(invalid_metadata, kFirstRecordOffset + 0x7f8, 9);
  const auto invalid_metadata_outcome = oxq::convert::read_cbl(invalid_metadata);
  const auto& invalid_metadata_result =
      std::get<oxq::convert::CblReadResult>(invalid_metadata_outcome);
  const auto& invalid_game = invalid_metadata_result.games[0];
  if (invalid_game.metadata.event.start_time.has_value() ||
      invalid_game.metadata.event.date_precision.has_value() ||
      invalid_game.metadata.red_player.rating.has_value() ||
      string_extension(invalid_game, "date_raw") != "2024-02-30" ||
      string_extension(invalid_game, "red_rating_raw") != "21x" ||
      !has_code(invalid_metadata_result.report,
                oxq::convert::ConversionCode::cbl_invalid_date) ||
      !has_code(invalid_metadata_result.report,
                oxq::convert::ConversionCode::cbl_invalid_rating) ||
      !has_code(invalid_metadata_result.report,
                oxq::convert::ConversionCode::cbl_unknown_record_type) ||
      invalid_metadata_result.report.has_loss() ||
      oxq::core::has_errors(oxq::core::validate(invalid_game))) {
    return 19;
  }

  auto invalid_magic = read_file(baseline / "cbl_00_empty.CBL");
  invalid_magic[0] = std::byte{0};
  const auto invalid_magic_outcome = oxq::convert::read_cbl(invalid_magic);
  if (!std::holds_alternative<oxq::convert::CblError>(invalid_magic_outcome) ||
      std::get<oxq::convert::CblError>(invalid_magic_outcome).code !=
          oxq::convert::CblErrorCode::invalid_magic) {
    return 20;
  }

  return 0;
}
