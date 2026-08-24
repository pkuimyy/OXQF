#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/writer.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
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

[[nodiscard]] bool exact_rewrite(const std::vector<std::byte>& source) {
  const auto read = oxq::core::read_oxq(source);
  if (!std::holds_alternative<oxq::core::ReaderResult>(read)) {
    return false;
  }
  const auto written = oxq::core::write_oxq(std::get<oxq::core::ReaderResult>(read).game);
  return std::holds_alternative<std::vector<std::byte>>(written) &&
         std::get<std::vector<std::byte>>(written) == source;
}

}  // namespace

int main() {
  const std::filesystem::path vectors{OXQF_VECTOR_DIRECTORY};
  const auto minimal_bytes = read_file(vectors / "minimal.oxq");
  const auto variation_bytes = read_file(vectors / "variation-zh.oxq");
  if (!exact_rewrite(minimal_bytes) || !exact_rewrite(variation_bytes)) {
    return 1;
  }

  const auto variation_read = oxq::core::read_oxq(variation_bytes);
  if (!std::holds_alternative<oxq::core::ReaderResult>(variation_read)) {
    return 2;
  }
  auto noncanonical = std::get<oxq::core::ReaderResult>(variation_read).game;
  std::ranges::reverse(noncanonical.initial_position.pieces);
  const auto original_nodes = noncanonical.move_tree.nodes;
  noncanonical.move_tree.nodes = {
      original_nodes[0], original_nodes[3], original_nodes[1], original_nodes[2]};
  noncanonical.move_tree.nodes[0].children = {2, 1};
  noncanonical.move_tree.nodes[1].parent = 0;
  noncanonical.move_tree.nodes[2].parent = 0;
  noncanonical.move_tree.nodes[2].children = {3};
  noncanonical.move_tree.nodes[3].parent = 2;
  const auto normalized = oxq::core::write_oxq(noncanonical);
  if (!std::holds_alternative<std::vector<std::byte>>(normalized) ||
      std::get<std::vector<std::byte>>(normalized) != variation_bytes) {
    return 3;
  }

  oxq::core::GameModel invalid;
  const auto invalid_result = oxq::core::write_oxq(invalid);
  if (!std::holds_alternative<oxq::core::WriterError>(invalid_result) ||
      std::get<oxq::core::WriterError>(invalid_result).code !=
          oxq::core::WriterErrorCode::invalid_model ||
      std::get<oxq::core::WriterError>(invalid_result).validation_issues.empty()) {
    return 4;
  }

  const auto unknown_read =
      oxq::core::read_oxq(read_file(vectors / "unknown-noncritical.oxq"));
  if (!std::holds_alternative<oxq::core::ReaderResult>(unknown_read) ||
      std::get<oxq::core::ReaderResult>(unknown_read).diagnostics.skipped_unknown_sections != 1) {
    return 5;
  }
  const auto without_unknown =
      oxq::core::write_oxq(std::get<oxq::core::ReaderResult>(unknown_read).game);
  if (!std::holds_alternative<std::vector<std::byte>>(without_unknown) ||
      !std::holds_alternative<oxq::core::ReaderResult>(
          oxq::core::read_oxq(std::get<std::vector<std::byte>>(without_unknown))) ||
      std::get<std::vector<std::byte>>(without_unknown) ==
          read_file(vectors / "unknown-noncritical.oxq")) {
    return 6;
  }

  auto full = std::get<oxq::core::ReaderResult>(
                  oxq::core::read_oxq(minimal_bytes)).game;
  full.uuid = *oxq::core::Uuid::parse("01980000-0000-7000-8000-000000000099");
  full.metadata.red_player = {"红方", "red-id", "CN", 2101, "大师", "红队", "01:02"};
  full.metadata.black_player = {"黑方", "black-id", "SG", -1, "棋手", "黑队", "02:03"};
  full.metadata.event.name = "赛事";
  full.metadata.event.id = "event-id";
  full.metadata.event.location = "上海";
  full.metadata.event.organizer = "主办方";
  full.metadata.event.round = "12.3";
  full.metadata.event.type = "公开赛";
  full.metadata.event.group = "A组";
  full.metadata.event.board_number = "8";
  full.metadata.event.time_control = "10+5";
  full.metadata.event.start_time = "2024-02-29T12:34:56Z";
  full.metadata.event.end_time = "2024-02-29T13:34:56Z";
  full.metadata.event.date_precision = oxq::core::DatePrecision::second;
  full.metadata.result = oxq::core::GameResult::draw;
  full.metadata.result_text = "和棋";
  full.metadata.opening = {"中炮", "C00", "opening-id"};
  full.metadata.title = "完整字段";
  full.metadata.tags = {"待复核", "研究"};
  full.metadata.game_type = "实战";
  full.metadata.referee = "裁判";
  full.metadata.recorder = "记录者";
  full.metadata.commentator = "解说者";
  full.metadata.commentator_uri = "https://example.com/commentator";
  full.metadata.creator = "创建者";
  full.metadata.creator_uri = "https://example.com/creator";
  full.metadata.record_created_at = "source-created";
  full.metadata.record_modified_at = "source-modified";
  full.metadata.provenance.source_format = "CBL";
  full.metadata.provenance.source_record_id = "record-id";
  full.metadata.provenance.source_uri = "library/example.CBL";
  full.metadata.provenance.import_note = "已导入";
  full.metadata.provenance.source_format_version = "3";
  full.metadata.provenance.source_library_id = "library-id";
  full.metadata.provenance.source_library_name = "棋库";
  full.metadata.provenance.source_category = "分类";
  full.metadata.extensions["org.openxiangqi.cbl"]["display_index"] = std::string{"12"};
  full.metadata.extensions["org.openxiangqi.cbl"]["labels"] =
      std::vector<std::string>{"甲", "乙", "甲"};
  full.move_tree.nodes[0].annotations.push_back(
      {oxq::core::AnnotationKind::source_note, false, "来源注释", "作者", "zh-Hans"});

  const auto full_written = oxq::core::write_oxq(full);
  if (!std::holds_alternative<std::vector<std::byte>>(full_written)) {
    return 7;
  }
  const auto full_read =
      oxq::core::read_oxq(std::get<std::vector<std::byte>>(full_written));
  if (!std::holds_alternative<oxq::core::ReaderResult>(full_read)) {
    return 8;
  }
  const auto& roundtrip = std::get<oxq::core::ReaderResult>(full_read).game;
  if (roundtrip.uuid != full.uuid) {
    return 10;
  }
  if (roundtrip.metadata != full.metadata) {
    return 12;
  }
  if (roundtrip.initial_position != full.initial_position) {
    return 13;
  }
  if (roundtrip.move_tree != full.move_tree) {
    return 14;
  }
  if (!std::get<oxq::core::ReaderResult>(full_read).diagnostics.canonical_ordering()) {
    return 11;
  }
  const auto full_rewritten =
      oxq::core::write_oxq(std::get<oxq::core::ReaderResult>(full_read).game);
  if (!std::holds_alternative<std::vector<std::byte>>(full_rewritten) ||
      std::get<std::vector<std::byte>>(full_rewritten) !=
          std::get<std::vector<std::byte>>(full_written)) {
    return 15;
  }

  oxq::core::WriterLimits limits;
  limits.max_extended_metadata_bytes = 4;
  const auto limited = oxq::core::write_oxq(full, limits);
  if (!std::holds_alternative<oxq::core::WriterError>(limited) ||
      std::get<oxq::core::WriterError>(limited).code !=
          oxq::core::WriterErrorCode::resource_limit) {
    return 9;
  }
  return 0;
}
