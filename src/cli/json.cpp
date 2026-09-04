#include "json.hpp"

#include "names.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace oxq::cli {
namespace {

void key(std::string& output, std::string_view name) {
  output += json_string(name);
  output += ':';
}

void separator(std::string& output, bool& first) {
  if (!first) {
    output += ',';
  }
  first = false;
}

void string_value(std::string& output, std::string_view name,
                  const std::optional<std::string>& value, bool& first) {
  separator(output, first);
  key(output, name);
  output += value.has_value() ? json_string(*value) : "null";
}

void integer_value(std::string& output, std::string_view name,
                   const std::optional<std::int32_t>& value, bool& first) {
  separator(output, first);
  key(output, name);
  output += value.has_value() ? std::to_string(*value) : "null";
}

[[nodiscard]] std::string_view side_name(core::Side side) noexcept {
  return side == core::Side::red ? "red" : "black";
}

[[nodiscard]] std::string_view piece_name(core::PieceType piece) noexcept {
  switch (piece) {
    case core::PieceType::king: return "king";
    case core::PieceType::advisor: return "advisor";
    case core::PieceType::elephant: return "elephant";
    case core::PieceType::horse: return "horse";
    case core::PieceType::rook: return "rook";
    case core::PieceType::cannon: return "cannon";
    case core::PieceType::pawn: return "pawn";
  }
  return "unknown";
}

[[nodiscard]] std::string_view result_name(core::GameResult result) noexcept {
  switch (result) {
    case core::GameResult::unknown: return "unknown";
    case core::GameResult::red_win: return "red_win";
    case core::GameResult::black_win: return "black_win";
    case core::GameResult::draw: return "draw";
    case core::GameResult::unfinished: return "unfinished";
    case core::GameResult::aborted: return "aborted";
  }
  return "unknown";
}

[[nodiscard]] std::string_view precision_name(
    core::DatePrecision precision) noexcept {
  switch (precision) {
    case core::DatePrecision::unknown: return "unknown";
    case core::DatePrecision::year: return "year";
    case core::DatePrecision::month: return "month";
    case core::DatePrecision::day: return "day";
    case core::DatePrecision::minute: return "minute";
    case core::DatePrecision::second: return "second";
    case core::DatePrecision::subsecond: return "subsecond";
  }
  return "unknown";
}

[[nodiscard]] std::string player_json(const core::PlayerMetadata& player) {
  std::string output{"{"};
  bool first = true;
  string_value(output, "name", player.name, first);
  string_value(output, "id", player.id, first);
  string_value(output, "country", player.country, first);
  integer_value(output, "rating", player.rating, first);
  string_value(output, "title", player.title, first);
  string_value(output, "team", player.team, first);
  string_value(output, "time_used", player.time_used, first);
  output += '}';
  return output;
}

[[nodiscard]] std::string event_json(const core::EventMetadata& event) {
  std::string output{"{"};
  bool first = true;
  string_value(output, "name", event.name, first);
  string_value(output, "id", event.id, first);
  string_value(output, "location", event.location, first);
  string_value(output, "organizer", event.organizer, first);
  string_value(output, "round", event.round, first);
  string_value(output, "type", event.type, first);
  string_value(output, "group", event.group, first);
  string_value(output, "board_number", event.board_number, first);
  string_value(output, "time_control", event.time_control, first);
  string_value(output, "start_time", event.start_time, first);
  string_value(output, "end_time", event.end_time, first);
  separator(output, first);
  key(output, "date_precision");
  output += event.date_precision.has_value()
                ? json_string(precision_name(*event.date_precision))
                : "null";
  output += '}';
  return output;
}

[[nodiscard]] std::string opening_json(const core::OpeningMetadata& opening) {
  std::string output{"{"};
  bool first = true;
  string_value(output, "name", opening.name, first);
  string_value(output, "code", opening.code, first);
  string_value(output, "id", opening.id, first);
  output += '}';
  return output;
}

[[nodiscard]] std::string provenance_json(const core::Provenance& provenance) {
  std::string output{"{"};
  bool first = true;
  string_value(output, "source_format", provenance.source_format, first);
  string_value(output, "source_record_id", provenance.source_record_id, first);
  string_value(output, "source_uri", provenance.source_uri, first);
  string_value(output, "import_note", provenance.import_note, first);
  string_value(output, "source_format_version",
               provenance.source_format_version, first);
  string_value(output, "source_library_id", provenance.source_library_id,
               first);
  string_value(output, "source_library_name", provenance.source_library_name,
               first);
  string_value(output, "source_category", provenance.source_category, first);
  output += '}';
  return output;
}

[[nodiscard]] std::string extensions_json(
    const core::ExtensionMetadata& extensions) {
  std::string output{"{"};
  bool first_namespace = true;
  for (const auto& [name_space, properties] : extensions) {
    separator(output, first_namespace);
    key(output, name_space);
    output += '{';
    bool first_property = true;
    for (const auto& [name, value] : properties) {
      separator(output, first_property);
      key(output, name);
      if (std::holds_alternative<std::string>(value)) {
        output += json_string(std::get<std::string>(value));
      } else {
        output += '[';
        bool first_item = true;
        for (const auto& item : std::get<std::vector<std::string>>(value)) {
          separator(output, first_item);
          output += json_string(item);
        }
        output += ']';
      }
    }
    output += '}';
  }
  output += '}';
  return output;
}

[[nodiscard]] std::string metadata_json(const core::GameMetadata& metadata) {
  std::string output{"{"};
  output += "\"red_player\":" + player_json(metadata.red_player);
  output += ",\"black_player\":" + player_json(metadata.black_player);
  output += ",\"event\":" + event_json(metadata.event);
  output += ",\"result\":";
  output += metadata.result.has_value()
                ? json_string(result_name(*metadata.result))
                : "null";
  bool first = false;
  string_value(output, "result_text", metadata.result_text, first);
  output += ",\"opening\":" + opening_json(metadata.opening);
  string_value(output, "title", metadata.title, first);
  output += ",\"tags\":[";
  bool first_tag = true;
  for (const auto& tag : metadata.tags) {
    separator(output, first_tag);
    output += json_string(tag);
  }
  output += ']';
  string_value(output, "game_type", metadata.game_type, first);
  string_value(output, "referee", metadata.referee, first);
  string_value(output, "recorder", metadata.recorder, first);
  string_value(output, "commentator", metadata.commentator, first);
  string_value(output, "commentator_uri", metadata.commentator_uri, first);
  string_value(output, "creator", metadata.creator, first);
  string_value(output, "creator_uri", metadata.creator_uri, first);
  string_value(output, "record_created_at", metadata.record_created_at, first);
  string_value(output, "record_modified_at", metadata.record_modified_at,
               first);
  output += ",\"provenance\":" + provenance_json(metadata.provenance);
  output += ",\"extensions\":" + extensions_json(metadata.extensions);
  output += '}';
  return output;
}

[[nodiscard]] std::string position_json(const core::Position& position) {
  std::string output{"{\"side_to_move\":"};
  output += json_string(side_name(position.side_to_move));
  output += ",\"fullmove_number\":" +
            std::to_string(position.fullmove_number) + ",\"pieces\":[";
  bool first = true;
  for (const auto& piece : position.pieces) {
    separator(output, first);
    output += "{\"side\":" + json_string(side_name(piece.side));
    output += ",\"type\":" + json_string(piece_name(piece.type));
    output += ",\"square\":" + std::to_string(piece.square) + '}';
  }
  output += "]}";
  return output;
}

[[nodiscard]] std::string annotation_json(const core::Annotation& annotation) {
  std::string output{"{\"kind\":"};
  output += json_string(annotation.kind == core::AnnotationKind::comment
                            ? "comment"
                            : "source_note");
  output += ",\"before_move\":";
  output += annotation.before_move ? "true" : "false";
  output += ",\"text\":" + json_string(annotation.text);
  bool first = false;
  string_value(output, "author", annotation.author, first);
  string_value(output, "language", annotation.language, first);
  output += '}';
  return output;
}

[[nodiscard]] std::string tree_json(const core::MoveTree& tree) {
  std::string output{"{\"nodes\":["};
  bool first_node = true;
  for (const auto& node : tree.nodes) {
    separator(output, first_node);
    output += "{\"parent\":";
    output += node.parent.has_value() ? std::to_string(*node.parent) : "null";
    output += ",\"move\":";
    if (node.move.has_value()) {
      output += "{\"from\":" + std::to_string(node.move->from_square) +
                ",\"to\":" + std::to_string(node.move->to_square) + '}';
    } else {
      output += "null";
    }
    output += ",\"children\":[";
    bool first_child = true;
    for (const auto child : node.children) {
      separator(output, first_child);
      output += std::to_string(child);
    }
    output += "],\"annotations\":[";
    bool first_annotation = true;
    for (const auto& annotation : node.annotations) {
      separator(output, first_annotation);
      output += annotation_json(annotation);
    }
    output += "]}";
  }
  output += "]}";
  return output;
}

[[nodiscard]] std::string_view validation_severity_name(
    core::ValidationSeverity severity) noexcept {
  return severity == core::ValidationSeverity::error ? "error" : "warning";
}

[[nodiscard]] std::string_view conversion_severity_name(
    convert::ConversionSeverity severity) noexcept {
  return severity == convert::ConversionSeverity::loss ? "loss" : "warning";
}

}  // namespace

