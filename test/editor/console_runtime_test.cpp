#include <oxq/editor/console.hpp>
#include <oxq/editor/editor_session.hpp>

#include <string>
#include <string_view>
#include <variant>

namespace {

using oxq::editor::CommandResult;
using oxq::editor::DebugConsole;
using oxq::editor::EditorErrorCode;
using oxq::editor::EditorSession;
using oxq::editor::InsertMoveCommand;
using oxq::editor::SetAnnotationsCommand;
using oxq::format::Annotation;
using oxq::format::GameDocument;
using oxq::format::Move;
using oxq::format::Piece;
using oxq::format::PieceType;
using oxq::format::Side;

[[nodiscard]] GameDocument starting_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000041");
  document.initial_position.pieces = {
      Piece{Side::red, PieceType::rook, 0},
      Piece{Side::black, PieceType::rook, 89},
  };
  return document;
}

[[nodiscard]] bool contains(std::string_view text, std::string_view part) {
  return text.find(part) != std::string_view::npos;
}

}  // namespace

int main() {
  auto console_opened = oxq::editor::open_document(starting_document());
  auto direct_opened = oxq::editor::open_document(starting_document());
  if (!std::holds_alternative<EditorSession>(console_opened) ||
      !std::holds_alternative<EditorSession>(direct_opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(console_opened));
  auto direct = std::get<EditorSession>(std::move(direct_opened));
  DebugConsole console{session};

  const auto status = console.execute("status", "c-1");
  if (!status.ok || status.revision != 0 || !status.session_state.has_value() ||
      status.session_state->current_node != 0 || session.state().revision != 0) {
    return 2;
  }

  const auto first = console.execute(
      "move add --parent 0 --from a0 --to a1", "c-2");
  const auto direct_first =
      direct.execute(InsertMoveCommand{0, Move{0, 9}, {}}, 0);
  if (!first.ok || first.revision != 1 ||
      first.command_result->created_node != 1 ||
      !std::holds_alternative<CommandResult>(direct_first)) {
    return 3;
  }
  const auto second = console.execute(
      "move add --parent 1 --from i9 --to i8", "c-3");
  const auto direct_second =
      direct.execute(InsertMoveCommand{1, Move{89, 80}, {}}, 1);
  if (!second.ok || second.command_result->created_node != 2 ||
      !std::holds_alternative<CommandResult>(direct_second)) {
    return 4;
  }
  const auto annotated = console.execute(
      "annotation set --node 2 --kind comment --text \"中炮 \\\"研究\\\"\"",
      "c-4");
  Annotation annotation;
  annotation.text = "中炮 \"研究\"";
  const auto direct_annotation =
      direct.execute(SetAnnotationsCommand{2, {annotation}}, 2);
  if (!annotated.ok || !std::holds_alternative<CommandResult>(direct_annotation) ||
      !(session.export_document() == direct.export_document()) ||
      session.state() != direct.state()) {
    return 5;
  }

  const auto tree = console.execute("tree --depth 2 --nodes 10", "c-5");
  const auto node = console.execute("node show --node 2", "c-6");
  const auto validation = console.execute("validate --state", "c-7");
  const auto help = console.execute("help move", "c-8");
  if (!tree.ok || !tree.graph.has_value() || tree.graph->nodes.size() != 3 ||
      !node.ok || !node.node.has_value() ||
      node.node->annotations.front().text != annotation.text ||
      !validation.ok || !validation.validation_issues.empty() ||
      validation.message != "valid" || !help.ok || !help.message.has_value() ||
      session.state().revision != 3) {
    return 6;
  }

  const auto json = oxq::editor::render_console_response(
      node, oxq::editor::ConsoleRenderFormat::json);
  if (!contains(json, "\"schemaVersion\":1") ||
      !contains(json, "\"requestId\":\"c-6\"") ||
      !contains(json, "\"revision\":\"3\"") ||
      !contains(json, "\"id\":\"2\"") ||
      !contains(json, "中炮 \\\"研究\\\"")) {
    return 7;
  }
  const auto text = oxq::editor::render_console_response(
      first, oxq::editor::ConsoleRenderFormat::text);
  if (text != "ok revision=1 kind=command node=1") {
    return 8;
  }

  const auto parse_failure = console.execute("checkout --node -1", "bad-1");
  if (parse_failure.ok || !parse_failure.console_error.has_value() ||
      parse_failure.revision != 3 || session.state().revision != 3) {
    return 9;
  }
  const auto parse_json = oxq::editor::render_console_response(
      parse_failure, oxq::editor::ConsoleRenderFormat::json);
  if (!contains(parse_json, "\"code\":\"console.invalid_value\"") ||
      !contains(parse_json, "\"span\":")) {
    return 10;
  }

  const auto engine_failure = console.execute(
      "move add --parent 0 --from i9 --to i8", "bad-2");
  if (engine_failure.ok || !engine_failure.editor_error.has_value() ||
      engine_failure.editor_error->code != EditorErrorCode::validation_failed ||
      engine_failure.revision != 3 || session.state().revision != 3) {
    return 11;
  }
  const auto engine_json = oxq::editor::render_console_response(
      engine_failure, oxq::editor::ConsoleRenderFormat::json);
  if (!contains(engine_json, "\"code\":\"editor.validation_failed\"")) {
    return 12;
  }

  if (!console.execute("checkout --node 1", "c-9").ok ||
      session.state().revision != 4 || session.state().dirty != direct.state().dirty ||
      !console.execute("undo", "c-10").ok ||
      !std::holds_alternative<CommandResult>(direct.undo(3)) ||
      !(session.export_document() == direct.export_document()) ||
      session.state().revision != 5 || !console.execute("redo", "c-11").ok ||
      !std::holds_alternative<CommandResult>(direct.redo(4)) ||
      !(session.export_document() == direct.export_document())) {
    return 13;
  }
  return 0;
}
