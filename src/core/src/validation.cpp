#include <oxq/core/validation.hpp>

#include "utf8.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace oxq::core {
namespace {

[[nodiscard]] bool valid_side(Side side) noexcept {
  return side == Side::red || side == Side::black;
}

[[nodiscard]] bool valid_piece_type(PieceType type) noexcept {
  const auto value = static_cast<std::uint8_t>(type);
  return value >= static_cast<std::uint8_t>(PieceType::king) &&
         value <= static_cast<std::uint8_t>(PieceType::pawn);
}

[[nodiscard]] bool valid_annotation_kind(AnnotationKind kind) noexcept {
  return kind == AnnotationKind::comment || kind == AnnotationKind::source_note;
}

[[nodiscard]] bool valid_game_result(GameResult result) noexcept {
  const auto value = static_cast<std::uint8_t>(result);
  return value <= static_cast<std::uint8_t>(GameResult::aborted);
}

[[nodiscard]] bool valid_date_precision(DatePrecision precision) noexcept {
  const auto value = static_cast<std::uint8_t>(precision);
  return value <= static_cast<std::uint8_t>(DatePrecision::subsecond);
}

[[nodiscard]] bool ascii_digits(std::string_view text, std::size_t offset,
                                std::size_t length) noexcept {
  if (offset > text.size() || length > text.size() - offset) {
    return false;
  }
  for (std::size_t index = offset; index < offset + length; ++index) {
    if (text[index] < '0' || text[index] > '9') {
      return false;
    }
  }
  return true;
}

[[nodiscard]] unsigned decimal(std::string_view text, std::size_t offset,
                               std::size_t length) noexcept {
  unsigned value = 0;
  for (std::size_t index = offset; index < offset + length; ++index) {
    value = value * 10U + static_cast<unsigned>(text[index] - '0');
  }
  return value;
}

[[nodiscard]] bool leap_year(unsigned year) noexcept {
  return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

[[nodiscard]] bool valid_calendar_date(std::string_view text) noexcept {
  if (text.size() < 10 || !ascii_digits(text, 0, 4) || text[4] != '-' ||
      !ascii_digits(text, 5, 2) || text[7] != '-' || !ascii_digits(text, 8, 2)) {
    return false;
  }
  const unsigned year = decimal(text, 0, 4);
  const unsigned month = decimal(text, 5, 2);
  const unsigned day = decimal(text, 8, 2);
  if (month == 0 || month > 12 || day == 0) {
    return false;
  }
  constexpr std::array<unsigned, 12> days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const unsigned limit = month == 2 && leap_year(year) ? 29U : days[month - 1];
  return day <= limit;
}

[[nodiscard]] bool valid_zone(std::string_view zone) noexcept {
  if (zone.empty() || zone == "Z") {
    return true;
  }
  if (zone.size() != 6 || (zone[0] != '+' && zone[0] != '-') || zone[3] != ':' ||
      !ascii_digits(zone, 1, 2) || !ascii_digits(zone, 4, 2)) {
    return false;
  }
  return decimal(zone, 1, 2) <= 23U && decimal(zone, 4, 2) <= 59U;
}

[[nodiscard]] std::optional<DatePrecision> date_time_precision(std::string_view text) noexcept {
  if (text.size() == 4 && ascii_digits(text, 0, 4)) {
    return DatePrecision::year;
  }
  if (text.size() == 7 && ascii_digits(text, 0, 4) && text[4] == '-' &&
      ascii_digits(text, 5, 2) && decimal(text, 5, 2) >= 1U &&
      decimal(text, 5, 2) <= 12U) {
    return DatePrecision::month;
  }
  if (!valid_calendar_date(text)) {
    return std::nullopt;
  }
  if (text.size() == 10) {
    return DatePrecision::day;
  }
  if (text.size() < 16 || text[10] != 'T' || !ascii_digits(text, 11, 2) || text[13] != ':' ||
      !ascii_digits(text, 14, 2) || decimal(text, 11, 2) > 23U ||
      decimal(text, 14, 2) > 59U) {
    return std::nullopt;
  }

  std::size_t cursor = 16;
  DatePrecision precision = DatePrecision::minute;
  if (cursor < text.size() && text[cursor] == ':') {
    if (!ascii_digits(text, cursor + 1, 2) || decimal(text, cursor + 1, 2) > 59U) {
      return std::nullopt;
    }
    cursor += 3;
    precision = DatePrecision::second;
    if (cursor < text.size() && text[cursor] == '.') {
      const std::size_t fraction_start = ++cursor;
      while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
        ++cursor;
      }
      if (cursor == fraction_start) {
        return std::nullopt;
      }
      precision = DatePrecision::subsecond;
    }
  }
  return valid_zone(text.substr(cursor)) ? std::optional<DatePrecision>{precision} : std::nullopt;
}

[[nodiscard]] bool valid_namespace(std::string_view value) noexcept {
  if (value.empty() || value.front() == '.' || value.back() == '.' || value.find('.') == value.npos) {
    return false;
  }
  bool at_segment_start = true;
  char previous = '\0';
  for (const char character : value) {
    if (character == '.') {
      if (at_segment_start || previous == '-') {
        return false;
      }
      at_segment_start = true;
    } else {
      const bool letter = character >= 'a' && character <= 'z';
      const bool digit = character >= '0' && character <= '9';
      if ((!letter && !digit && character != '-') || (at_segment_start && !letter)) {
        return false;
      }
      at_segment_start = false;
    }
    previous = character;
  }
  return !at_segment_start && previous != '-';
}

[[nodiscard]] bool valid_extension_key(std::string_view value) noexcept {
  if (value.empty() || value.front() < 'a' || value.front() > 'z') {
    return false;
  }
  return std::ranges::all_of(value, [](char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '_';
  });
}

}  // namespace

