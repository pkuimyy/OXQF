#include <oxq/editor/editor_session.hpp>
#include <oxq/editor/product.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <variant>

namespace {

using oxq::editor::EditorError;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::format::GameDocument;
using oxq::format::Move;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Position;
using oxq::format::Side;

[[nodiscard]] GameDocument document_with_line() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000030");
  document.initial_position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::black, PieceType::rook, 89},
  };
  const auto red_move = document.move_tree.addNode(0, Move{0, 9});
  static_cast<void>(document.move_tree.addNode(red_move, Move{89, 80}));
  return document;
}

[[nodiscard]] bool contains_piece(const Position& position, Side side,
                                  PieceType type, std::uint8_t square) {
  return std::ranges::any_of(position.pieces, [&](const Piece& piece) {
    return piece.side == side && piece.type == type && piece.square == square;
  });
}

[[nodiscard]] bool error_is(const oxq::editor::Status& status,
                            EditorErrorCode code) {
  return status.has_value() && status->code == code;
}

}  // namespace

int main() {
  if (oxq::editor::product_name() != std::string_view{"oxq-editor"} ||
      oxq::editor::product_version().empty()) {
    return 1;
  }

  auto opened = oxq::editor::open_document(document_with_line());
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 2;
  }
  auto session = std::get<EditorSession>(std::move(opened));
  if (session.state().current_node != 0 || session.state().dirty ||
      session.state().revision != 0 || session.state().can_undo ||
      session.state().can_redo) {
    return 3;
  }

  auto root_snapshot = session.snapshot();
  if (!std::holds_alternative<oxq::editor::SessionSnapshot>(root_snapshot)) {
    return 4;
  }
  const auto& root_position =
      std::get<oxq::editor::SessionSnapshot>(root_snapshot).position;
  if (root_position.side_to_move != Side::red ||
      root_position.fullmove_number != 1 ||
      !contains_piece(root_position, Side::red, PieceType::rook, 0)) {
    return 5;
  }

  if (session.checkout(1, 0).has_value() || session.state().revision != 1 ||
      session.state().current_node != 1 || session.state().dirty) {
    return 6;
  }
  if (session.checkout(1, 1).has_value() || session.state().revision != 1) {
    return 7;
  }
  const auto first_snapshot = session.snapshot();
  if (!std::holds_alternative<oxq::editor::SessionSnapshot>(first_snapshot)) {
    return 8;
  }
  const auto& first_position =
      std::get<oxq::editor::SessionSnapshot>(first_snapshot).position;
  if (first_position.side_to_move != Side::black ||
      first_position.fullmove_number != 1 ||
      !contains_piece(first_position, Side::red, PieceType::rook, 9)) {
    return 9;
  }

  if (!error_is(session.checkout(2, 0), EditorErrorCode::revision_conflict) ||
      session.state().revision != 1 || session.state().current_node != 1) {
    return 10;
  }
  if (!error_is(session.checkout(999), EditorErrorCode::node_not_found) ||
      session.state().revision != 1) {
    return 11;
  }
  if (session.checkout(2, 1).has_value() || session.state().revision != 2) {
    return 12;
  }
  const auto second_snapshot = session.snapshot();
  if (!std::holds_alternative<oxq::editor::SessionSnapshot>(second_snapshot)) {
    return 13;
  }
  const auto& second_position =
      std::get<oxq::editor::SessionSnapshot>(second_snapshot).position;
  if (second_position.side_to_move != Side::red ||
      second_position.fullmove_number != 2 ||
      !contains_piece(second_position, Side::red, PieceType::rook, 9) ||
      !contains_piece(second_position, Side::black, PieceType::rook, 80)) {
    return 14;
  }

  if (!error_is(session.mark_saved(1), EditorErrorCode::revision_conflict) ||
      session.mark_saved(2).has_value() ||
      !(session.export_document() == document_with_line())) {
    return 15;
  }

  auto invalid = document_with_line();
  invalid.uuid = {};
  const auto rejected = oxq::editor::open_document(std::move(invalid));
  if (!std::holds_alternative<EditorError>(rejected) ||
      std::get<EditorError>(rejected).code != EditorErrorCode::invalid_document) {
    return 16;
  }

  auto inconsistent = document_with_line();
  inconsistent.move_tree.findNode(1)->move = Move{10, 9};
  const auto state_rejected = oxq::editor::open_document(inconsistent);
  if (!std::holds_alternative<EditorError>(state_rejected)) {
    return 17;
  }
  auto structural = oxq::editor::open_document(
      std::move(inconsistent),
      {oxq::editor::ValidationPolicy::structural});
  if (!std::holds_alternative<EditorSession>(structural)) {
    return 18;
  }
  auto structural_session = std::get<EditorSession>(std::move(structural));
  if (structural_session.checkout(1).has_value()) {
    return 19;
  }
  const auto invalid_snapshot = structural_session.snapshot();
  if (!std::holds_alternative<EditorError>(invalid_snapshot) ||
      std::get<EditorError>(invalid_snapshot).code !=
          EditorErrorCode::invalid_position) {
    return 20;
  }

  auto overflow = document_with_line();
  overflow.initial_position.side_to_move = Side::black;
  overflow.initial_position.fullmove_number =
      std::numeric_limits<std::uint16_t>::max();
  overflow.move_tree.findNode(1)->move = Move{89, 80};
  static_cast<void>(overflow.move_tree.removeNode(2));
  auto overflow_opened = oxq::editor::open_document(std::move(overflow));
  if (!std::holds_alternative<EditorSession>(overflow_opened)) {
    return 21;
  }
  auto overflow_session = std::get<EditorSession>(std::move(overflow_opened));
  if (overflow_session.checkout(1).has_value()) {
    return 22;
  }
  const auto overflow_snapshot = overflow_session.snapshot();
  if (!std::holds_alternative<EditorError>(overflow_snapshot) ||
      std::get<EditorError>(overflow_snapshot).code !=
          EditorErrorCode::move_number_overflow) {
    return 23;
  }
  return 0;
}
