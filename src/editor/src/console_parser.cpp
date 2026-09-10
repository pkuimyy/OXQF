#include <oxq/editor/console.hpp>

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace oxq::editor {
namespace {

[[nodiscard]] ConsoleError error(ConsoleErrorCode code, std::string message,
                                 ConsoleSpan span) {
  return ConsoleError{code, std::move(message), span};
}

[[nodiscard]] bool valid_utf8(std::string_view input) noexcept {
  for (std::size_t index = 0; index < input.size();) {
    const auto first = static_cast<std::uint8_t>(input[index]);
    if (first <= 0x7fU) {
      ++index;
      continue;
    }
    std::size_t count = 0;
    std::uint32_t value = 0;
    if (first >= 0xc2U && first <= 0xdfU) {
      count = 2;
      value = first & 0x1fU;
    } else if (first >= 0xe0U && first <= 0xefU) {
      count = 3;
      value = first & 0x0fU;
    } else if (first >= 0xf0U && first <= 0xf4U) {
      count = 4;
      value = first & 0x07U;
    } else {
      return false;
    }
    if (index + count > input.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset < count; ++offset) {
      const auto continuation =
          static_cast<std::uint8_t>(input[index + offset]);
      if ((continuation & 0xc0U) != 0x80U) {
        return false;
      }
      value = (value << 6U) | (continuation & 0x3fU);
    }
    if ((count == 3 && value < 0x800U) ||
        (count == 4 && value < 0x10000U) ||
        (value >= 0xd800U && value <= 0xdfffU) || value > 0x10ffffU) {
      return false;
    }
    index += count;
  }
  return true;
}

[[nodiscard]] bool ascii_space(char value) noexcept {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

template <typename Integer>
[[nodiscard]] std::optional<Integer> parse_unsigned(std::string_view value) {
  if (value.empty() || value.front() == '+' || value.front() == '-') {
    return std::nullopt;
  }
  Integer result{};
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result, 10);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] std::optional<std::uint8_t> parse_square(
    std::string_view value) {
  if (const auto numeric = parse_unsigned<unsigned int>(value);
      numeric.has_value() && *numeric < 90U) {
    return static_cast<std::uint8_t>(*numeric);
  }
  if (value.size() == 2 && value[0] >= 'a' && value[0] <= 'i' &&
      value[1] >= '0' && value[1] <= '9') {
    return static_cast<std::uint8_t>((value[1] - '0') * 9 +
                                     (value[0] - 'a'));
  }
  return std::nullopt;
}

class Options {
 public:
  Options(const std::vector<ConsoleToken>& tokens, std::size_t first)
      : tokens_(tokens), first_(first) {}

  [[nodiscard]] std::optional<ConsoleError> parse(
      const std::unordered_set<std::string_view>& flags = {}) {
    for (std::size_t index = first_; index < tokens_.size(); ++index) {
      const auto& token = tokens_[index];
      if (!token.value.starts_with("--")) {
        return error(ConsoleErrorCode::invalid_syntax,
                     "expected an option beginning with --", token.span);
      }
      if (values_.contains(token.value)) {
        return error(ConsoleErrorCode::duplicate_option,
                     "option appears more than once", token.span);
      }
      if (flags.contains(token.value)) {
        values_.emplace(token.value, std::nullopt);
        continue;
      }
      if (index + 1 >= tokens_.size() ||
          tokens_[index + 1].value.starts_with("--")) {
        return error(ConsoleErrorCode::missing_option,
                     "option requires a value", token.span);
      }
      values_.emplace(token.value, index + 1);
      ++index;
    }
    return std::nullopt;
  }

  [[nodiscard]] bool contains(std::string_view name) const {
    return values_.contains(std::string{name});
  }

  [[nodiscard]] const ConsoleToken* value(std::string_view name) const {
    const auto found = values_.find(std::string{name});
    if (found == values_.end() || !found->second.has_value()) {
      return nullptr;
    }
    return &tokens_[*found->second];
  }

  [[nodiscard]] std::optional<ConsoleError> reject_unknown(
      const std::unordered_set<std::string_view>& allowed) const {
    for (const auto& [name, index] : values_) {
      if (!allowed.contains(name)) {
        const auto& token = index.has_value() ? tokens_[*index - 1] : tokens_.back();
        return error(ConsoleErrorCode::unknown_option, "unknown option " + name,
                     token.span);
      }
    }
    return std::nullopt;
  }

 private:
  const std::vector<ConsoleToken>& tokens_;
  std::size_t first_;
  std::unordered_map<std::string, std::optional<std::size_t>> values_;
};

[[nodiscard]] ConsoleError syntax(const std::vector<ConsoleToken>& tokens,
                                  std::string message) {
  const auto span = tokens.empty() ? ConsoleSpan{} : tokens.front().span;
  return error(ConsoleErrorCode::invalid_syntax, std::move(message), span);
}

[[nodiscard]] std::variant<format::NodeId, ConsoleError> required_node(
    const Options& options, const std::vector<ConsoleToken>& tokens) {
  const auto* token = options.value("--node");
  if (token == nullptr) {
    return error(ConsoleErrorCode::missing_option,
                 "missing required --node option", tokens.front().span);
  }
  const auto node = parse_unsigned<format::NodeId>(token->value);
  if (!node.has_value()) {
    return error(ConsoleErrorCode::invalid_value, "invalid node id", token->span);
  }
  return *node;
}

}  // namespace

ConsoleTokensOutcome tokenize_console(std::string_view line,
                                      ConsoleLimits limits) {
  if (line.size() > limits.max_line_bytes) {
    return error(ConsoleErrorCode::input_too_long,
                 "console input exceeds the configured byte limit",
                 ConsoleSpan{limits.max_line_bytes,
                             line.size() - limits.max_line_bytes});
  }
  if (!valid_utf8(line)) {
    return error(ConsoleErrorCode::invalid_utf8,
                 "console input is not valid UTF-8",
                 ConsoleSpan{0, line.size()});
  }

  std::vector<ConsoleToken> tokens;
  for (std::size_t index = 0; index < line.size();) {
    while (index < line.size() && ascii_space(line[index])) {
      ++index;
    }
    if (index == line.size() || line[index] == '#') {
      break;
    }
    if (tokens.size() >= limits.max_tokens) {
      return error(ConsoleErrorCode::too_many_tokens,
                   "console input contains too many tokens",
                   ConsoleSpan{index, line.size() - index});
    }
    const auto start = index;
    std::string value;
    if (line[index] == '"') {
      ++index;
      bool closed = false;
      while (index < line.size()) {
        const auto character = line[index++];
        if (character == '"') {
          closed = true;
          break;
        }
        if (character != '\\') {
          value.push_back(character);
          continue;
        }
        if (index == line.size()) {
          return error(ConsoleErrorCode::unterminated_string,
                       "quoted string ends after an escape",
                       ConsoleSpan{start, line.size() - start});
        }
        const auto escaped = line[index++];
        if (escaped == '"' || escaped == '\\') {
          value.push_back(escaped);
        } else if (escaped == 'n') {
          value.push_back('\n');
        } else if (escaped == 't') {
          value.push_back('\t');
        } else {
          return error(ConsoleErrorCode::invalid_escape,
                       "unknown escape sequence", ConsoleSpan{index - 2, 2});
        }
      }
      if (!closed) {
        return error(ConsoleErrorCode::unterminated_string,
                     "quoted string is not terminated",
                     ConsoleSpan{start, line.size() - start});
      }
      if (index < line.size() && !ascii_space(line[index])) {
        return error(ConsoleErrorCode::unexpected_character,
                     "quoted token must be followed by whitespace",
                     ConsoleSpan{index, 1});
      }
    } else {
      while (index < line.size() && !ascii_space(line[index])) {
        value.push_back(line[index++]);
      }
    }
    tokens.push_back(ConsoleToken{std::move(value),
                                  ConsoleSpan{start, index - start}});
  }
  return tokens;
}

ConsoleParseOutcome parse_console(const std::vector<ConsoleToken>& tokens) {
  if (tokens.empty()) {
    return syntax(tokens, "console request is empty");
  }
  const auto& command = tokens[0].value;
  if (command == "status") {
    return tokens.size() == 1
               ? ConsoleParseOutcome{ConsoleRequest{StatusConsoleRequest{}}}
               : ConsoleParseOutcome{syntax(tokens, "status takes no arguments")};
  }
  if (command == "undo" || command == "redo") {
    if (tokens.size() != 1) {
      return syntax(tokens, command + " takes no arguments");
    }
    return command == "undo" ? ConsoleRequest{UndoConsoleRequest{}}
                             : ConsoleRequest{RedoConsoleRequest{}};
  }
  if (command == "help") {
    if (tokens.size() > 2) {
      return syntax(tokens, "help accepts at most one command name");
    }
    return ConsoleRequest{HelpConsoleRequest{
        tokens.size() == 2 ? std::optional{tokens[1].value} : std::nullopt}};
  }
  if (command == "validate") {
    if (tokens.size() == 1) {
      return ConsoleRequest{ValidateConsoleRequest{false}};
    }
    if (tokens.size() == 2 && tokens[1].value == "--state") {
      return ConsoleRequest{ValidateConsoleRequest{true}};
    }
    return syntax(tokens, "validate only accepts --state");
  }
  if (command == "tree") {
    Options options{tokens, 1};
    if (auto parse_error = options.parse(); parse_error.has_value()) {
      return *parse_error;
    }
    if (auto unknown = options.reject_unknown({"--from", "--depth", "--nodes"});
        unknown.has_value()) {
      return *unknown;
    }
    VariationGraphQuery query;
    if (const auto* token = options.value("--from"); token != nullptr) {
      const auto value = parse_unsigned<format::NodeId>(token->value);
      if (!value.has_value()) {
        return error(ConsoleErrorCode::invalid_value, "invalid tree root",
                     token->span);
      }
      query.from = *value;
    }
    for (const auto name : {std::string_view{"--depth"},
                            std::string_view{"--nodes"}}) {
      if (const auto* token = options.value(name); token != nullptr) {
        const auto value = parse_unsigned<std::size_t>(token->value);
        if (!value.has_value()) {
          return error(ConsoleErrorCode::invalid_value,
                       "invalid non-negative limit", token->span);
        }
        (name == "--depth" ? query.max_depth : query.max_nodes) = *value;
      }
    }
    return ConsoleRequest{TreeConsoleRequest{query}};
  }
  if (command == "checkout") {
    Options options{tokens, 1};
    if (auto parse_error = options.parse(); parse_error.has_value()) {
      return *parse_error;
    }
    if (auto unknown = options.reject_unknown({"--node"}); unknown.has_value()) {
      return *unknown;
    }
    auto node = required_node(options, tokens);
    if (std::holds_alternative<ConsoleError>(node)) {
      return std::get<ConsoleError>(std::move(node));
    }
    return ConsoleRequest{CheckoutConsoleRequest{
        std::get<format::NodeId>(node)}};
  }

  if (command == "node" && tokens.size() >= 2 && tokens[1].value == "show") {
    Options options{tokens, 2};
    if (auto parse_error = options.parse(); parse_error.has_value()) {
      return *parse_error;
    }
    if (auto unknown = options.reject_unknown({"--node"}); unknown.has_value()) {
      return *unknown;
    }
    auto node = required_node(options, tokens);
    if (std::holds_alternative<ConsoleError>(node)) {
      return std::get<ConsoleError>(std::move(node));
    }
    return ConsoleRequest{NodeShowConsoleRequest{
        std::get<format::NodeId>(node)}};
  }
  if (command == "node" && tokens.size() >= 2 && tokens[1].value == "delete") {
    Options options{tokens, 2};
    if (auto parse_error = options.parse(); parse_error.has_value()) {
      return *parse_error;
    }
    if (auto unknown = options.reject_unknown({"--node"}); unknown.has_value()) {
      return *unknown;
    }
    auto node = required_node(options, tokens);
    if (std::holds_alternative<ConsoleError>(node)) {
      return std::get<ConsoleError>(std::move(node));
    }
    return ConsoleRequest{ExecuteConsoleRequest{
        DeleteSubtreeCommand{std::get<format::NodeId>(node)}}};
  }

  if (command == "move" && tokens.size() >= 2 &&
      (tokens[1].value == "add" || tokens[1].value == "replace")) {
    const bool adding = tokens[1].value == "add";
    Options options{tokens, 2};
    if (auto parse_error = options.parse(); parse_error.has_value()) {
      return *parse_error;
    }
    const std::unordered_set<std::string_view> allowed =
        adding ? std::unordered_set<std::string_view>{"--parent", "--from",
                                                       "--to", "--index"}
               : std::unordered_set<std::string_view>{"--node", "--from",
                                                       "--to"};
    if (auto unknown = options.reject_unknown(allowed); unknown.has_value()) {
      return *unknown;
    }
    const auto* from_token = options.value("--from");
    const auto* to_token = options.value("--to");
    if (from_token == nullptr || to_token == nullptr) {
      return syntax(tokens, "move requires --from and --to");
    }
    const auto from = parse_square(from_token->value);
    const auto to = parse_square(to_token->value);
    if (!from.has_value() || !to.has_value()) {
      const auto& invalid = !from.has_value() ? *from_token : *to_token;
      return error(ConsoleErrorCode::invalid_value, "invalid board square",
                   invalid.span);
    }
    if (adding) {
      const auto* parent_token = options.value("--parent");
      if (parent_token == nullptr) {
        return syntax(tokens, "move add requires --parent");
      }
      const auto parent = parse_unsigned<format::NodeId>(parent_token->value);
      if (!parent.has_value()) {
        return error(ConsoleErrorCode::invalid_value, "invalid parent node id",
                     parent_token->span);
      }
      std::optional<std::size_t> index;
      if (const auto* index_token = options.value("--index");
          index_token != nullptr) {
        index = parse_unsigned<std::size_t>(index_token->value);
        if (!index.has_value()) {
          return error(ConsoleErrorCode::invalid_value, "invalid sibling index",
                       index_token->span);
        }
      }
      return ConsoleRequest{ExecuteConsoleRequest{
          InsertMoveCommand{*parent, format::Move{*from, *to}, index}}};
    }
    auto node = required_node(options, tokens);
    if (std::holds_alternative<ConsoleError>(node)) {
      return std::get<ConsoleError>(std::move(node));
    }
    return ConsoleRequest{ExecuteConsoleRequest{ReplaceMoveCommand{
        std::get<format::NodeId>(node), format::Move{*from, *to}}}};
  }

  if (command == "branch" && tokens.size() >= 2 &&
      tokens[1].value == "promote") {
    Options options{tokens, 2};
    if (auto parse_error = options.parse(); parse_error.has_value()) {
      return *parse_error;
    }
    if (auto unknown = options.reject_unknown({"--node", "--index"});
        unknown.has_value()) {
      return *unknown;
    }
    auto node = required_node(options, tokens);
    const auto* index_token = options.value("--index");
    if (std::holds_alternative<ConsoleError>(node)) {
      return std::get<ConsoleError>(std::move(node));
    }
    if (index_token == nullptr) {
      return syntax(tokens, "branch promote requires --index");
    }
    const auto index = parse_unsigned<std::size_t>(index_token->value);
    if (!index.has_value()) {
      return error(ConsoleErrorCode::invalid_value, "invalid sibling index",
                   index_token->span);
    }
    return ConsoleRequest{ExecuteConsoleRequest{ReorderVariationCommand{
        std::get<format::NodeId>(node), *index}}};
  }

  if (command == "annotation" && tokens.size() >= 2 &&
      tokens[1].value == "set") {
    Options options{tokens, 2};
    if (auto parse_error = options.parse(); parse_error.has_value()) {
      return *parse_error;
    }
    if (auto unknown = options.reject_unknown(
            {"--node", "--kind", "--text"});
        unknown.has_value()) {
      return *unknown;
    }
    auto node = required_node(options, tokens);
    if (std::holds_alternative<ConsoleError>(node)) {
      return std::get<ConsoleError>(std::move(node));
    }
    const auto* kind_token = options.value("--kind");
    const auto* text_token = options.value("--text");
    if (kind_token == nullptr || text_token == nullptr) {
      return syntax(tokens, "annotation set requires --kind and --text");
    }
    format::Annotation annotation;
    if (kind_token->value == "comment") {
      annotation.kind = format::AnnotationKind::comment;
    } else if (kind_token->value == "source_note") {
      annotation.kind = format::AnnotationKind::source_note;
    } else {
      return error(ConsoleErrorCode::invalid_value, "invalid annotation kind",
                   kind_token->span);
    }
    annotation.text = text_token->value;
    return ConsoleRequest{ExecuteConsoleRequest{SetAnnotationsCommand{
        std::get<format::NodeId>(node), {std::move(annotation)}}}};
  }

  return error(ConsoleErrorCode::unknown_command, "unknown console command",
               tokens.front().span);
}

ConsoleParseOutcome parse_console(std::string_view line, ConsoleLimits limits) {
  auto tokens = tokenize_console(line, limits);
  if (std::holds_alternative<ConsoleError>(tokens)) {
    return std::get<ConsoleError>(std::move(tokens));
  }
  return parse_console(std::get<std::vector<ConsoleToken>>(tokens));
}

}  // namespace oxq::editor
