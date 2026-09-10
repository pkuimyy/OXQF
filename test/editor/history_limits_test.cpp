#include <oxq/editor/command.hpp>
#include <oxq/editor/editor_session.hpp>

#include <variant>

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
using oxq::format::Annotation;
using oxq::format::GameDocument;
using oxq::format::GameMetadata;
using oxq::format::Move;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Side;

[[nodiscard]] GameDocument starting_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000037");
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
  SessionOptions limited_options;
  limited_options.max_history_entries = 2;
  auto opened = oxq::editor::open_document(original, limited_options);
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(opened));

  GameMetadata first_metadata;
  first_metadata.title = "first";
  GameMetadata second_metadata;
  second_metadata.title = "second";
  Annotation annotation;
  annotation.text = "bounded history";
  if (!std::holds_alternative<CommandResult>(
          session.execute(SetMetadataCommand{first_metadata}, 0)) ||
      !std::holds_alternative<CommandResult>(session.execute(
          SetAnnotationsCommand{0, {annotation}}, 1)) ||
      !std::holds_alternative<CommandResult>(
          session.execute(SetMetadataCommand{second_metadata}, 2))) {
    return 2;
  }
  if (!std::holds_alternative<CommandResult>(session.undo(3)) ||
      session.document().metadata != first_metadata ||
      !std::holds_alternative<CommandResult>(session.undo(4)) ||
      !session.document().move_tree.findNode(0)->annotations.empty() ||
      !is_error(session.undo(5), EditorErrorCode::history_empty)) {
    return 3;
  }

  SessionOptions no_history_options;
  no_history_options.max_history_entries = 0;
  auto no_history_opened =
      oxq::editor::open_document(original, no_history_options);
  auto no_history = std::get<EditorSession>(std::move(no_history_opened));
  if (!is_error(no_history.execute(SetMetadataCommand{first_metadata}, 0),
                EditorErrorCode::resource_limit) ||
      no_history.state().revision != 0 || no_history.state().dirty ||
      !(no_history.export_document() == original)) {
    return 4;
  }

  SessionOptions tiny_bytes_options;
  tiny_bytes_options.max_history_bytes = 1;
  auto tiny_opened = oxq::editor::open_document(original, tiny_bytes_options);
  auto tiny = std::get<EditorSession>(std::move(tiny_opened));
  if (!is_error(tiny.execute(SetAnnotationsCommand{0, {annotation}}, 0),
                EditorErrorCode::resource_limit) ||
      tiny.state().revision != 0 || tiny.state().can_undo ||
      !(tiny.export_document() == original)) {
    return 5;
  }

  SessionOptions one_entry_options;
  one_entry_options.max_history_entries = 1;
  auto id_opened = oxq::editor::open_document(original, one_entry_options);
  auto id_session = std::get<EditorSession>(std::move(id_opened));
  const auto first = id_session.execute(InsertMoveCommand{0, Move{0, 9}, {}}, 0);
  const auto second = id_session.execute(InsertMoveCommand{1, Move{89, 80}, {}}, 1);
  if (!std::holds_alternative<CommandResult>(first) ||
      std::get<CommandResult>(first).created_node != 1 ||
      !std::holds_alternative<CommandResult>(second) ||
      std::get<CommandResult>(second).created_node != 2 ||
      !std::holds_alternative<CommandResult>(id_session.undo(2)) ||
      !is_error(id_session.undo(3), EditorErrorCode::history_empty)) {
    return 6;
  }
  const auto replacement =
      id_session.execute(InsertMoveCommand{1, Move{89, 71}, {}}, 3);
  if (!std::holds_alternative<CommandResult>(replacement) ||
      std::get<CommandResult>(replacement).created_node != 3 ||
      id_session.state().can_redo) {
    return 7;
  }

  auto compound_opened =
      oxq::editor::open_document(original, one_entry_options);
  auto compound_session = std::get<EditorSession>(std::move(compound_opened));
  CompoundCommand compound{{
      AtomicCommand{SetMetadataCommand{first_metadata}},
      AtomicCommand{SetAnnotationsCommand{0, {annotation}}},
  }};
  if (!std::holds_alternative<CommandResult>(
          compound_session.execute(std::move(compound), 0)) ||
      compound_session.state().revision != 1 ||
      !std::holds_alternative<CommandResult>(compound_session.undo(1)) ||
      !(compound_session.export_document() == original) ||
      !is_error(compound_session.undo(2), EditorErrorCode::history_empty)) {
    return 8;
  }
  return 0;
}
