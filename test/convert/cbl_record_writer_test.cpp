#include "cbl/container.hpp"
#include "cbl/record.hpp"
#include "cbl/record_writer.hpp"

#include <oxq/convert/cbl_writer.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] oxq::core::GameModel rich_game() {
  oxq::core::GameModel game;
  game.uuid = *oxq::core::Uuid::parse("526fff92-a6c3-43c0-9b03-ae68ae4f8a73");
  auto& metadata = game.metadata;
  metadata.title = "标题_😀";
  metadata.provenance.source_category = "分类/子类";
  metadata.provenance.source_uri = "来源";
  metadata.event.type = "慢棋";
  metadata.event.name = "测试赛";
  metadata.event.round = "第3轮";
  metadata.event.group = "A组";
  metadata.event.board_number = "12";
  metadata.event.start_time = "2026-08-25";
  metadata.event.date_precision = oxq::core::DatePrecision::day;
  metadata.event.location = "上海";
  metadata.event.time_control = "60+30";
  metadata.red_player.name = "红方";
  metadata.red_player.team = "红队";
  metadata.red_player.time_used = "00:50";
  metadata.red_player.rating = 2101;
  metadata.black_player.name = "黑方";
  metadata.black_player.team = "黑队";
  metadata.black_player.time_used = "00:58";
  metadata.black_player.rating = 2202;
  metadata.referee = "裁判";
  metadata.recorder = "记录者";
  metadata.commentator = "解说者";
  metadata.commentator_uri = "解说URI";
  metadata.creator = "创建者";
  metadata.creator_uri = "创建URI";
  metadata.record_created_at = "2026-08-25 10:00:00";
  metadata.record_modified_at = "2026-08-25 11:00:00";
  metadata.opening.code = "A45";
  metadata.game_type = "实战中局";
  metadata.result = oxq::core::GameResult::unknown;
  metadata.result_text = "多重结果";
  metadata.extensions["org.openxiangqi.cbl"]["record_type"] = std::string{"3"};
  metadata.extensions["org.openxiangqi.cbl"]["result"] = std::string{"4"};
  metadata.extensions["org.openxiangqi.cbl"]["source_fullmove_number"] =
      std::string{"0"};
  metadata.extensions["org.openxiangqi.cbl"]["root_marker"] =
      std::string{"1234abcd"};
  metadata.extensions["org.openxiangqi.cbl"]["source_controls"] =
      std::vector<std::string>{"0300"};
  metadata.extensions["org.openxiangqi.cbl"]["record_kind"] =
      std::string{"实战中局"};
  metadata.extensions["org.openxiangqi.cbl"]["result_text"] =
      std::string{"多重结果"};

  game.initial_position.side_to_move = oxq::core::Side::black;
  game.initial_position.fullmove_number = 1;
  game.initial_position.pieces = {
      {oxq::core::Side::red, oxq::core::PieceType::king, 4},
      {oxq::core::Side::black, oxq::core::PieceType::cannon, 31},
      {oxq::core::Side::black, oxq::core::PieceType::king, 84},
  };
  game.move_tree.nodes[0].annotations.push_back(
      {oxq::core::AnnotationKind::comment, false, "根注释", {}, {}});
  return game;
}

[[nodiscard]] oxq::convert::detail::CblRecordOutcome decode_prefix(
    const std::vector<std::byte>& bytes) {
  oxq::convert::detail::CblContainerView container;
  container.input = bytes;
  oxq::convert::detail::CblDirectoryEntryView entry;
  entry.physical_slot = 7;
  entry.used_size = static_cast<std::uint32_t>(bytes.size());
  entry.allocated_size = bytes.size();
  entry.kind = oxq::convert::detail::CblResourceKind::live_game;
  return oxq::convert::detail::read_cbl_record(container, entry);
}

}  // namespace

