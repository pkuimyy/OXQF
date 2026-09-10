#include <oxq/editor/command.hpp>
#include <oxq/editor/editor_session.hpp>

#include <variant>

namespace {

using oxq::editor::CommandOutcome;
using oxq::editor::EditorError;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::editor::InsertMoveCommand;
using oxq::format::GameDocument;
using oxq::format::Move;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Side;

[[nodiscard]] GameDocument starting_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000031");
  document.initial_position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::black, PieceType::rook, 89},
  };
  static_cast<void>(document.move_tree.addNode(0, Move{0, 9}));
  return document;
}

[[nodiscard]] bool is_error(const CommandOutcome& outcome,
                            EditorErrorCode code) {
  return std::holds_alternative<EditorError>(outcome) &&
         std::get<EditorError>(outcome).code == code;
}

}  // namespace

int main() {
  auto opened = oxq::editor::open_document(starting_document());
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(opened));

  const auto inserted = session.execute(InsertMoveCommand{0, Move{0, 18}, 0}, 0);
  if (!std::holds_alternative<oxq::editor::CommandResult>(inserted)) {
    return 2;
  }
  const auto& result = std::get<oxq::editor::CommandResult>(inserted);
  if (result.created_node != 2 || result.revision != 1 ||
      result.changes.inserted != std::vector<oxq::format::NodeId>{2} ||
      !result.changes.selection_changed || session.state().current_node != 2 ||
      !session.state().dirty || !session.state().can_undo ||
      session.state().can_redo ||
      session.document().move_tree.nodes.front().children !=
          std::vector<oxq::format::NodeId>({2, 1})) {
    return 3;
  }

  const auto duplicate = session.execute(InsertMoveCommand{0, Move{0, 18}, {}}, 1);
  if (!is_error(duplicate, EditorErrorCode::duplicate_move) ||
      std::get<EditorError>(duplicate).node_id != 2 ||
      session.state().revision != 1) {
    return 4;
  }
  const auto invalid_state =
      session.execute(InsertMoveCommand{0, Move{89, 80}, {}}, 1);
  if (!is_error(invalid_state, EditorErrorCode::validation_failed) ||
      session.state().revision != 1 ||
      session.document().move_tree.findNode(3) != nullptr) {
    return 5;
  }

  if (session.mark_saved(1).has_value() || session.state().dirty) {
    return 6;
  }
  const auto undone = session.undo(1);
  if (!std::holds_alternative<oxq::editor::CommandResult>(undone) ||
      session.state().revision != 2 || !session.state().dirty ||
      session.state().can_undo || !session.state().can_redo ||
      session.state().current_node != 0 ||
      session.document().move_tree.findNode(2) != nullptr) {
    return 7;
  }
  const auto redone = session.redo(2);
  if (!std::holds_alternative<oxq::editor::CommandResult>(redone) ||
      std::get<oxq::editor::CommandResult>(redone).created_node != 2 ||
      session.state().revision != 3 || session.state().dirty ||
      session.state().current_node != 2 ||
      session.document().move_tree.nodes.front().children !=
          std::vector<oxq::format::NodeId>({2, 1})) {
    return 8;
  }

  if (!std::holds_alternative<oxq::editor::CommandResult>(session.undo(3))) {
    return 9;
  }
  const auto replacement =
      session.execute(InsertMoveCommand{0, Move{0, 27}, {}}, 4);
  if (!std::holds_alternative<oxq::editor::CommandResult>(replacement) ||
      std::get<oxq::editor::CommandResult>(replacement).created_node != 3 ||
      session.state().revision != 5 || !session.state().can_undo ||
      session.state().can_redo) {
    return 10;
  }
  if (!std::holds_alternative<oxq::editor::CommandResult>(session.undo(5)) ||
      !is_error(session.undo(6), EditorErrorCode::history_empty) ||
      session.state().revision != 6) {
    return 11;
  }

  const auto invalid_index =
      session.execute(InsertMoveCommand{0, Move{0, 36}, 99}, 6);
  if (!is_error(invalid_index, EditorErrorCode::invalid_argument) ||
      session.state().revision != 6) {
    return 12;
  }
  return 0;
}
