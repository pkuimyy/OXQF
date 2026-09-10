#include <oxq/convert/product.hpp>
#include <oxq/editor/console.hpp>
#include <oxq/editor/editor_session.hpp>
#include <oxq/editor/product.hpp>
#include <oxq/editor/variation_graph.hpp>
#include <oxq/format/document.hpp>
#include <oxq/format/product.hpp>
#include <oxq/format/reader.hpp>
#include <oxq/format/state_validation.hpp>
#include <oxq/format/validation.hpp>
#include <oxq/format/validator.hpp>
#include <oxq/format/writer.hpp>

#include <variant>

int main() {
  oxq::format::GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000022");
  document.initial_position.pieces.push_back(
      {oxq::format::Side::red, oxq::format::PieceType::rook, 0});
  const oxq::format::ValidatorOutcome validator{oxq::format::ReaderDiagnostics{}};
  auto session = oxq::editor::open_document(document);
  bool editor_command_works = false;
  if (std::holds_alternative<oxq::editor::EditorSession>(session)) {
    auto& editor = std::get<oxq::editor::EditorSession>(session);
    const auto result = editor.execute(
        oxq::editor::InsertMoveCommand{0, oxq::format::Move{0, 1}, {}});
    editor_command_works =
        std::holds_alternative<oxq::editor::CommandResult>(result) &&
        std::holds_alternative<oxq::editor::CommandResult>(editor.undo());
    oxq::editor::DebugConsole console{editor};
    editor_command_works = editor_command_works &&
                           console.execute("status", "consumer").ok &&
                           std::holds_alternative<
                               oxq::editor::VariationGraphProjection>(
                               editor.variation_graph());
  }

  return std::holds_alternative<oxq::format::ReaderDiagnostics>(validator) &&
                 editor_command_works &&
                 !oxq::format::has_errors(oxq::format::validate(document)) &&
                 oxq::editor::product_version() == oxq::format::product_version() &&
                 oxq::format::product_version() == oxq::convert::product_version()
             ? 0
             : 1;
}
