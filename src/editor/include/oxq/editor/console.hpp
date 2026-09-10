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

class EditorSession;

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

enum class ConsoleResultKind {
  status,
  tree,
  node,
  validation,
  command,
  checkout,
  help,
};

struct ConsoleNodeDetails {
  NodeSummary summary;
  format::Position position;
  std::vector<format::Annotation> annotations;

  friend bool operator==(const ConsoleNodeDetails&,
                         const ConsoleNodeDetails&) = default;
};

struct ConsoleSessionState {
  format::NodeId current_node{0};
  bool dirty{false};
  std::uint64_t revision{0};
  bool can_undo{false};
  bool can_redo{false};

  friend bool operator==(const ConsoleSessionState&,
                         const ConsoleSessionState&) = default;
};

struct ConsoleResponse {
  std::uint32_t schema_version{1};
  std::string request_id;
  bool ok{false};
  std::uint64_t revision{0};
  ConsoleResultKind kind{ConsoleResultKind::status};
  std::optional<ConsoleSessionState> session_state;
  std::optional<VariationGraphProjection> graph;
  std::optional<ConsoleNodeDetails> node;
  std::vector<format::ValidationIssue> validation_issues;
  std::optional<CommandResult> command_result;
  std::optional<std::string> message;
  std::optional<ConsoleError> console_error;
  std::optional<EditorError> editor_error;

  friend bool operator==(const ConsoleResponse&, const ConsoleResponse&) = default;
};

class DebugConsole {
 public:
  explicit DebugConsole(EditorSession& session,
                        ConsoleLimits limits = {}) noexcept;

  [[nodiscard]] ConsoleResponse execute(
      std::string_view line, std::string request_id = {});
  [[nodiscard]] ConsoleResponse dispatch(
      ConsoleRequest request, std::string request_id = {});

 private:
  EditorSession& session_;
  ConsoleLimits limits_;
};

enum class ConsoleRenderFormat {
  text,
  json,
};

[[nodiscard]] std::string render_console_response(
    const ConsoleResponse& response, ConsoleRenderFormat format);

}  // namespace oxq::editor