std::string json_string(std::string_view value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string output{"\""};
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    switch (byte) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (byte < 0x20U) {
          output += "\\u00";
          output += digits[(byte >> 4U) & 0x0fU];
          output += digits[byte & 0x0fU];
        } else {
          output += static_cast<char>(byte);
        }
    }
  }
  output += '"';
  return output;
}

std::string game_json(const core::GameModel& game) {
  std::string output{"{\"uuid\":"};
  output += json_string(game.uuid.to_string());
  output += ",\"metadata\":" + metadata_json(game.metadata);
  output += ",\"initial_position\":" + position_json(game.initial_position);
  output += ",\"move_tree\":" + tree_json(game.move_tree) + '}';
  return output;
}

std::string games_json(const std::vector<core::GameModel>& games) {
  std::string output{"["};
  bool first = true;
  for (const auto& game : games) {
    separator(output, first);
    output += game_json(game);
  }
  output += ']';
  return output;
}

std::string reader_diagnostics_json(const core::ReaderDiagnostics& diagnostics) {
  return std::string{"{\"canonical\":"} +
         (diagnostics.canonical_ordering() ? "true" : "false") +
         ",\"skipped_unknown_sections\":" +
         std::to_string(diagnostics.skipped_unknown_sections) +
         ",\"skipped_unknown_metadata_fields\":" +
         std::to_string(diagnostics.skipped_unknown_metadata_fields) +
         ",\"skipped_unknown_metadata_value_types\":" +
         std::to_string(diagnostics.skipped_unknown_metadata_value_types) + '}';
}

