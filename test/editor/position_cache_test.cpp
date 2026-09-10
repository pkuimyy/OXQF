#include <oxq/editor/command.hpp>
#include <oxq/editor/editor_session.hpp>

#include <algorithm>
#include <variant>

namespace {

using oxq::editor::CommandResult;
using oxq::editor::DeleteSubtreeCommand;
using oxq::editor::EditorError;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::editor::ReplaceMoveCommand;
using oxq::editor::SessionOptions;
using oxq::editor::SetAnnotationsCommand;
using oxq::format::Annotation;
using oxq::format::GameDocument;
using oxq::format::Move;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Position;
using oxq::format::Side;

[[nodiscard]] GameDocument branched_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000039");
  document.initial_position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::black, PieceType::rook, 89},
  };
  const auto first = document.move_tree.addNode(0, Move{0, 9});
  static_cast<void>(document.move_tree.addNode(first, Move{89, 80}));
  const auto sibling = document.move_tree.addNode(0, Move{0, 18});
  static_cast<void>(document.move_tree.addNode(sibling, Move{89, 71}));
  return document;
}

[[nodiscard]] bool has_piece(const Position& position, Side side,
                             PieceType type, std::uint8_t square) {
  return std::ranges::any_of(position.pieces, [&](const Piece& piece) {
    return piece.side == side && piece.type == type && piece.square == square;
  });
}

}  // namespace

int main() {
  auto opened = oxq::editor::open_document(branched_document());
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(opened));

  const auto first = session.position_at(2);
  const auto second = session.position_at(2);
  if (!std::holds_alternative<Position>(first) || first != second ||
      session.position_cache_stats() !=
          oxq::editor::PositionCacheStats{1, 1, 1}) {
    return 2;
  }
  if (!std::holds_alternative<Position>(session.position_at(3)) ||
      session.position_cache_stats().entries != 2) {
    return 3;
  }

  Annotation annotation;
  annotation.text = "position-neutral";
  if (!std::holds_alternative<CommandResult>(
          session.execute(SetAnnotationsCommand{2, {annotation}}, 0)) ||
      !std::holds_alternative<Position>(session.position_at(2)) ||
      session.position_cache_stats().hits != 2) {
    return 4;
  }

  if (!std::holds_alternative<CommandResult>(
          session.execute(ReplaceMoveCommand{1, Move{0, 27}}, 1)) ||
      session.position_cache_stats().entries != 1) {
    return 5;
  }
  const auto replaced = session.position_at(2);
  if (!std::holds_alternative<Position>(replaced) ||
      !has_piece(std::get<Position>(replaced), Side::red, PieceType::rook, 27) ||
      session.position_cache_stats().misses != 3) {
    return 6;
  }

  if (!std::holds_alternative<CommandResult>(session.undo(2)) ||
      session.position_cache_stats().entries != 1) {
    return 7;
  }
  const auto restored_move = session.position_at(2);
  if (!std::holds_alternative<Position>(restored_move) ||
      !has_piece(std::get<Position>(restored_move), Side::red, PieceType::rook, 9)) {
    return 8;
  }

  if (!std::holds_alternative<CommandResult>(session.redo(3)) ||
      session.position_cache_stats().entries != 1 ||
      !std::holds_alternative<Position>(session.position_at(2)) ||
      !std::holds_alternative<CommandResult>(session.undo(4)) ||
      !std::holds_alternative<Position>(session.position_at(2))) {
    return 9;
  }

  if (!std::holds_alternative<CommandResult>(
          session.execute(DeleteSubtreeCommand{1}, 5)) ||
      session.position_cache_stats().entries != 1 ||
      !std::holds_alternative<CommandResult>(session.undo(6)) ||
      !std::holds_alternative<Position>(session.position_at(2))) {
    return 10;
  }

  const auto invalid = session.position_at(999);
  if (!std::holds_alternative<EditorError>(invalid) ||
      std::get<EditorError>(invalid).code != EditorErrorCode::node_not_found) {
    return 11;
  }

  SessionOptions one_entry;
  one_entry.max_position_cache_entries = 1;
  auto limited_opened =
      oxq::editor::open_document(branched_document(), one_entry);
  auto limited = std::get<EditorSession>(std::move(limited_opened));
  static_cast<void>(limited.position_at(2));
  static_cast<void>(limited.position_at(3));
  static_cast<void>(limited.position_at(2));
  if (limited.position_cache_stats() !=
      oxq::editor::PositionCacheStats{1, 0, 3}) {
    return 12;
  }

  SessionOptions disabled_options;
  disabled_options.max_position_cache_entries = 0;
  auto disabled_opened =
      oxq::editor::open_document(branched_document(), disabled_options);
  auto disabled = std::get<EditorSession>(std::move(disabled_opened));
  static_cast<void>(disabled.position_at(2));
  static_cast<void>(disabled.position_at(2));
  if (disabled.position_cache_stats() !=
      oxq::editor::PositionCacheStats{0, 0, 2}) {
    return 13;
  }
  return 0;
}