int main() {
  const auto game = rich_game();
  oxq::convert::CblWriteOptions options;
  options.mode = oxq::convert::ConversionMode::strict;
  options.library.uuid =
      *oxq::core::Uuid::parse("4c5415f9-e3e7-4df6-8614-19ac28832ad1");
  const auto preflight = oxq::convert::preflight_cbl_write(
      std::span<const oxq::core::GameModel>{&game, 1}, options);
  if (!std::holds_alternative<oxq::convert::CblWritePlan>(preflight) ||
      std::get<oxq::convert::CblWritePlan>(preflight).report.rejected) {
    return 1;
  }

  const auto first = oxq::convert::detail::encode_cbl_record_prefix(game);
  const auto second = oxq::convert::detail::encode_cbl_record_prefix(game);
  if (first != second || first.size() != 0x8aa) {
    return 2;
  }
  if (!std::ranges::all_of(first.begin() + 0x24, first.begin() + 0x0b4,
                           [](const std::byte byte) { return byte == std::byte{0}; }) ||
      first[0x8a8] != std::byte{0} || first[0x8a9] != std::byte{0}) {
    return 3;
  }

  const auto decoded = decode_prefix(first);
  if (!std::holds_alternative<oxq::convert::detail::CblRecordView>(decoded)) {
    return 4;
  }
  const auto& record = std::get<oxq::convert::detail::CblRecordView>(decoded);
  const auto& metadata = record.metadata;
  if (record.guid != game.uuid || metadata.name != "标题_😀" ||
      metadata.url_or_category != "分类/子类" || metadata.source != "来源" ||
      metadata.contest_type != "慢棋" || metadata.contest != "测试赛" ||
      metadata.round != "第3轮" || metadata.group != "A组" ||
      metadata.table != "12" || metadata.date != "2026-08-25" ||
      metadata.site != "上海" || metadata.time_rule != "60+30" ||
      metadata.red != "红方" || metadata.red_team != "红队" ||
      metadata.red_time != "00:50" || metadata.red_rating != "2101" ||
      metadata.black != "黑方" || metadata.black_team != "黑队" ||
      metadata.black_time != "00:58" || metadata.black_rating != "2202" ||
      metadata.referee != "裁判" || metadata.recorder != "记录者" ||
      metadata.commentator != "解说者" || metadata.commentator_uri != "解说URI" ||
      metadata.creator != "创建者" || metadata.creator_uri != "创建URI" ||
      metadata.created_at != "2026-08-25 10:00:00" ||
      metadata.modified_at != "2026-08-25 11:00:00" ||
      metadata.ecco_code != "A45" || metadata.record_type != 3 ||
      metadata.record_kind != "实战中局" || metadata.result != 4 ||
      metadata.result_text != "多重结果") {
    return 5;
  }
  if (record.position.side_to_move != oxq::core::Side::black ||
      record.source_fullmove_number != 0 || record.position.fullmove_number != 1 ||
      record.position.pieces != game.initial_position.pieces ||
      record.root_marker != 0x1234abcdU || record.root_control != 0x0305U ||
      record.node_stream_offset != 0x8aa || record.used_size != 0x8aa) {
    return 6;
  }

  auto defaults = rich_game();
  defaults.metadata.extensions.clear();
  defaults.metadata.result = oxq::core::GameResult::black_win;
  defaults.initial_position.fullmove_number = 42;
  defaults.move_tree.nodes[0].annotations.clear();
  const auto defaults_decoded = decode_prefix(
      oxq::convert::detail::encode_cbl_record_prefix(defaults));
  const auto& defaults_record =
      std::get<oxq::convert::detail::CblRecordView>(defaults_decoded);
  if (defaults_record.metadata.record_type != 0 ||
      defaults_record.metadata.result != 2 ||
      defaults_record.source_fullmove_number != 42 ||
      defaults_record.root_marker != 0xffffffffU ||
      defaults_record.root_control != 0x0001U) {
    return 7;
  }
  return 0;
}
