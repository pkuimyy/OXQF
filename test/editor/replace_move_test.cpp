#include <oxq/editor/command.hpp>
#include <oxq/editor/editor_session.hpp>

#include <variant>
#include <vector>

namespace {

using oxq::editor::CommandResult;
using oxq::editor::EditorError;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::editor::ReplaceMoveCommand;
using oxq::format::GameDocument;
using oxq::format::Move;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Side;

[[nodiscard]] GameDocument branching_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000033");
  document.initial_position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::black, PieceType::rook, 89},
  };
  const auto main = document.move_tree.addNode(0, Move{0, 9});
  static_cast<void>(document.move_tree.addNode(main, Move{89, 80}));
  static_cast<void>(document.move_tree.addNode(0, Move{0, 27}));
  return document;
}

[[nodiscard]] bool is_error(const oxq::editor::CommandOutcome& outcome,
                            EditorErrorCode code) {
  return std::holds_alternative<EditorError>(outcome) &&
         std::get<EditorError>(outcome).code == code;
}

}  // namespace

int main() {
  const auto original = branching_document();
  auto opened = oxq::editor::open_document(original);
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(opened));

  const auto invalidates_descendant =
      session.execute(ReplaceMoveCommand{1, Move{0, 89}}, 0);
  if (!is_error(invalidates_descendant, EditorErrorCode::validation_failed) ||
      session.state().revision != 0 || !(session.export_document() == original)) {
    return 2;
  }
  const auto duplicate = session.execute(ReplaceMoveCommand{1, Move{0, 27}}, 0);
  if (!is_error(duplicate, EditorErrorCode::duplicate_move) ||
      std::get<EditorError>(duplicate).node_id != 3 ||
      session.state().revision != 0) {
    return 3;
  }
  if (!is_error(session.execute(ReplaceMoveCommand{0, Move{0, 18}}, 0),
                EditorErrorCode::invalid_argument) ||
      !is_error(session.execute(ReplaceMoveCommand{99, Move{0, 18}}, 0),
                EditorErrorCode::node_not_found) ||
      !is_error(session.execute(ReplaceMoveCommand{1, Move{18, 18}}, 0),
                EditorErrorCode::invalid_argument)) {
    return 4;
  }

  const auto replaced = session.execute(ReplaceMoveCommand{1, Move{0, 18}}, 0);
  if (!std::holds_alternative<CommandResult>(replaced) ||
      std::get<CommandResult>(replaced).changes.updated !=
          std::vector<oxq::format::NodeId>{1} ||
      session.document().move_tree.findNode(1)->move !=
          std::optional<Move>{Move{0, 18}} ||
      session.state().revision != 1 || !session.state().dirty ||
      session.state().current_node != 0) {
    return 5;
  }

  const auto no_op = session.execute(ReplaceMoveCommand{1, Move{0, 18}}, 1);
  if (!std::holds_alternative<CommandResult>(no_op) ||
      std::get<CommandResult>(no_op).revision != 1 ||
      !std::get<CommandResult>(no_op).changes.updated.empty() ||
      session.state().revision != 1) {
    return 6;
  }

  const auto undone = session.undo(1);
  if (!std::holds_alternative<CommandResult>(undone) ||
      std::get<CommandResult>(undone).changes.updated !=
          std::vector<oxq::format::NodeId>{1} ||
      session.state().revision != 2 || session.state().dirty ||
      !(session.export_document() == original)) {
    return 7;
  }
  const auto redone = session.redo(2);
  if (!std::holds_alternative<CommandResult>(redone) ||
      session.document().move_tree.findNode(1)->move !=
          std::optional<Move>{Move{0, 18}} ||
      session.state().revision != 3 || !session.state().dirty) {
    return 8;
  }

  if (session.mark_saved(3).has_value() || session.state().dirty ||
      !std::holds_alternative<CommandResult>(session.undo(3)) ||
      !session.state().dirty ||
      !std::holds_alternative<CommandResult>(session.redo(4)) ||
      session.state().dirty) {
    return 9;
  }
  return 0;
}
