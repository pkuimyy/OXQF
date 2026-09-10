#pragma once

#include <oxq/editor/command.hpp>
#include <oxq/editor/variation_graph.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oxq::editor {

struct ConsoleSpan {
  std::size_t start{0};
  std::size_t length{0};

  friend bool operator==(const ConsoleSpan&, const ConsoleSpan&) = default;
};

enum class ConsoleErrorCode {
  invalid_utf8,
  input_too_long,
  too_many_tokens,
  unterminated_string,
  invalid_escape,
  unexpected_character,
  unknown_command,
  invalid_syntax,
  missing_option,
  duplicate_option,
  unknown_option,
  invalid_value,
};

struct ConsoleError {
  ConsoleErrorCode code{};
  std::string message;
  ConsoleSpan span;

  friend bool operator==(const ConsoleError&, const ConsoleError&) = default;
};

struct ConsoleLimits {
  std::size_t max_line_bytes{64U * 1024U};
  std::size_t max_tokens{256};
};

struct ConsoleToken {
  std::string value;
  ConsoleSpan span;

  friend bool operator==(const ConsoleToken&, const ConsoleToken&) = default;
};

struct StatusConsoleRequest {
  friend bool operator==(const StatusConsoleRequest&,
                         const StatusConsoleRequest&) = default;
};

struct TreeConsoleRequest {
  VariationGraphQuery query;

  friend bool operator==(const TreeConsoleRequest&,
                         const TreeConsoleRequest&) = default;
};

struct NodeShowConsoleRequest {
  format::NodeId node{0};

  friend bool operator==(const NodeShowConsoleRequest&,
                         const NodeShowConsoleRequest&) = default;
};

struct ValidateConsoleRequest {
  bool state_consistent{false};

  friend bool operator==(const ValidateConsoleRequest&,
                         const ValidateConsoleRequest&) = default;
};

struct ExecuteConsoleRequest {
  Command command;

  friend bool operator==(const ExecuteConsoleRequest&,
                         const ExecuteConsoleRequest&) = default;
};

struct CheckoutConsoleRequest {
  format::NodeId node{0};

  friend bool operator==(const CheckoutConsoleRequest&,
                         const CheckoutConsoleRequest&) = default;
};

struct UndoConsoleRequest {
  friend bool operator==(const UndoConsoleRequest&,
                         const UndoConsoleRequest&) = default;
};

struct RedoConsoleRequest {
  friend bool operator==(const RedoConsoleRequest&,
                         const RedoConsoleRequest&) = default;
};

struct HelpConsoleRequest {
  std::optional<std::string> command;

  friend bool operator==(const HelpConsoleRequest&,
                         const HelpConsoleRequest&) = default;
};

using ConsoleRequest =
    std::variant<StatusConsoleRequest, TreeConsoleRequest,
                 NodeShowConsoleRequest, ValidateConsoleRequest,
                 ExecuteConsoleRequest, CheckoutConsoleRequest,
                 UndoConsoleRequest, RedoConsoleRequest, HelpConsoleRequest>;
using ConsoleTokensOutcome = std::variant<std::vector<ConsoleToken>, ConsoleError>;
using ConsoleParseOutcome = std::variant<ConsoleRequest, ConsoleError>;

[[nodiscard]] ConsoleTokensOutcome tokenize_console(
    std::string_view line, ConsoleLimits limits = {});
[[nodiscard]] ConsoleParseOutcome parse_console(
    const std::vector<ConsoleToken>& tokens);
[[nodiscard]] ConsoleParseOutcome parse_console(
    std::string_view line, ConsoleLimits limits = {});

}  // namespace oxq::editor
