#include <oxq/convert/cbl_writer.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] oxq::core::Position standard_position() {
  using oxq::core::Piece;
  using oxq::core::PieceType;
  using oxq::core::Side;
  oxq::core::Position position;
  position.side_to_move = Side::red;
  position.fullmove_number = 1;
  position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::red, PieceType::horse, 1},
      Piece{Side::red, PieceType::elephant, 2},
      Piece{Side::red, PieceType::advisor, 3},
      Piece{Side::red, PieceType::king, 4},
      Piece{Side::red, PieceType::advisor, 5},
      Piece{Side::red, PieceType::elephant, 6},
      Piece{Side::red, PieceType::horse, 7},
      Piece{Side::red, PieceType::rook, 8},
      Piece{Side::red, PieceType::cannon, 19},
      Piece{Side::red, PieceType::cannon, 25},
      Piece{Side::red, PieceType::pawn, 27},
      Piece{Side::red, PieceType::pawn, 29},
      Piece{Side::red, PieceType::pawn, 31},
      Piece{Side::red, PieceType::pawn, 33},
      Piece{Side::red, PieceType::pawn, 35},
      Piece{Side::black, PieceType::pawn, 54},
      Piece{Side::black, PieceType::pawn, 56},
      Piece{Side::black, PieceType::pawn, 58},
      Piece{Side::black, PieceType::pawn, 60},
      Piece{Side::black, PieceType::pawn, 62},
      Piece{Side::black, PieceType::cannon, 64},
      Piece{Side::black, PieceType::cannon, 70},
      Piece{Side::black, PieceType::rook, 81},
      Piece{Side::black, PieceType::horse, 82},
      Piece{Side::black, PieceType::elephant, 83},
      Piece{Side::black, PieceType::advisor, 84},
      Piece{Side::black, PieceType::king, 85},
      Piece{Side::black, PieceType::advisor, 86},
      Piece{Side::black, PieceType::elephant, 87},
      Piece{Side::black, PieceType::horse, 88},
      Piece{Side::black, PieceType::rook, 89},
  };
  return position;
}

[[nodiscard]] oxq::core::GameModel metadata_mainline() {
  oxq::core::GameModel game;
  game.uuid =
      *oxq::core::Uuid::parse("526fff92-a6c3-43c0-9b03-ae68ae4f8a73");
  game.metadata.title = "M5 元数据与主线";
  game.metadata.red_player.name = "红方";
  game.metadata.red_player.team = "北京";
  game.metadata.red_player.rating = 2500;
  game.metadata.black_player.name = "Black";
  game.metadata.black_player.team = "Shanghai";
  game.metadata.black_player.rating = 2490;
  game.metadata.event.name = "OXQF Writer Compatibility";
  game.metadata.event.location = "Beijing";
  game.metadata.event.round = "1";
  game.metadata.event.start_time = "2026-09-04";
  game.metadata.event.date_precision = oxq::core::DatePrecision::day;
  game.metadata.result = oxq::core::GameResult::draw;
  game.metadata.opening.code = "A00";
  game.metadata.creator = "OXQF";
  game.initial_position = standard_position();
  game.move_tree.nodes = {
      {{}, {}, {1}, {}},
      {0, oxq::core::Move{19, 22}, {2},
       {{oxq::core::AnnotationKind::comment, false, "炮二平五", {}, {}}}},
      {1, oxq::core::Move{82, 65}, {}, {}},
  };
  return game;
}

[[nodiscard]] oxq::core::GameModel custom_variations() {
  oxq::core::GameModel game;
  game.uuid =
      *oxq::core::Uuid::parse("a29ab94d-d463-4b46-8224-eb8d56abeddd");
  game.metadata.title = "M5 自定义局面、变化与注释😀";
  game.metadata.result = oxq::core::GameResult::unknown;
  game.initial_position.side_to_move = oxq::core::Side::red;
  game.initial_position.fullmove_number = 23;
  game.initial_position.pieces = {
      {oxq::core::Side::red, oxq::core::PieceType::king, 4},
      {oxq::core::Side::red, oxq::core::PieceType::rook, 18},
      {oxq::core::Side::black, oxq::core::PieceType::king, 85},
      {oxq::core::Side::black, oxq::core::PieceType::rook, 71},
  };
  game.move_tree.nodes = {
      {{}, {}, {1, 3},
       {{oxq::core::AnnotationKind::comment, false, "根注释\n第二行", {}, {}}}},
      {0, oxq::core::Move{18, 27}, {2},
       {{oxq::core::AnnotationKind::comment, false, "主变化", {}, {}}}},
      {1, oxq::core::Move{71, 62}, {}, {}},
      {0, oxq::core::Move{18, 19}, {},
       {{oxq::core::AnnotationKind::comment, false, "并列变化😀", {}, {}}}},
  };
  return game;
}

[[nodiscard]] oxq::convert::CblWriteOptions options(std::string_view name,
                                                    std::string_view uuid) {
  oxq::convert::CblWriteOptions result;
  result.mode = oxq::convert::ConversionMode::strict;
  result.library.uuid = *oxq::core::Uuid::parse(uuid);
  result.library.name = name;
  result.library.author = "Open Xiangqi Format";
  result.library.author_email = "interop@openxiangqi.org";
  result.library.created_at = "2026-09-04";
  result.library.modified_at = "2026-09-04";
  return result;
}

[[nodiscard]] bool write_vector(
    const std::filesystem::path& path,
    std::span<const oxq::core::GameModel> games,
    const oxq::convert::CblWriteOptions& write_options) {
  auto outcome = oxq::convert::write_cbl(games, write_options);
  const auto* result = std::get_if<oxq::convert::CblWriteResult>(&outcome);
  if (result == nullptr || result->report.rejected || result->report.has_loss()) {
    std::cerr << "cannot encode " << path.string() << '\n';
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(result->bytes.data()),
               static_cast<std::streamsize>(result->bytes.size()));
  if (!output) {
    std::cerr << "cannot write " << path.string() << '\n';
    return false;
  }
  std::cout << path.string() << '\n';
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: oxq_convert_cbl_writer_compatibility_vectors OUTPUT_DIR\n";
    return 2;
  }
  const std::filesystem::path output_directory{argv[1]};
  std::error_code error;
  std::filesystem::create_directories(output_directory, error);
  if (error) {
    std::cerr << "cannot create output directory: " << error.message() << '\n';
    return 3;
  }

  const std::vector<oxq::core::GameModel> empty;
  const std::vector metadata{metadata_mainline()};
  const std::vector variations{custom_variations()};
  const std::vector combined{metadata.front(), variations.front()};
  const bool empty_ok = write_vector(
      output_directory / "m5_writer_00_empty.CBL", empty,
      options("M5 empty", "112b3986-7ca7-4771-8350-9d040a8c219c"));
  const bool metadata_ok = write_vector(
      output_directory / "m5_writer_01_metadata_mainline.CBL", metadata,
      options("M5 metadata", "7b9fd0ba-0a0c-43ae-94af-2d6c5d49877a"));
  const bool variations_ok = write_vector(
      output_directory / "m5_writer_02_custom_variations.CBL", variations,
      options("M5 variations", "4f94cc93-031a-49cc-82ea-e99d6affefdc"));
  const bool combined_ok = write_vector(
      output_directory / "m5_writer_03_two_games.CBL", combined,
      options("M5 two games", "582bb547-60ce-4fcf-b578-1c38a6ac5ae9"));
  return empty_ok && metadata_ok && variations_ok && combined_ok ? 0 : 1;
}
