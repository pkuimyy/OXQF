#include <oxq/editor/command.hpp>
#include <oxq/editor/editor_session.hpp>

#include <string>
#include <variant>
#include <vector>

namespace {

using oxq::editor::CommandResult;
using oxq::editor::DeleteSubtreeCommand;
using oxq::editor::EditorError;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::format::GameDocument;
using oxq::format::Move;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Side;

[[nodiscard]] GameDocument branching_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000032");
  document.initial_position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::black, PieceType::rook, 89},
  };
  const auto main = document.move_tree.addNode(0, Move{0, 9});
  static_cast<void>(document.move_tree.addNode(0, Move{0, 18}));
  const auto reply = document.move_tree.addNode(main, Move{89, 80});
  oxq::format::Annotation annotation;
  annotation.text = "nested annotation";
  document.move_tree.findNode(reply)->annotations.push_back(std::move(annotation));
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
  if (session.checkout(3, 0).has_value()) {
    return 2;
  }

  const auto deleted = session.execute(DeleteSubtreeCommand{1}, 1);
  if (!std::holds_alternative<CommandResult>(deleted)) {
    return 3;
  }
  const auto& delete_result = std::get<CommandResult>(deleted);
  if (delete_result.changes.removed !=
          std::vector<oxq::format::NodeId>({1, 3}) ||
      !delete_result.changes.selection_changed ||
      session.state().current_node != 0 || session.state().revision != 2 ||
      !session.state().dirty || session.document().move_tree.findNode(1) != nullptr ||
      session.document().move_tree.findNode(3) != nullptr ||
      session.document().move_tree.nodes.front().children !=
          std::vector<oxq::format::NodeId>{2}) {
    return 4;
  }

  const auto undone = session.undo(2);
  if (!std::holds_alternative<CommandResult>(undone) ||
      std::get<CommandResult>(undone).changes.inserted !=
          std::vector<oxq::format::NodeId>({1, 3}) ||
      session.state().current_node != 3 || session.state().revision != 3 ||
      session.state().dirty || !(session.export_document() == original)) {
    return 5;
  }
  const auto* restored_reply = session.document().move_tree.findNode(3);
  if (restored_reply == nullptr || restored_reply->annotations.size() != 1 ||
      restored_reply->annotations.front().text != "nested annotation") {
    return 6;
  }

  const auto redone = session.redo(3);
  if (!std::holds_alternative<CommandResult>(redone) ||
      std::get<CommandResult>(redone).changes.removed !=
          std::vector<oxq::format::NodeId>({1, 3}) ||
      session.state().current_node != 0 || session.state().revision != 4 ||
      !session.state().dirty) {
    return 7;
  }
  if (!is_error(session.execute(DeleteSubtreeCommand{0}, 4),
                EditorErrorCode::invalid_argument) ||
      !is_error(session.execute(DeleteSubtreeCommand{999}, 4),
                EditorErrorCode::node_not_found) ||
      session.state().revision != 4) {
    return 8;
  }

  if (!std::holds_alternative<CommandResult>(session.undo(4)) ||
      session.checkout(2, 5).has_value()) {
    return 9;
  }
  const auto deleted_off_path = session.execute(DeleteSubtreeCommand{1}, 6);
  if (!std::holds_alternative<CommandResult>(deleted_off_path) ||
      session.state().current_node != 2 ||
      std::get<CommandResult>(deleted_off_path).changes.selection_changed) {
    return 10;
  }
  return 0;
}
