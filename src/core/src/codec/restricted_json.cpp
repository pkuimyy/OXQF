#include "codec/restricted_json.hpp"

#include "extension_name.hpp"
#include "utf8.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace oxq::core::detail {
namespace {

[[nodiscard]] int hex_value(char character) noexcept {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

void append_utf8(std::string& output, std::uint32_t code_point) {
  if (code_point <= 0x7fU) {
    output.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7ffU) {
    output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else if (code_point <= 0xffffU) {
    output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else {
    output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  }
}

class Parser {
 public:
  explicit Parser(std::string_view input) : input_(input) {}

  [[nodiscard]] RestrictedJsonResult parse() {
    ExtensionMetadata result;
    skip_space();
    if (!consume('{')) {
      return failure("top-level value must be an object");
    }
    skip_space();
    if (peek('}')) {
      return failure("top-level object must not be empty");
    }
    while (!failed()) {
      std::string name_space;
      if (!parse_string(name_space) || !valid_extension_namespace(name_space)) {
        return failed() ? failure() : failure("invalid extension namespace");
      }
      if (result.contains(name_space)) {
        return failure("duplicate extension namespace");
      }
      if (!consume(':')) {
        return failure("expected ':' after namespace");
      }
      ExtensionProperties properties;
      if (!parse_properties(properties)) {
        return failure();
      }
      result.emplace(std::move(name_space), std::move(properties));
      skip_space();
      if (consume('}')) {
        break;
      }
      if (!consume(',')) {
        return failure("expected ',' or '}' after namespace object");
      }
    }
    skip_space();
    if (position_ != input_.size()) {
      return failure("trailing data after top-level object");
    }
    const std::string canonical = serialize_restricted_metadata_json(result);
    return RestrictedJsonDocument{std::move(result), canonical == input_};
  }

 private:
  [[nodiscard]] bool parse_properties(ExtensionProperties& output) {
    if (!consume('{')) {
      set_error("namespace value must be an object");
      return false;
    }
    skip_space();
    if (peek('}')) {
      set_error("namespace object must not be empty");
      return false;
    }
    while (!failed()) {
      std::string key;
      if (!parse_string(key) || !valid_extension_key(key)) {
        if (!failed()) {
          set_error("invalid extension property key");
        }
        return false;
      }
      if (output.contains(key)) {
        set_error("duplicate extension property key");
        return false;
      }
      if (!consume(':')) {
        set_error("expected ':' after extension property key");
        return false;
      }
      skip_space();
      if (peek('"')) {
        std::string value;
        if (!parse_string(value)) {
          return false;
        }
        output.emplace(std::move(key), std::move(value));
      } else if (peek('[')) {
        std::vector<std::string> values;
        if (!parse_array(values)) {
          return false;
        }
        output.emplace(std::move(key), std::move(values));
      } else {
        set_error("extension value must be a string or non-empty string array");
        return false;
      }
      skip_space();
      if (consume('}')) {
        return true;
      }
      if (!consume(',')) {
        set_error("expected ',' or '}' after extension property");
        return false;
      }
    }
    return false;
  }

  [[nodiscard]] bool parse_array(std::vector<std::string>& output) {
    if (!consume('[')) {
      set_error("expected string array");
      return false;
    }
    skip_space();
    if (peek(']')) {
      set_error("extension string array must not be empty");
      return false;
    }
    while (!failed()) {
      std::string value;
      if (!parse_string(value)) {
        return false;
      }
      output.push_back(std::move(value));
      skip_space();
      if (consume(']')) {
        return true;
      }
      if (!consume(',')) {
        set_error("expected ',' or ']' in string array");
        return false;
      }
    }
    return false;
  }

  [[nodiscard]] bool parse_string(std::string& output) {
    skip_space();
    if (position_ >= input_.size() || input_[position_] != '"') {
      set_error("expected JSON string");
      return false;
    }
    ++position_;
    while (position_ < input_.size()) {
      const auto character = static_cast<unsigned char>(input_[position_++]);
      if (character == '"') {
        return true;
      }
      if (character < 0x20U) {
        set_error("unescaped control character in JSON string");
        return false;
      }
      if (character != '\\') {
        output.push_back(static_cast<char>(character));
        continue;
      }
      if (position_ >= input_.size()) {
        set_error("truncated JSON escape");
        return false;
      }
      const char escape = input_[position_++];
      switch (escape) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
          const auto first = parse_hex_quad();
          if (!first.has_value()) {
            return false;
          }
          std::uint32_t code_point = *first;
          if (code_point >= 0xd800U && code_point <= 0xdbffU) {
            if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                input_[position_ + 1] != 'u') {
              set_error("high surrogate must be followed by a low surrogate");
              return false;
            }
            position_ += 2;
            const auto second = parse_hex_quad();
            if (!second.has_value() || *second < 0xdc00U || *second > 0xdfffU) {
              set_error("invalid low surrogate");
              return false;
            }
            code_point = 0x10000U + ((code_point - 0xd800U) << 10U) + (*second - 0xdc00U);
          } else if (code_point >= 0xdc00U && code_point <= 0xdfffU) {
            set_error("unpaired low surrogate");
            return false;
          }
          append_utf8(output, code_point);
          break;
        }
        default:
          set_error("unknown JSON escape");
          return false;
      }
    }
    set_error("unterminated JSON string");
    return false;
  }

  [[nodiscard]] std::optional<std::uint32_t> parse_hex_quad() {
    if (position_ + 4 > input_.size()) {
      set_error("truncated Unicode escape");
      return std::nullopt;
    }
    std::uint32_t result = 0;
    for (unsigned index = 0; index < 4; ++index) {
      const int value = hex_value(input_[position_ + index]);
      if (value < 0) {
        set_error("invalid Unicode escape");
        return std::nullopt;
      }
      result = (result << 4U) | static_cast<std::uint32_t>(value);
    }
    position_ += 4;
    return result;
  }

  void skip_space() noexcept {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\t' ||
            input_[position_] == '\n' || input_[position_] == '\r')) {
      ++position_;
    }
  }

  [[nodiscard]] bool peek(char expected) {
    skip_space();
    return position_ < input_.size() && input_[position_] == expected;
  }

  [[nodiscard]] bool consume(char expected) {
    skip_space();
    if (position_ >= input_.size() || input_[position_] != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  void set_error(std::string message) {
    if (!error_.has_value()) {
      error_ = RestrictedJsonError{position_, std::move(message)};
    }
  }

  [[nodiscard]] bool failed() const noexcept { return error_.has_value(); }

  [[nodiscard]] RestrictedJsonError failure(std::string message = {}) {
    if (!message.empty()) {
      set_error(std::move(message));
    }
    return error_.value_or(RestrictedJsonError{position_, "invalid restricted JSON"});
  }

  std::string_view input_;
  std::size_t position_{};
  std::optional<RestrictedJsonError> error_;
};

void append_json_string(std::string& output, std::string_view value) {
  constexpr std::string_view hex = "0123456789abcdef";
  output.push_back('"');
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case 0x08: output += "\\b"; break;
      case 0x09: output += "\\t"; break;
      case 0x0a: output += "\\n"; break;
      case 0x0c: output += "\\f"; break;
      default:
        if (character < 0x20U) {
          output += "\\u00";
          output.push_back(hex[character >> 4U]);
          output.push_back(hex[character & 0x0fU]);
        } else {
          output.push_back(static_cast<char>(character));
        }
    }
  }
  output.push_back('"');
}

}  // namespace