std::string validation_issues_json(
    const std::vector<core::ValidationIssue>& issues) {
  std::string output{"["};
  bool first = true;
  for (const auto& issue : issues) {
    separator(output, first);
    output += "{\"severity\":" +
              json_string(validation_severity_name(issue.severity));
    output += ",\"code\":" + json_string(name(issue.code));
    output += ",\"path\":" + json_string(issue.path);
    output += ",\"message\":" + json_string(issue.message) + '}';
  }
  output += ']';
  return output;
}

std::string conversion_report_json(const convert::ConversionReport& report) {
  std::string output{"{\"source_games\":"};
  output += std::to_string(report.source_game_count);
  output += ",\"converted_games\":" +
            std::to_string(report.converted_game_count);
  output += ",\"rejected\":";
  output += report.rejected ? "true" : "false";
  output += ",\"diagnostics\":[";
  bool first = true;
  for (const auto& diagnostic : report.diagnostics) {
    separator(output, first);
    output += "{\"severity\":" +
              json_string(conversion_severity_name(diagnostic.severity));
    output += ",\"code\":" + json_string(name(diagnostic.code));
    output += ",\"game_index\":";
    output += diagnostic.game_index.has_value()
                  ? std::to_string(*diagnostic.game_index)
                  : "null";
    output += ",\"physical_slot\":";
    output += diagnostic.physical_slot.has_value()
                  ? std::to_string(*diagnostic.physical_slot)
                  : "null";
    output += ",\"node_index\":";
    output += diagnostic.node_index.has_value()
                  ? std::to_string(*diagnostic.node_index)
                  : "null";
    output += ",\"field\":" + json_string(diagnostic.field);
    output += ",\"message\":" + json_string(diagnostic.message) + '}';
  }
  output += "]}";
  return output;
}

std::string cbl_library_json(const convert::CblLibraryInfo& library) {
  std::string output{"{\"uuid\":"};
  output += json_string(library.uuid.to_string());
  output += ",\"name\":" + json_string(library.name);
  output += ",\"author\":" + json_string(library.author);
  output += ",\"author_email\":" + json_string(library.author_email);
  output += ",\"created_at\":" + json_string(library.created_at);
  output += ",\"modified_at\":" + json_string(library.modified_at);
  output += ",\"directory_capacity\":" +
            std::to_string(library.directory_capacity);
  output += ",\"allocated_resources\":" +
            std::to_string(library.allocated_resource_count);
  output += ",\"live_games\":" +
            std::to_string(library.live_game_count);
  output += ",\"deleted_games\":" +
            std::to_string(library.deleted_game_count);
  output += ",\"live_non_games\":" +
            std::to_string(library.live_non_game_count);
  output += ",\"total_blocks\":" + std::to_string(library.total_blocks);
  output += ",\"trailing_bytes\":" + std::to_string(library.trailing_bytes) +
            '}';
  return output;
}

}  // namespace oxq::cli
