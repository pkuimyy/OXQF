#include <oxq/editor/console.hpp>

#include <oxq/editor/editor_session.hpp>
#include <oxq/format/state_validation.hpp>
#include <oxq/format/validation.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace oxq::editor {
namespace {

[[nodiscard]] ConsoleSessionState console_state(const SessionState& state) {
  return ConsoleSessionState{state.current_node, state.dirty, state.revision,
                             state.can_undo, state.can_redo};
}

[[nodiscard]] ConsoleResponse base_response(const EditorSession& session,
                                            std::string request_id) {
  ConsoleResponse response;
  response.request_id = std::move(request_id);
  response.revision = session.state().revision;
  return response;
}

[[nodiscard]] std::string_view kind_name(ConsoleResultKind kind) noexcept {
  switch (kind) {
    case ConsoleResultKind::status:
      return "status";
    case ConsoleResultKind::tree:
      return "tree";
    case ConsoleResultKind::node:
      return "node";
    case ConsoleResultKind::validation:
      return "validation";
    case ConsoleResultKind::command:
      return "command";
    case ConsoleResultKind::checkout:
      return "checkout";
    case ConsoleResultKind::help:
      return "help";
  }
  return "unknown";
}

[[nodiscard]] std::string_view console_error_name(
    ConsoleErrorCode code) noexcept {
  switch (code) {
    case ConsoleErrorCode::invalid_utf8:
      return "console.invalid_utf8";
    case ConsoleErrorCode::input_too_long:
      return "console.input_too_long";
    case ConsoleErrorCode::too_many_tokens:
      return "console.too_many_tokens";
    case ConsoleErrorCode::unterminated_string:
      return "console.unterminated_string";
    case ConsoleErrorCode::invalid_escape:
      return "console.invalid_escape";
    case ConsoleErrorCode::unexpected_character:
      return "console.unexpected_character";
    case ConsoleErrorCode::unknown_command:
      return "console.unknown_command";
    case ConsoleErrorCode::invalid_syntax:
      return "console.invalid_syntax";
    case ConsoleErrorCode::missing_option:
      return "console.missing_option";
    case ConsoleErrorCode::duplicate_option:
      return "console.duplicate_option";
    case ConsoleErrorCode::unknown_option:
      return "console.unknown_option";
    case ConsoleErrorCode::invalid_value:
      return "console.invalid_value";
  }
  return "console.unknown";
}

[[nodiscard]] std::string_view editor_error_name(EditorErrorCode code) noexcept {
  switch (code) {
    case EditorErrorCode::invalid_document:
      return "editor.invalid_document";
    case EditorErrorCode::invalid_argument:
      return "editor.invalid_argument";
    case EditorErrorCode::node_not_found:
      return "editor.node_not_found";
    case EditorErrorCode::duplicate_move:
      return "editor.duplicate_move";
    case EditorErrorCode::revision_conflict:
      return "editor.revision_conflict";
    case EditorErrorCode::validation_failed:
      return "editor.validation_failed";
    case EditorErrorCode::history_empty:
      return "editor.history_empty";
    case EditorErrorCode::resource_limit:
      return "editor.resource_limit";
    case EditorErrorCode::internal_invariant:
      return "editor.internal_invariant";
    case EditorErrorCode::invalid_position:
      return "editor.invalid_position";
    case EditorErrorCode::move_number_overflow:
      return "editor.move_number_overflow";
  }
  return "editor.unknown";
}

void append_json_string(std::string& output, std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  output.push_back('"');
  for (const auto character : value) {
    const auto byte = static_cast<std::uint8_t>(character);
    switch (character) {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (byte < 0x20U) {
          output += "\\u00";
          output.push_back(hex[byte >> 4U]);
          output.push_back(hex[byte & 0x0fU]);
        } else {
          output.push_back(character);
        }
    }
  }
  output.push_back('"');
}

void append_json_bool(std::string& output, bool value) {
  output += value ? "true" : "false";
}

void append_node_id(std::string& output, format::NodeId node) {
  append_json_string(output, std::to_string(node));
}

[[nodiscard]] std::string square_coordinate(std::uint8_t square) {
  std::string result;
  result.push_back(static_cast<char>('a' + square % 9U));
  result.push_back(static_cast<char>('0' + square / 9U));
  return result;
}

void append_move(std::string& output, const format::Move& move) {
  output += "{\"from\":{";
  output += "\"index\":" + std::to_string(move.from_square) + ",\"coord\":";
  append_json_string(output, square_coordinate(move.from_square));
  output += "},\"to\":{";
  output += "\"index\":" + std::to_string(move.to_square) + ",\"coord\":";
  append_json_string(output, square_coordinate(move.to_square));
  output += "}}";
}

void append_summary(std::string& output, const NodeSummary& summary) {
  output += "{\"id\":";
  append_node_id(output, summary.id);
  output += ",\"parent\":";
  if (summary.parent.has_value()) {
    append_node_id(output, *summary.parent);
  } else {
    output += "null";
  }
  output += ",\"move\":";
  if (summary.move.has_value()) {
    append_move(output, *summary.move);
  } else {
    output += "null";
  }
  output += ",\"childCount\":" + std::to_string(summary.child_count) +
            ",\"annotationCount\":" +
            std::to_string(summary.annotation_count) + "}";
}

void append_position(std::string& output, const format::Position& position) {
  output += "{\"sideToMove\":";
  append_json_string(output,
                     position.side_to_move == format::Side::red ? "red" : "black");
  output += ",\"fullmoveNumber\":" +
            std::to_string(position.fullmove_number) + ",\"pieces\":[";
  for (std::size_t index = 0; index < position.pieces.size(); ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    const auto& piece = position.pieces[index];
    output += "{\"side\":";
    append_json_string(output, piece.side == format::Side::red ? "red" : "black");
    output += ",\"type\":" +
              std::to_string(static_cast<unsigned int>(piece.type)) +
              ",\"square\":{\"index\":" +
              std::to_string(piece.square) + ",\"coord\":";
    append_json_string(output, square_coordinate(piece.square));
    output += "}}";
  }
  output += "]}";
}

void append_annotations(std::string& output,
                        const std::vector<format::Annotation>& annotations) {
  output.push_back('[');
  for (std::size_t index = 0; index < annotations.size(); ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    const auto& annotation = annotations[index];
    output += "{\"kind\":";
    append_json_string(output, annotation.kind == format::AnnotationKind::comment
                                   ? "comment"
                                   : "source_note");
    output += ",\"beforeMove\":";
    append_json_bool(output, annotation.before_move);
    output += ",\"text\":";
    append_json_string(output, annotation.text);
    output += ",\"author\":";
    if (annotation.author.has_value()) {
      append_json_string(output, *annotation.author);
    } else {
      output += "null";
    }
    output += ",\"language\":";
    if (annotation.language.has_value()) {
      append_json_string(output, *annotation.language);
    } else {
      output += "null";
    }
    output.push_back('}');
  }
  output.push_back(']');
}

void append_validation_issues(
    std::string& output,
    const std::vector<format::ValidationIssue>& issues) {
  output.push_back('[');
  for (std::size_t index = 0; index < issues.size(); ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    const auto& issue = issues[index];
    output += "{\"severity\":";
    append_json_string(output,
                       issue.severity == format::ValidationSeverity::error
                           ? "error"
                           : "warning");
    output += ",\"code\":" +
              std::to_string(static_cast<unsigned int>(issue.code)) +
              ",\"path\":";
    append_json_string(output, issue.path);
    output += ",\"message\":";
    append_json_string(output, issue.message);
    output.push_back('}');
  }
  output.push_back(']');
}

void append_state(std::string& output, const ConsoleSessionState& state) {
  output += "{\"currentNode\":";
  append_node_id(output, state.current_node);
  output += ",\"dirty\":";
  append_json_bool(output, state.dirty);
  output += ",\"revision\":";
  append_json_string(output, std::to_string(state.revision));
  output += ",\"canUndo\":";
  append_json_bool(output, state.can_undo);
  output += ",\"canRedo\":";
  append_json_bool(output, state.can_redo);
  output.push_back('}');
}

void append_graph(std::string& output,
                  const VariationGraphProjection& graph) {
  output += "{\"from\":";
  append_node_id(output, graph.from);
  output += ",\"truncated\":";
  append_json_bool(output, graph.truncated);
  output += ",\"nodes\":[";
  for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    const auto& node = graph.nodes[index];
    output += "{\"id\":";
    append_node_id(output, node.summary.id);
    output += ",\"parent\":";
    if (node.summary.parent.has_value()) {
      append_node_id(output, *node.summary.parent);
    } else {
      output += "null";
    }
    output += ",\"depth\":" + std::to_string(node.depth) +
              ",\"siblingIndex\":" + std::to_string(node.sibling_index) +
              ",\"mainVariation\":";
    append_json_bool(output, node.main_variation);
    output += ",\"onCurrentPath\":";
    append_json_bool(output, node.on_current_path);
    output += ",\"childCount\":" +
              std::to_string(node.summary.child_count) +
              ",\"annotationCount\":" +
              std::to_string(node.summary.annotation_count) + ",\"move\":";
    if (node.summary.move.has_value()) {
      append_move(output, *node.summary.move);
    } else {
      output += "null";
    }
    output.push_back('}');
  }
  output += "],\"edges\":[";
  for (std::size_t index = 0; index < graph.edges.size(); ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    const auto& edge = graph.edges[index];
    output += "{\"parent\":";
    append_node_id(output, edge.parent);
    output += ",\"child\":";
    append_node_id(output, edge.child);
    output += ",\"siblingIndex\":" + std::to_string(edge.sibling_index) +
              ",\"mainVariation\":";
    append_json_bool(output, edge.main_variation);
    output.push_back('}');
  }
  output += "]}";
}

