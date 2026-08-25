#include "cbl/container.hpp"
#include "cbl/record.hpp"

#include <oxq/convert/cbl_reader.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

namespace {

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

[[nodiscard]] const oxq::convert::detail::CblDirectoryEntryView* first_game(
    const oxq::convert::detail::CblContainerView& container) {
  for (const auto& entry : container.entries) {
    if (entry.kind == oxq::convert::detail::CblResourceKind::live_game) {
      return &entry;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  const std::filesystem::path baseline{OXQF_GOLD_BASELINE_DIRECTORY};
  struct Expected {
    std::string_view file;
    std::size_t used_size;
    std::size_t pieces;
  };
  constexpr std::array expected{
      Expected{"cbl_01_game_empty.CBL", 2218, 32},
      Expected{"cbl_02_one_ply.CBL", 2222, 32},
      Expected{"cbl_03_two_plies.CBL", 2226, 32},
      Expected{"cbl_04_mainline.CBL", 2250, 32},
      Expected{"cbl_05_variation.CBL", 2234, 32},
      Expected{"cbl_06_nested_variation.CBL", 2242, 32},
      Expected{"cbl_07_comments.CBL", 2464, 32},
      Expected{"cbl_08_metadata.CBL", 2218, 32},
      Expected{"cbl_09_custom_position.CBL", 2222, 3},
  };
  for (const auto& item : expected) {
    const auto bytes = read_file(baseline / item.file);
    const auto inspected = oxq::convert::detail::inspect_cbl_container(bytes);
    if (!std::holds_alternative<oxq::convert::detail::CblContainerView>(inspected)) {
      return 1;
    }
    const auto& container =
        std::get<oxq::convert::detail::CblContainerView>(inspected);
    const auto* entry = first_game(container);
    if (entry == nullptr) {
      return 2;
    }
    const auto record = oxq::convert::detail::read_cbl_record(container, *entry);
    if (!std::holds_alternative<oxq::convert::detail::CblRecordView>(record)) {
      const auto& failure = std::get<oxq::convert::CblError>(record);
      std::cerr << item.file << ": " << failure.field << " @ " << failure.offset
                << ": " << failure.message << '\n';
      return 3;
    }
    const auto& value = std::get<oxq::convert::detail::CblRecordView>(record);
    if (value.used_size != item.used_size || value.guid.is_nil() ||
        value.metadata.name.empty() || value.metadata.name != entry->title ||
        value.position.side_to_move != oxq::core::Side::red ||
        value.position.pieces.size() != item.pieces || value.node_stream_offset != 0x8aa) {
      return 4;
    }
  }

  const auto metadata_bytes = read_file(baseline / "cbl_08_metadata.CBL");
  const auto metadata_container_result =
      oxq::convert::detail::inspect_cbl_container(metadata_bytes);
  const auto& metadata_container =
      std::get<oxq::convert::detail::CblContainerView>(metadata_container_result);
  const auto metadata_record = oxq::convert::detail::read_cbl_record(
      metadata_container, *first_game(metadata_container));
  const auto& metadata =
      std::get<oxq::convert::detail::CblRecordView>(metadata_record).metadata;
  if (metadata.name != "TITLE_01_测试棋局" || metadata.red != "RED_02_红棋手" ||
      metadata.black != "BLACK_03_黑棋手" || metadata.contest != "EVENT_04_测试赛" ||
      metadata.site != "LOCATION_05_上海" || metadata.round != "ROUND_06" ||
      metadata.date != "2024-02-03" || metadata.result != 1 ||
      metadata.referee != "ARBITER_08" || metadata.group != "GROUP_09" ||
      metadata.table != "BOARD_10" || metadata.source != "SOURCE_11" ||
      metadata.red_rating != "2101" || metadata.black_rating != "2202" ||
      !metadata.ecco_code.empty()) {
    return 5;
  }

  const auto custom_bytes = read_file(baseline / "cbl_09_custom_position.CBL");
  const auto custom_container_result =
      oxq::convert::detail::inspect_cbl_container(custom_bytes);
  const auto& custom_container =
      std::get<oxq::convert::detail::CblContainerView>(custom_container_result);
  const auto custom_record = oxq::convert::detail::read_cbl_record(
      custom_container, *first_game(custom_container));
  const auto& custom = std::get<oxq::convert::detail::CblRecordView>(custom_record);
  if (custom.position.pieces.size() != 3 || custom.source_fullmove_number != 1 ||
      custom.position.pieces[0].square != 4 || custom.position.pieces[1].square != 31 ||
      custom.position.pieces[2].square != 84 ||
      custom.position.pieces[0].side != oxq::core::Side::red ||
      custom.position.pieces[0].type != oxq::core::PieceType::king ||
      custom.position.pieces[1].side != oxq::core::Side::red ||
      custom.position.pieces[1].type != oxq::core::PieceType::rook ||
      custom.position.pieces[2].side != oxq::core::Side::black ||
      custom.position.pieces[2].type != oxq::core::PieceType::king) {
    return 6;
  }

  auto invalid_version = read_file(baseline / "cbl_01_game_empty.CBL");
  const auto original_container_result =
      oxq::convert::detail::inspect_cbl_container(invalid_version);
  const auto& original_container =
      std::get<oxq::convert::detail::CblContainerView>(original_container_result);
  const auto record_offset = first_game(original_container)->resource_offset;
  invalid_version[record_offset + 0x13] = std::byte{3};
  const auto invalid_container_result =
      oxq::convert::detail::inspect_cbl_container(invalid_version);
  const auto& invalid_container =
      std::get<oxq::convert::detail::CblContainerView>(invalid_container_result);
  const auto invalid_record = oxq::convert::detail::read_cbl_record(
      invalid_container, *first_game(invalid_container));
  if (!std::holds_alternative<oxq::convert::CblError>(invalid_record) ||
      std::get<oxq::convert::CblError>(invalid_record).code !=
          oxq::convert::CblErrorCode::unsupported_version ||
      std::get<oxq::convert::CblError>(invalid_record).offset != record_offset + 0x10) {
    return 7;
  }
  return 0;
}
