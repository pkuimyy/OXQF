#include <oxq/editor/command.hpp>
#include <oxq/editor/editor_session.hpp>

#include <variant>
#include <vector>

namespace {

using oxq::editor::CommandResult;
using oxq::editor::EditorError;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::editor::ReorderVariationCommand;
using oxq::format::GameDocument;
using oxq::format::Move;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Side;

[[nodiscard]] GameDocument variation_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000034");
  document.initial_position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::black, PieceType::rook, 89},
  };
  static_cast<void>(document.move_tree.addNode(0, Move{0, 9}));
  static_cast<void>(document.move_tree.addNode(0, Move{0, 18}));
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
  const auto original = variation_document();
  auto opened = oxq::editor::open_document(original);
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(opened));
  if (session.checkout(2, 0).has_value()) {
    return 2;
  }

  const auto promoted = session.execute(ReorderVariationCommand{3, 0}, 1);
  if (!std::holds_alternative<CommandResult>(promoted) ||
      std::get<CommandResult>(promoted).changes.reordered_parents !=
          std::vector<oxq::format::NodeId>{0} ||
      !std::get<CommandResult>(promoted).changes.updated.empty() ||
      std::get<CommandResult>(promoted).changes.selection_changed ||
      session.document().move_tree.nodes.front().children !=
          std::vector<oxq::format::NodeId>({3, 1, 2}) ||
      session.state().current_node != 2 || session.state().revision != 2 ||
      !session.state().dirty) {
    return 3;
  }

  const auto no_op = session.execute(ReorderVariationCommand{3, 0}, 2);
  if (!std::holds_alternative<CommandResult>(no_op) ||
      std::get<CommandResult>(no_op).revision != 2 ||
      !std::get<CommandResult>(no_op).changes.reordered_parents.empty() ||
      session.state().revision != 2) {
    return 4;
  }
  if (!is_error(session.execute(ReorderVariationCommand{0, 0}, 2),
                EditorErrorCode::invalid_argument) ||
      !is_error(session.execute(ReorderVariationCommand{99, 0}, 2),
                EditorErrorCode::node_not_found) ||
      !is_error(session.execute(ReorderVariationCommand{3, 3}, 2),
                EditorErrorCode::invalid_argument) ||
      session.state().revision != 2) {
    return 5;
  }

  const auto undone = session.undo(2);
  if (!std::holds_alternative<CommandResult>(undone) ||
      session.document().move_tree.nodes.front().children !=
          std::vector<oxq::format::NodeId>({1, 2, 3}) ||
      session.state().current_node != 2 || session.state().revision != 3 ||
      session.state().dirty || !(session.export_document() == original)) {
    return 6;
  }
  const auto redone = session.redo(3);
  if (!std::holds_alternative<CommandResult>(redone) ||
      session.document().move_tree.nodes.front().children !=
          std::vector<oxq::format::NodeId>({3, 1, 2}) ||
      session.state().current_node != 2 || session.state().revision != 4 ||
      !session.state().dirty) {
    return 7;
  }

  if (session.mark_saved(4).has_value() || session.state().dirty ||
      !std::holds_alternative<CommandResult>(session.undo(4)) ||
      !session.state().dirty ||
      !std::holds_alternative<CommandResult>(session.redo(5)) ||
      session.state().dirty) {
    return 8;
  }
  return 0;
}