void append_change_set(std::string& output, const ChangeSet& changes) {
  const auto append_ids = [&](const std::vector<format::NodeId>& nodes) {
    output.push_back('[');
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      if (index != 0) {
        output.push_back(',');
      }
      append_node_id(output, nodes[index]);
    }
    output.push_back(']');
  };
  output += "{\"beforeRevision\":";
  append_json_string(output, std::to_string(changes.before_revision));
  output += ",\"afterRevision\":";
  append_json_string(output, std::to_string(changes.after_revision));
  output += ",\"inserted\":";
  append_ids(changes.inserted);
  output += ",\"removed\":";
  append_ids(changes.removed);
  output += ",\"updated\":";
  append_ids(changes.updated);
  output += ",\"reorderedParents\":";
  append_ids(changes.reordered_parents);
  output += ",\"metadataChanged\":";
  append_json_bool(output, changes.metadata_changed);
  output += ",\"selectionChanged\":";
  append_json_bool(output, changes.selection_changed);
  output.push_back('}');
}

[[nodiscard]] std::string help_text(const std::optional<std::string>& command) {
  if (!command.has_value()) {
    std::string result;
    for (const auto& info : console_commands()) {
      if (!result.empty()) {
        result += "; ";
      }
      result += info.name;
    }
    return result;
  }
  for (const auto& info : console_commands()) {
    if (info.name == *command) {
      return std::string{info.usage};
    }
  }
  return "unknown help topic; use help for the command list";
}

}  // namespace

