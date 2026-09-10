#include <oxq/editor/console.hpp>

#include <string>
#include <variant>
#include <vector>

namespace {

using oxq::editor::ConsoleError;
using oxq::editor::ConsoleErrorCode;
using oxq::editor::ConsoleParseOutcome;
using oxq::editor::ConsoleRequest;
using oxq::editor::ConsoleToken;
using oxq::editor::ExecuteConsoleRequest;
using oxq::editor::InsertMoveCommand;
using oxq::editor::SetAnnotationsCommand;

[[nodiscard]] bool is_error(const ConsoleParseOutcome& outcome,
                            ConsoleErrorCode code) {
  return std::holds_alternative<ConsoleError>(outcome) &&
         std::get<ConsoleError>(outcome).code == code;
}

}  // namespace

int main() {
  const auto tokenized = oxq::editor::tokenize_console(
      "annotation set --node 7 --kind comment --text \"中炮\\n变化\\t\\\"A\\\"\" # ignored");
  if (!std::holds_alternative<std::vector<ConsoleToken>>(tokenized)) {
    return 1;
  }
  const auto& tokens = std::get<std::vector<ConsoleToken>>(tokenized);
  if (tokens.size() != 8 || tokens.back().value != "中炮\n变化\t\"A\"" ||
      tokens.back().span.start != 46) {
    return 2;
  }
  const auto annotation = oxq::editor::parse_console(tokens);
  if (!std::holds_alternative<ConsoleRequest>(annotation)) {
    return 3;
  }
  const auto& annotation_request =
      std::get<ConsoleRequest>(annotation);
  if (!std::holds_alternative<ExecuteConsoleRequest>(annotation_request)) {
    return 4;
  }
  const auto& annotation_command =
      std::get<ExecuteConsoleRequest>(annotation_request).command;
  if (!std::holds_alternative<SetAnnotationsCommand>(annotation_command) ||
      std::get<SetAnnotationsCommand>(annotation_command)
              .annotations.front()
              .text != "中炮\n变化\t\"A\"") {
    return 5;
  }

  const auto move = oxq::editor::parse_console(
      "move add --parent 42 --from h2 --to 89 --index 0");
  if (!std::holds_alternative<ConsoleRequest>(move)) {
    return 6;
  }
  const auto& move_request = std::get<ConsoleRequest>(move);
  if (!std::holds_alternative<ExecuteConsoleRequest>(move_request)) {
    return 7;
  }
  const auto& move_command = std::get<ExecuteConsoleRequest>(move_request).command;
  if (!std::holds_alternative<InsertMoveCommand>(move_command) ||
      std::get<InsertMoveCommand>(move_command).parent != 42 ||
      std::get<InsertMoveCommand>(move_command).move !=
          oxq::format::Move{25, 89} ||
      std::get<InsertMoveCommand>(move_command).sibling_index != 0) {
    return 8;
  }

  for (const auto input : {
           "status", "tree --from 1 --depth 2 --nodes 30",
           "node show --node 1", "validate", "validate --state",
           "move replace --node 1 --from 0 --to a1",
           "node delete --node 1", "branch promote --node 1 --index 0",
           "checkout --node 1", "undo", "redo", "help", "help tree"}) {
    if (!std::holds_alternative<ConsoleRequest>(
            oxq::editor::parse_console(input))) {
      return 9;
    }
  }

  const std::vector<std::pair<std::string, ConsoleErrorCode>> invalid_requests{
      {"", ConsoleErrorCode::invalid_syntax},
      {"unknown", ConsoleErrorCode::unknown_command},
      {"status extra", ConsoleErrorCode::invalid_syntax},
      {"checkout", ConsoleErrorCode::missing_option},
      {"checkout --node 1 --node 2", ConsoleErrorCode::duplicate_option},
      {"tree --bogus 1", ConsoleErrorCode::unknown_option},
      {"checkout --node -1", ConsoleErrorCode::invalid_value},
      {"checkout --node 18446744073709551616",
       ConsoleErrorCode::invalid_value},
      {"move add --parent 0 --from j0 --to 1",
       ConsoleErrorCode::invalid_value},
  };
  for (std::size_t index = 0; index < invalid_requests.size(); ++index) {
    const auto& [input, code] = invalid_requests[index];
    if (!is_error(oxq::editor::parse_console(input), code)) {
      return static_cast<int>(20 + index);
    }
  }

  const auto unterminated = oxq::editor::parse_console("help \"open");
  const auto invalid_escape = oxq::editor::parse_console("help \"a\\q\"");
  const auto adjacent = oxq::editor::parse_console("help \"a\"b");
  if (!is_error(unterminated, ConsoleErrorCode::unterminated_string) ||
      !is_error(invalid_escape, ConsoleErrorCode::invalid_escape) ||
      !is_error(adjacent, ConsoleErrorCode::unexpected_character)) {
    return 11;
  }

  const std::string invalid_utf8{"help \xc0\x80", 7};
  if (!is_error(oxq::editor::parse_console(invalid_utf8),
                ConsoleErrorCode::invalid_utf8)) {
    return 12;
  }
  oxq::editor::ConsoleLimits short_line;
  short_line.max_line_bytes = 3;
  if (!is_error(oxq::editor::parse_console("help", short_line),
                ConsoleErrorCode::input_too_long)) {
    return 13;
  }
  oxq::editor::ConsoleLimits one_token;
  one_token.max_tokens = 1;
  if (!is_error(oxq::editor::parse_console("help tree", one_token),
                ConsoleErrorCode::too_many_tokens)) {
    return 14;
  }

  const auto embedded_hash = oxq::editor::tokenize_console("help abc#def");
  if (!std::holds_alternative<std::vector<ConsoleToken>>(embedded_hash) ||
      std::get<std::vector<ConsoleToken>>(embedded_hash).back().value !=
          "abc#def") {
    return 15;
  }
  return 0;
}