RestrictedJsonResult parse_restricted_metadata_json(std::string_view input, std::size_t max_bytes) {
  if (input.size() > max_bytes) {
    return RestrictedJsonError{0, "extended metadata JSON byte limit exceeded"};
  }
  if (const auto invalid = first_invalid_utf8(input); invalid.has_value()) {
    return RestrictedJsonError{*invalid, "extended metadata JSON is not valid UTF-8"};
  }
  return Parser{input}.parse();
}

std::string serialize_restricted_metadata_json(const ExtensionMetadata& value) {
  std::string output;
  output.push_back('{');
  bool first_namespace = true;
  for (const auto& [name_space, properties] : value) {
    if (!first_namespace) {
      output.push_back(',');
    }
    first_namespace = false;
    append_json_string(output, name_space);
    output += ":{";
    bool first_property = true;
    for (const auto& [key, property] : properties) {
      if (!first_property) {
        output.push_back(',');
      }
      first_property = false;
      append_json_string(output, key);
      output.push_back(':');
      std::visit(
          [&output](const auto& item) {
            using Value = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Value, std::string>) {
              append_json_string(output, item);
            } else {
              output.push_back('[');
              for (std::size_t index = 0; index < item.size(); ++index) {
                if (index != 0) {
                  output.push_back(',');
                }
                append_json_string(output, item[index]);
              }
              output.push_back(']');
            }
          },
          property);
    }
    output.push_back('}');
  }
  output.push_back('}');
  return output;
}

}  // namespace oxq::core::detail