const std::vector<ConsoleCommandInfo>& console_commands() noexcept {
  static const std::vector<ConsoleCommandInfo> commands{
      {"status", "status", false},
      {"tree", "tree [--from ID] [--depth N] [--nodes N]", false},
      {"node", "node show --node ID; node delete --node ID", true},
      {"validate", "validate [--state]", false},
      {"move", "move add --parent ID --from SQUARE --to SQUARE [--index N]; move replace --node ID --from SQUARE --to SQUARE", true},
      {"branch", "branch promote --node ID --index N", true},
      {"checkout", "checkout --node ID", true},
      {"annotation", "annotation set --node ID --kind KIND --text TEXT", true},
      {"undo", "undo", true},
      {"redo", "redo", true},
      {"help", "help [command]", false},
  };
  return commands;
}

DebugConsole::DebugConsole(EditorSession& session, ConsoleLimits limits) noexcept
    : session_(session), limits_(limits) {}

ConsoleResponse DebugConsole::execute(std::string_view line,
                                      std::string request_id) {
  auto parsed = parse_console(line, limits_);
  if (std::holds_alternative<ConsoleError>(parsed)) {
    auto response = base_response(session_, std::move(request_id));
    response.console_error = std::get<ConsoleError>(std::move(parsed));
    return response;
  }
  return dispatch(std::get<ConsoleRequest>(std::move(parsed)),
                  std::move(request_id));
}

