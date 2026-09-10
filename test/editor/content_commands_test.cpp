#include <oxq/editor/command.hpp>
#include <oxq/editor/editor_session.hpp>

#include <string>
#include <variant>
#include <vector>

namespace {

using oxq::editor::CommandResult;
using oxq::editor::EditorError;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::editor::SetAnnotationsCommand;
using oxq::editor::SetMetadataCommand;
using oxq::format::Annotation;
using oxq::format::GameDocument;
using oxq::format::GameMetadata;

[[nodiscard]] GameDocument empty_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000035");
  return document;
}

[[nodiscard]] bool is_error(const oxq::editor::CommandOutcome& outcome,
                            EditorErrorCode code) {
  return std::holds_alternative<EditorError>(outcome) &&
         std::get<EditorError>(outcome).code == code;
}

}  // namespace

int main() {
  const auto original = empty_document();
  auto opened = oxq::editor::open_document(original);
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(opened));

  Annotation annotation;
  annotation.text = "根节点注释";
  annotation.author = "tester";
  const std::vector<Annotation> annotations{annotation};
  const auto annotated =
      session.execute(SetAnnotationsCommand{0, annotations}, 0);
  if (!std::holds_alternative<CommandResult>(annotated) ||
      std::get<CommandResult>(annotated).changes.updated !=
          std::vector<oxq::format::NodeId>{0} ||
      session.document().move_tree.findNode(0)->annotations != annotations ||
      session.state().revision != 1 || !session.state().dirty) {
    return 2;
  }

  const auto no_op = session.execute(SetAnnotationsCommand{0, annotations}, 1);
  if (!std::holds_alternative<CommandResult>(no_op) ||
      std::get<CommandResult>(no_op).revision != 1 ||
      !std::get<CommandResult>(no_op).changes.updated.empty()) {
    return 3;
  }
  auto invalid_annotation = annotation;
  invalid_annotation.kind = static_cast<oxq::format::AnnotationKind>(255);
  if (!is_error(session.execute(SetAnnotationsCommand{0, {invalid_annotation}}, 1),
                EditorErrorCode::validation_failed) ||
      !is_error(session.execute(SetAnnotationsCommand{99, annotations}, 1),
                EditorErrorCode::node_not_found) ||
      session.state().revision != 1) {
    return 4;
  }

  GameMetadata metadata;
  metadata.title = "研究局";
  metadata.tags = {"中局", "待复核"};
  const auto metadata_set = session.execute(SetMetadataCommand{metadata}, 1);
  if (!std::holds_alternative<CommandResult>(metadata_set) ||
      !std::get<CommandResult>(metadata_set).changes.metadata_changed ||
      session.document().metadata != metadata || session.state().revision != 2) {
    return 5;
  }
  const auto metadata_no_op = session.execute(SetMetadataCommand{metadata}, 2);
  if (!std::holds_alternative<CommandResult>(metadata_no_op) ||
      std::get<CommandResult>(metadata_no_op).changes.metadata_changed ||
      session.state().revision != 2) {
    return 6;
  }
  auto invalid_metadata = metadata;
  invalid_metadata.tags.push_back(std::string{"\xc0\x80", 2});
  if (!is_error(session.execute(SetMetadataCommand{invalid_metadata}, 2),
                EditorErrorCode::validation_failed) ||
      session.state().revision != 2 || session.document().metadata != metadata) {
    return 7;
  }

  const auto undo_metadata = session.undo(2);
  if (!std::holds_alternative<CommandResult>(undo_metadata) ||
      !std::get<CommandResult>(undo_metadata).changes.metadata_changed ||
      session.document().metadata != original.metadata ||
      session.document().move_tree.findNode(0)->annotations != annotations ||
      session.state().revision != 3) {
    return 8;
  }
  const auto undo_annotations = session.undo(3);
  if (!std::holds_alternative<CommandResult>(undo_annotations) ||
      std::get<CommandResult>(undo_annotations).changes.updated !=
          std::vector<oxq::format::NodeId>{0} ||
      !(session.export_document() == original) || session.state().revision != 4 ||
      session.state().dirty) {
    return 9;
  }
  if (!std::holds_alternative<CommandResult>(session.redo(4)) ||
      !std::holds_alternative<CommandResult>(session.redo(5)) ||
      session.document().metadata != metadata ||
      session.document().move_tree.findNode(0)->annotations != annotations ||
      session.state().revision != 6 || !session.state().dirty) {
    return 10;
  }
  return 0;
}