std::vector<ValidationIssue> validate(const GameModel& game, const ValidationLimits& limits) {
  std::vector<ValidationIssue> issues;
  const auto report = [&issues](ValidationCode code, std::string path, std::string message) {
    issues.push_back({ValidationSeverity::error, code, std::move(path), std::move(message)});
  };
  const auto warn = [&issues](ValidationCode code, std::string path, std::string message) {
    issues.push_back({ValidationSeverity::warning, code, std::move(path), std::move(message)});
  };

  if (game.uuid.is_nil()) {
    report(ValidationCode::nil_uuid, "uuid", "game UUID must not be nil");
  } else if ((game.uuid.bytes[8] & 0xc0U) != 0x80U) {
    report(ValidationCode::invalid_uuid_variant, "uuid", "game UUID must use the RFC 9562 variant");
  }
  if (!valid_side(game.initial_position.side_to_move)) {
    report(ValidationCode::invalid_side, "initial_position.side_to_move", "side must be red or black");
  }
  if (game.initial_position.fullmove_number == 0) {
    report(ValidationCode::invalid_fullmove_number, "initial_position.fullmove_number",
           "fullmove number must start at 1");
  }
  if (game.initial_position.pieces.size() > limits.max_pieces) {
    report(ValidationCode::too_many_pieces, "initial_position.pieces", "piece limit exceeded");
  }

  std::array<bool, 90> occupied{};
  for (std::size_t index = 0; index < game.initial_position.pieces.size(); ++index) {
    const auto& piece = game.initial_position.pieces[index];
    const std::string path = "initial_position.pieces[" + std::to_string(index) + "]";
    if (!valid_side(piece.side)) {
      report(ValidationCode::invalid_side, path + ".side", "piece side must be red or black");
    }
    if (!valid_piece_type(piece.type)) {
      report(ValidationCode::invalid_piece_type, path + ".type", "piece type is outside the model domain");
    }
    if (piece.square >= occupied.size()) {
      report(ValidationCode::invalid_square, path + ".square", "square must be in 0..89");
    } else if (occupied[piece.square]) {
      report(ValidationCode::duplicate_square, path + ".square", "multiple pieces occupy the same square");
    } else {
      occupied[piece.square] = true;
    }
  }

  const auto& nodes = game.move_tree.nodes;
  if (nodes.empty()) {
    report(ValidationCode::empty_tree, "move_tree.nodes", "move tree must contain a root node");
  } else if (nodes.size() > limits.max_nodes) {
    report(ValidationCode::too_many_nodes, "move_tree.nodes", "node limit exceeded");
  } else {
    if (nodes[0].parent.has_value() || nodes[0].move.has_value()) {
      report(ValidationCode::invalid_root, "move_tree.nodes[0]", "root must not have a parent or move");
    }
    for (std::size_t annotation = 0; annotation < nodes[0].annotations.size(); ++annotation) {
      if (nodes[0].annotations[annotation].before_move) {
        report(ValidationCode::invalid_annotation,
               "move_tree.nodes[0].annotations[" + std::to_string(annotation) + "].before_move",
               "a root annotation cannot be before a move");
      }
    }

    std::vector<std::size_t> incoming(nodes.size(), 0);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      const auto& node = nodes[index];
      const std::string path = "move_tree.nodes[" + std::to_string(index) + "]";
      if (index != 0) {
        if (!node.parent.has_value() || *node.parent >= nodes.size()) {
          report(ValidationCode::invalid_parent, path + ".parent", "non-root node needs a valid parent");
        }
        if (!node.move.has_value()) {
          report(ValidationCode::invalid_move, path + ".move", "non-root node needs a move");
        }
      }
      if (node.move.has_value() &&
          (node.move->from_square >= 90 || node.move->to_square >= 90)) {
        report(ValidationCode::invalid_move, path + ".move", "move squares must be in 0..89");
      } else if (node.move.has_value() && node.move->from_square == node.move->to_square) {
        report(ValidationCode::same_square_move, path + ".move",
               "move origin and destination must differ");
      }

      std::vector<std::size_t> local_children;
      local_children.reserve(node.children.size());
      for (std::size_t child_position = 0; child_position < node.children.size(); ++child_position) {
        const auto child = node.children[child_position];
        if (child >= nodes.size()) {
          report(ValidationCode::invalid_child,
                 path + ".children[" + std::to_string(child_position) + "]",
                 "child index is out of range");
          continue;
        }
        if (std::ranges::find(local_children, child) != local_children.end()) {
          report(ValidationCode::duplicate_child,
                 path + ".children[" + std::to_string(child_position) + "]",
                 "a child cannot appear twice under one parent");
        } else {
          local_children.push_back(child);
        }
        ++incoming[child];
        if (!nodes[child].parent.has_value() || *nodes[child].parent != index) {
          report(ValidationCode::invalid_parent, "move_tree.nodes[" + std::to_string(child) + "].parent",
                 "parent does not agree with the ordered child list");
        }
      }
    }

    for (std::size_t index = 1; index < incoming.size(); ++index) {
      if (incoming[index] != 1) {
        report(ValidationCode::invalid_parent, "move_tree.nodes[" + std::to_string(index) + "]",
               "every non-root node must occur in exactly one child list");
      }
    }
    if (incoming[0] != 0) {
      report(ValidationCode::invalid_root, "move_tree.nodes[0]", "root cannot occur in a child list");
    }

    struct Frame {
      std::size_t node;
      std::size_t next_child;
      std::size_t depth;
    };
    std::vector<std::uint8_t> color(nodes.size(), 0);
    std::vector<Frame> stack{{0, 0, 0}};
    color[0] = 1;
    bool depth_reported = false;
    while (!stack.empty()) {
      auto& frame = stack.back();
      if (frame.depth > limits.max_tree_depth && !depth_reported) {
        report(ValidationCode::tree_too_deep, "move_tree", "tree depth limit exceeded");
        depth_reported = true;
      }
      if (frame.next_child >= nodes[frame.node].children.size()) {
        color[frame.node] = 2;
        stack.pop_back();
        continue;
      }
      const auto child = nodes[frame.node].children[frame.next_child++];
      if (child >= nodes.size()) {
        continue;
      }
      if (color[child] == 1) {
        report(ValidationCode::tree_cycle, "move_tree.nodes[" + std::to_string(child) + "]",
               "ordered child graph contains a cycle");
      } else if (color[child] == 0) {
        const auto child_depth = frame.depth + 1;
        color[child] = 1;
        stack.push_back({child, 0, child_depth});
      }
    }
    for (std::size_t index = 0; index < color.size(); ++index) {
      if (color[index] == 0) {
        report(ValidationCode::unreachable_node, "move_tree.nodes[" + std::to_string(index) + "]",
               "node is not reachable from the root");
      }
    }
  }

  std::size_t annotation_count = 0;
  for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index) {
    const auto& node = nodes[node_index];
    if (node.annotations.size() > limits.max_annotations -
                                      std::min(annotation_count, limits.max_annotations)) {
      report(ValidationCode::too_many_annotations, "move_tree", "annotation limit exceeded");
      annotation_count = limits.max_annotations;
    } else {
      annotation_count += node.annotations.size();
    }
  }

  const auto validate_text = [&report, &limits](std::string_view text, std::string path) {
    if (text.size() > limits.max_string_bytes) {
      report(ValidationCode::string_too_long, path, "UTF-8 string byte limit exceeded");
    } else if (!detail::valid_utf8(text)) {
      report(ValidationCode::invalid_utf8, path, "text is not valid shortest-form UTF-8");
    }
  };
  const auto validate_optional = [&validate_text](const std::optional<std::string>& value,
                                                   std::string path) {
    if (value.has_value()) {
      validate_text(*value, std::move(path));
    }
  };

  const auto validate_player = [&validate_optional](const PlayerMetadata& player, std::string path) {
    validate_optional(player.name, path + ".name");
    validate_optional(player.id, path + ".id");
    validate_optional(player.country, path + ".country");
    validate_optional(player.title, path + ".title");
    validate_optional(player.team, path + ".team");
    validate_optional(player.time_used, path + ".time_used");
  };
  validate_player(game.metadata.red_player, "metadata.red_player");
  validate_player(game.metadata.black_player, "metadata.black_player");

  const auto& event = game.metadata.event;
  validate_optional(event.name, "metadata.event.name");
  validate_optional(event.id, "metadata.event.id");
  validate_optional(event.location, "metadata.event.location");
  validate_optional(event.organizer, "metadata.event.organizer");
  validate_optional(event.round, "metadata.event.round");
  validate_optional(event.type, "metadata.event.type");
  validate_optional(event.group, "metadata.event.group");
  validate_optional(event.board_number, "metadata.event.board_number");
  validate_optional(event.time_control, "metadata.event.time_control");
  validate_optional(event.start_time, "metadata.event.start_time");
  validate_optional(event.end_time, "metadata.event.end_time");
  validate_optional(game.metadata.result_text, "metadata.result_text");
  validate_optional(game.metadata.opening.name, "metadata.opening.name");
  validate_optional(game.metadata.opening.code, "metadata.opening.code");
  validate_optional(game.metadata.opening.id, "metadata.opening.id");
  validate_optional(game.metadata.title, "metadata.title");
  for (std::size_t index = 0; index < game.metadata.tags.size(); ++index) {
    validate_text(game.metadata.tags[index], "metadata.tags[" + std::to_string(index) + "]");
  }
  validate_optional(game.metadata.game_type, "metadata.game_type");
  validate_optional(game.metadata.referee, "metadata.referee");
  validate_optional(game.metadata.recorder, "metadata.recorder");
  validate_optional(game.metadata.commentator, "metadata.commentator");
  validate_optional(game.metadata.commentator_uri, "metadata.commentator_uri");
  validate_optional(game.metadata.creator, "metadata.creator");
  validate_optional(game.metadata.creator_uri, "metadata.creator_uri");
  validate_optional(game.metadata.record_created_at, "metadata.record_created_at");
  validate_optional(game.metadata.record_modified_at, "metadata.record_modified_at");

  const auto& provenance = game.metadata.provenance;
  validate_optional(provenance.source_format, "metadata.provenance.source_format");
  validate_optional(provenance.source_record_id, "metadata.provenance.source_record_id");
  validate_optional(provenance.source_uri, "metadata.provenance.source_uri");
  validate_optional(provenance.import_note, "metadata.provenance.import_note");
  validate_optional(provenance.source_format_version, "metadata.provenance.source_format_version");
  validate_optional(provenance.source_library_id, "metadata.provenance.source_library_id");
  validate_optional(provenance.source_library_name, "metadata.provenance.source_library_name");
  validate_optional(provenance.source_category, "metadata.provenance.source_category");

  for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index) {
    for (std::size_t index = 0; index < nodes[node_index].annotations.size(); ++index) {
      const auto& annotation = nodes[node_index].annotations[index];
      const std::string path = "move_tree.nodes[" + std::to_string(node_index) + "].annotations[" +
                               std::to_string(index) + "]";
      if (!valid_annotation_kind(annotation.kind)) {
        report(ValidationCode::invalid_annotation_kind, path + ".kind",
               "annotation kind is outside the model domain");
      }
      validate_text(annotation.text, path + ".text");
      validate_optional(annotation.author, path + ".author");
      validate_optional(annotation.language, path + ".language");
    }
  }

  if (game.metadata.result.has_value() && !valid_game_result(*game.metadata.result)) {
    report(ValidationCode::invalid_game_result, "metadata.result",
           "game result is outside the model domain");
  }

  const bool has_date = event.start_time.has_value() || event.end_time.has_value();
  if (has_date && !event.date_precision.has_value()) {
    report(ValidationCode::missing_date_precision, "metadata.event.date_precision",
           "date precision is required when a start or end time is present");
  } else if (!has_date && event.date_precision.has_value()) {
    report(ValidationCode::invalid_date_precision, "metadata.event.date_precision",
           "date precision must be absent when both date fields are absent");
  } else if (event.date_precision.has_value() && !valid_date_precision(*event.date_precision)) {
    report(ValidationCode::invalid_date_precision, "metadata.event.date_precision",
           "date precision is outside the model domain");
  } else if (event.date_precision.has_value()) {
    const auto validate_date = [&report, &event](const std::optional<std::string>& value,
                                                 std::string path) {
      if (!value.has_value()) {
        return;
      }
      const auto actual = date_time_precision(*value);
      if (!actual.has_value() ||
          (*event.date_precision != DatePrecision::unknown && *actual != *event.date_precision)) {
        report(ValidationCode::invalid_date_time, std::move(path),
               "date/time is invalid or does not match date_precision");
      }
    };
    validate_date(event.start_time, "metadata.event.start_time");
    validate_date(event.end_time, "metadata.event.end_time");
  }

  for (std::size_t index = 0; index < game.metadata.tags.size(); ++index) {
    if (std::ranges::find(game.metadata.tags.begin(), game.metadata.tags.begin() +
                                                           static_cast<std::ptrdiff_t>(index),
                          game.metadata.tags[index]) !=
        game.metadata.tags.begin() + static_cast<std::ptrdiff_t>(index)) {
      warn(ValidationCode::duplicate_tag, "metadata.tags[" + std::to_string(index) + "]",
           "duplicate tags are removed by canonical serialization");
    }
  }

  for (const auto& [name_space, properties] : game.metadata.extensions) {
    const std::string path = "metadata.extensions[" + name_space + "]";
    if (!valid_namespace(name_space)) {
      report(ValidationCode::invalid_extension_namespace, path,
             "namespace must be a lowercase reverse-domain name");
    }
    if (properties.empty()) {
      report(ValidationCode::empty_extension_namespace, path, "namespace object must not be empty");
    }
    for (const auto& [key, value] : properties) {
      const std::string value_path = path + "[" + key + "]";
      if (!valid_extension_key(key)) {
        report(ValidationCode::invalid_extension_key, value_path,
               "extension key must be lowercase ASCII snake_case");
      }
      std::visit(
          [&validate_text, &report, &value_path](const auto& item) {
            using Value = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Value, std::string>) {
              validate_text(item, value_path);
            } else {
              if (item.empty()) {
                report(ValidationCode::empty_extension_array, value_path,
                       "extension string array must not be empty");
              }
              for (std::size_t index = 0; index < item.size(); ++index) {
                validate_text(item[index], value_path + "[" + std::to_string(index) + "]");
              }
            }
          },
          value);
    }
  }

  return issues;
}

bool has_errors(const std::vector<ValidationIssue>& issues) noexcept {
  return std::ranges::any_of(issues, [](const ValidationIssue& issue) {
    return issue.severity == ValidationSeverity::error;
  });
}

}  // namespace oxq::core