ConsoleResponse DebugConsole::dispatch(ConsoleRequest request,
                                       std::string request_id) {
  auto response = base_response(session_, std::move(request_id));
  std::visit(
      [&](auto&& typed_request) {
        using Request = std::decay_t<decltype(typed_request)>;
        if constexpr (std::is_same_v<Request, StatusConsoleRequest>) {
          response.kind = ConsoleResultKind::status;
          response.session_state = console_state(session_.state());
          response.ok = true;
        } else if constexpr (std::is_same_v<Request, TreeConsoleRequest>) {
          response.kind = ConsoleResultKind::tree;
          auto graph = session_.variation_graph(typed_request.query);
          if (std::holds_alternative<EditorError>(graph)) {
            response.editor_error = std::get<EditorError>(std::move(graph));
          } else {
            response.graph =
                std::get<VariationGraphProjection>(std::move(graph));
            response.ok = true;
          }
        } else if constexpr (std::is_same_v<Request, NodeShowConsoleRequest>) {
          response.kind = ConsoleResultKind::node;
          auto summary = session_.node_summary(typed_request.node);
          auto position = session_.position_at(typed_request.node);
          if (std::holds_alternative<EditorError>(summary)) {
            response.editor_error = std::get<EditorError>(std::move(summary));
          } else if (std::holds_alternative<EditorError>(position)) {
            response.editor_error = std::get<EditorError>(std::move(position));
          } else {
            const auto* node =
                session_.document().move_tree.findNode(typed_request.node);
            response.node = ConsoleNodeDetails{
                std::get<NodeSummary>(std::move(summary)),
                std::get<format::Position>(std::move(position)),
                node == nullptr ? std::vector<format::Annotation>{}
                                : node->annotations};
            response.ok = true;
          }
        } else if constexpr (std::is_same_v<Request,
                                             ValidateConsoleRequest>) {
          response.kind = ConsoleResultKind::validation;
          response.validation_issues =
              typed_request.state_consistent
                  ? format::validate_state(session_.document())
                  : format::validate(session_.document());
          response.message = format::has_errors(response.validation_issues)
                                 ? "invalid"
                                 : "valid";
          response.ok = true;
        } else if constexpr (std::is_same_v<Request,
                                             ExecuteConsoleRequest>) {
          response.kind = ConsoleResultKind::command;
          auto outcome = session_.execute(std::move(typed_request.command),
                                          session_.state().revision);
          if (std::holds_alternative<EditorError>(outcome)) {
            response.editor_error = std::get<EditorError>(std::move(outcome));
          } else {
            response.command_result =
                std::get<CommandResult>(std::move(outcome));
            response.ok = true;
          }
        } else if constexpr (std::is_same_v<Request,
                                             CheckoutConsoleRequest>) {
          response.kind = ConsoleResultKind::checkout;
          auto status = session_.checkout(typed_request.node,
                                          session_.state().revision);
          if (status.has_value()) {
            response.editor_error = *std::move(status);
          } else {
            response.session_state = console_state(session_.state());
            response.ok = true;
          }
        } else if constexpr (std::is_same_v<Request, UndoConsoleRequest> ||
                             std::is_same_v<Request, RedoConsoleRequest>) {
          response.kind = ConsoleResultKind::command;
          auto outcome = [&]() {
            if constexpr (std::is_same_v<Request, UndoConsoleRequest>) {
              return session_.undo(session_.state().revision);
            } else {
              return session_.redo(session_.state().revision);
            }
          }();
          if (std::holds_alternative<EditorError>(outcome)) {
            response.editor_error = std::get<EditorError>(std::move(outcome));
          } else {
            response.command_result =
                std::get<CommandResult>(std::move(outcome));
            response.ok = true;
          }
        } else {
          response.kind = ConsoleResultKind::help;
          response.message = help_text(typed_request.command);
          response.ok = true;
        }
      },
      std::move(request));
  response.revision = session_.state().revision;
  return response;
}

