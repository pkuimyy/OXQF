#include <oxq/editor/command.hpp>
#include <oxq/editor/editor_session.hpp>

#include <string>
#include <variant>
#include <vector>

namespace {

using oxq::editor::AtomicCommand;
using oxq::editor::CommandResult;
using oxq::editor::CompoundCommand;
using oxq::editor::EditorError;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::editor::InsertMoveCommand;
using oxq::editor::SessionOptions;
using oxq::editor::SetAnnotationsCommand;
using oxq::editor::SetMetadataCommand;
using oxq::editor::ValidationPolicy;
using oxq::format::Annotation;
using oxq::format::GameDocument;
using oxq::format::GameMetadata;
using oxq::format::Move;
using oxq::format::NodeId;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Side;

[[nodiscard]] GameDocument starting_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000036");
  document.initial_position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::black, PieceType::rook, 89},
  };
  return document;
}

[[nodiscard]] bool is_error(const oxq::editor::CommandOutcome& outcome,
                            EditorErrorCode code) {
  return std::holds_alternative<EditorError>(outcome) &&
         std::get<EditorError>(outcome).code == code;
}

}  // namespace

int main() {
  const auto original = starting_document();
  auto opened = oxq::editor::open_document(original);
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(opened));

  Annotation annotation;
  annotation.text = "关键变化";
  GameMetadata metadata;
  metadata.title = "复合编辑";
  CompoundCommand compound{{
      AtomicCommand{InsertMoveCommand{0, Move{0, 9}, {}}},
      AtomicCommand{InsertMoveCommand{1, Move{89, 80}, {}}},
      AtomicCommand{SetAnnotationsCommand{2, {annotation}}},
      AtomicCommand{SetMetadataCommand{metadata}},
  }};
  const auto executed = session.execute(compound, 0);
  if (!std::holds_alternative<CommandResult>(executed)) {
    return 2;
  }
  const auto& result = std::get<CommandResult>(executed);
  if (result.revision != 1 || result.created_node.has_value() ||
      result.changes.before_revision != 0 || result.changes.after_revision != 1 ||
      result.changes.inserted != std::vector<NodeId>({1, 2}) ||
      result.changes.updated != std::vector<NodeId>{2} ||
      !result.changes.metadata_changed || !result.changes.selection_changed ||
      session.state().current_node != 2 || !session.state().dirty ||
      !session.state().can_undo || session.state().can_redo ||
      session.document().metadata != metadata ||
      session.document().move_tree.findNode(2)->annotations !=
          std::vector<Annotation>{annotation}) {
    return 3;
  }
  const auto edited = session.export_document();

  const auto undone = session.undo(1);
  if (!std::holds_alternative<CommandResult>(undone)) {
    return 4;
  }
  const auto& undo_result = std::get<CommandResult>(undone);
  if (undo_result.revision != 2 ||
      undo_result.changes.removed != std::vector<NodeId>({1, 2}) ||
      !undo_result.changes.metadata_changed ||
      !(session.export_document() == original) || session.state().dirty ||
      session.state().current_node != 0 || session.state().can_undo ||
      !session.state().can_redo) {
    return 5;
  }

  const auto redone = session.redo(2);
  if (!std::holds_alternative<CommandResult>(redone)) {
    return 6;
  }
  const auto& redo_result = std::get<CommandResult>(redone);
  if (redo_result.revision != 3 ||
      redo_result.changes.inserted != std::vector<NodeId>({1, 2}) ||
      !(session.export_document() == edited) || !session.state().dirty ||
      session.state().current_node != 2 || !session.state().can_undo ||
      session.state().can_redo) {
    return 7;
  }

  auto failure_opened = oxq::editor::open_document(original);
  auto failure_session = std::get<EditorSession>(std::move(failure_opened));
  CompoundCommand failing{{
      AtomicCommand{InsertMoveCommand{0, Move{0, 9}, {}}},
      AtomicCommand{InsertMoveCommand{0, Move{89, 80}, {}}},
  }};
  if (!is_error(failure_session.execute(std::move(failing), 0),
                EditorErrorCode::validation_failed) ||
      !(failure_session.export_document() == original) ||
      failure_session.state().revision != 0 ||
      failure_session.state().can_undo || failure_session.state().dirty) {
    return 8;
  }

  const auto empty = failure_session.execute(CompoundCommand{}, 0);
  if (!std::holds_alternative<CommandResult>(empty) ||
      std::get<CommandResult>(empty).revision != 0 ||
      failure_session.state().can_undo) {
    return 9;
  }

  auto limited_opened = oxq::editor::open_document(
      original, SessionOptions{ValidationPolicy::state_consistent, 1});
  auto limited_session = std::get<EditorSession>(std::move(limited_opened));
  CompoundCommand too_large{{
      AtomicCommand{SetMetadataCommand{metadata}},
      AtomicCommand{SetAnnotationsCommand{0, {annotation}}},
  }};
  if (!is_error(limited_session.execute(std::move(too_large), 0),
                EditorErrorCode::resource_limit) ||
      limited_session.state().revision != 0 || limited_session.state().dirty ||
      !(limited_session.export_document() == original)) {
    return 10;
  }
  return 0;
}