std::string render_console_response(const ConsoleResponse& response,
                                    ConsoleRenderFormat format) {
  if (format == ConsoleRenderFormat::text) {
    std::ostringstream output;
    output << (response.ok ? "ok" : "error") << " revision="
           << response.revision;
    if (response.ok) {
      output << " kind=" << kind_name(response.kind);
      if (response.message.has_value()) {
        output << ' ' << *response.message;
      }
      if (response.command_result.has_value() &&
          response.command_result->created_node.has_value()) {
        output << " node=" << *response.command_result->created_node;
      }
      if (response.session_state.has_value()) {
        output << " current=" << response.session_state->current_node
               << " dirty=" << (response.session_state->dirty ? "true" : "false")
               << " undo=" << (response.session_state->can_undo ? "true" : "false")
               << " redo=" << (response.session_state->can_redo ? "true" : "false");
      }
      if (response.graph.has_value()) {
        output << " nodes=" << response.graph->nodes.size()
               << " edges=" << response.graph->edges.size()
               << " truncated=" << (response.graph->truncated ? "true" : "false");
      }
      if (response.node.has_value()) {
        output << " node=" << response.node->summary.id
               << " pieces=" << response.node->position.pieces.size()
               << " annotations=" << response.node->annotations.size();
      }
      if (response.kind == ConsoleResultKind::validation) {
        output << " diagnostics=" << response.validation_issues.size();
      }
    } else if (response.console_error.has_value()) {
      output << " code=" << console_error_name(response.console_error->code)
             << " span=" << response.console_error->span.start << ':'
             << response.console_error->span.length << ' '
             << response.console_error->message;
    } else if (response.editor_error.has_value()) {
      output << " code=" << editor_error_name(response.editor_error->code) << ' '
             << response.editor_error->message;
    }
    return output.str();
  }

  std::string output{"{\"schemaVersion\":"};
  output += std::to_string(response.schema_version) + ",\"requestId\":";
  append_json_string(output, response.request_id);
  output += ",\"ok\":";
  append_json_bool(output, response.ok);
  output += ",\"revision\":";
  append_json_string(output, std::to_string(response.revision));
  if (!response.ok) {
    output += ",\"error\":{";
    if (response.console_error.has_value()) {
      output += "\"code\":";
      append_json_string(output, console_error_name(response.console_error->code));
      output += ",\"message\":";
      append_json_string(output, response.console_error->message);
      output += ",\"span\":{\"start\":" +
                std::to_string(response.console_error->span.start) +
                ",\"length\":" +
                std::to_string(response.console_error->span.length) + "}";
    } else if (response.editor_error.has_value()) {
      output += "\"code\":";
      append_json_string(output, editor_error_name(response.editor_error->code));
      output += ",\"message\":";
      append_json_string(output, response.editor_error->message);
      if (response.editor_error->node_id.has_value()) {
        output += ",\"nodeId\":";
        append_node_id(output, *response.editor_error->node_id);
      }
    } else {
      output += "\"code\":\"console.unknown\",\"message\":\"unknown error\"";
    }
    output.push_back('}');
  } else {
    output += ",\"result\":{\"kind\":";
    append_json_string(output, kind_name(response.kind));
    if (response.session_state.has_value()) {
      output += ",\"state\":";
      append_state(output, *response.session_state);
    }
    if (response.graph.has_value()) {
      output += ",\"graph\":";
      append_graph(output, *response.graph);
    }
    if (response.node.has_value()) {
      output += ",\"node\":{\"summary\":";
      append_summary(output, response.node->summary);
      output += ",\"position\":";
      append_position(output, response.node->position);
      output += ",\"annotations\":";
      append_annotations(output, response.node->annotations);
      output.push_back('}');
    }
    if (response.command_result.has_value()) {
      output += ",\"changes\":";
      append_change_set(output, response.command_result->changes);
      if (response.command_result->created_node.has_value()) {
        output += ",\"nodeId\":";
        append_node_id(output, *response.command_result->created_node);
      }
    }
    if (response.message.has_value()) {
      output += ",\"message\":";
      append_json_string(output, *response.message);
    }
    output.push_back('}');
  }
  output += ",\"diagnostics\":";
  if (!response.ok && response.editor_error.has_value()) {
    append_validation_issues(output, response.editor_error->validation_issues);
  } else {
    append_validation_issues(output, response.validation_issues);
  }
  output.push_back('}');
  return output;
}

}  // namespace oxq::editor
