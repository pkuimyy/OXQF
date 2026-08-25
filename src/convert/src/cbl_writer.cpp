#include <oxq/convert/cbl_writer.hpp>

#include "cbl/hash.hpp"

#include <oxq/core/validation.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace oxq::convert {
namespace {

constexpr std::size_t kDirectoryOffset = 0x10440;
constexpr std::size_t kDirectoryEntrySize = 0x114;
constexpr std::size_t kRecordPrefixSize = 0x8aa;
constexpr std::size_t kBlockSize = 4096;
constexpr std::string_view kCblExtension = "org.openxiangqi.cbl";
constexpr std::string_view kWriterUuidNamespace =
    "8e4fd752-6a26-5a9b-b295-0f2e0973c943";

struct PreflightState {
  CblWritePlan plan;
  bool fatal{};
};

[[nodiscard]] CblWriteError write_error(
    CblWriteErrorCode code, std::string field, std::string message,
    std::optional<std::uint64_t> expected = {},
    std::optional<std::uint64_t> actual = {}) {
  return {code, {}, {}, std::move(field), std::move(message), expected, actual};
}

void diagnostic(PreflightState& state, ConversionSeverity severity,
                ConversionCode code, std::size_t game_index,
                std::optional<std::size_t> node_index, std::string field,
                std::string message) {
  state.plan.report.diagnostics.push_back(
      {severity, code, game_index, {}, node_index, std::move(field),
       std::move(message)});
}

void unsupported(PreflightState& state, std::size_t game_index,
                 std::string field, std::string message) {
  diagnostic(state, ConversionSeverity::loss,
             ConversionCode::cbl_write_metadata_unsupported, game_index, {},
             std::move(field), std::move(message));
}

[[nodiscard]] bool checked_add(std::size_t left, std::size_t right,
                               std::size_t& result) noexcept {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] bool checked_multiply(std::size_t left, std::size_t right,
                                    std::size_t& result) noexcept {
  if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

struct Utf16Length {
  bool valid{};
  std::size_t units{};
};

[[nodiscard]] Utf16Length utf16_length(std::string_view value) noexcept {
  std::size_t units = 0;
  for (std::size_t index = 0; index < value.size();) {
    const auto first = static_cast<unsigned char>(value[index]);
    if (first < 0x80U) {
      ++index;
      ++units;
    } else {
      std::size_t length = 0;
      std::uint32_t code_point = 0;
      std::uint32_t minimum = 0;
      if ((first & 0xe0U) == 0xc0U) {
        length = 2;
        code_point = first & 0x1fU;
        minimum = 0x80U;
      } else if ((first & 0xf0U) == 0xe0U) {
        length = 3;
        code_point = first & 0x0fU;
        minimum = 0x800U;
      } else if ((first & 0xf8U) == 0xf0U) {
        length = 4;
        code_point = first & 0x07U;
        minimum = 0x10000U;
      } else {
        return {};
      }
      if (length > value.size() - index) {
        return {};
      }
      for (std::size_t continuation = 1; continuation < length; ++continuation) {
        const auto byte = static_cast<unsigned char>(value[index + continuation]);
        if ((byte & 0xc0U) != 0x80U) {
          return {};
        }
        code_point = (code_point << 6U) | (byte & 0x3fU);
      }
      if (code_point < minimum || code_point > 0x10ffffU ||
          (code_point >= 0xd800U && code_point <= 0xdfffU)) {
        return {};
      }
      index += length;
      units += code_point > 0xffffU ? 2U : 1U;
    }
  }
  return {true, units};
}

void fixed_text(PreflightState& state, std::size_t game_index,
                std::string_view field, const std::optional<std::string>& value,
                std::size_t slot_bytes) {
  if (!value.has_value()) {
    return;
  }
  if (value->empty()) {
    unsupported(state, game_index, std::string{field},
                "CBL fixed text slots cannot preserve present-but-empty metadata");
    return;
  }
  const auto length = utf16_length(*value);
  if (!length.valid) {
    return;
  }
  const auto maximum = slot_bytes / 2U - 1U;
  if (length.units > maximum) {
    diagnostic(state, ConversionSeverity::loss,
               ConversionCode::cbl_write_text_too_long, game_index, {},
               std::string{field},
               "text exceeds the target CBL UTF-16 fixed slot");
    state.fatal = true;
  }
}

void plain_fixed_text(PreflightState& state, std::string_view field,
                      const std::string& value, std::size_t slot_bytes) {
  const auto length = utf16_length(value);
  if (!length.valid) {
    state.plan.report.diagnostics.push_back(
        {ConversionSeverity::loss,
         ConversionCode::cbl_write_invalid_game_model,
         {}, {}, {}, std::string{field},
         "library text is not valid shortest-form UTF-8"});
    state.fatal = true;
    return;
  }
  if (length.units > slot_bytes / 2U - 1U) {
    state.plan.report.diagnostics.push_back(
        {ConversionSeverity::loss,
         ConversionCode::cbl_write_text_too_long,
         {}, {}, {}, std::string{field},
         "library text exceeds the target CBL UTF-16 fixed slot"});
    state.fatal = true;
  }
}

template <typename T>
void unsupported_optional(PreflightState& state, std::size_t game_index,
                          std::string_view field,
                          const std::optional<T>& value) {
  if (value.has_value()) {
    unsupported(state, game_index, std::string{field},
                "metadata field has no CBL v3 representation");
  }
}

void check_metadata(PreflightState& state, const core::GameModel& game,
                    std::size_t game_index) {
  const auto& metadata = game.metadata;
  fixed_text(state, game_index, "metadata.title", metadata.title, 0x80);
  fixed_text(state, game_index, "metadata.red_player.name", metadata.red_player.name, 0x40);
  fixed_text(state, game_index, "metadata.red_player.team", metadata.red_player.team, 0x40);
  fixed_text(state, game_index, "metadata.red_player.time_used", metadata.red_player.time_used, 0x40);
  fixed_text(state, game_index, "metadata.black_player.name", metadata.black_player.name, 0x40);
  fixed_text(state, game_index, "metadata.black_player.team", metadata.black_player.team, 0x40);
  fixed_text(state, game_index, "metadata.black_player.time_used", metadata.black_player.time_used, 0x40);
  fixed_text(state, game_index, "metadata.event.name", metadata.event.name, 0x40);
  fixed_text(state, game_index, "metadata.event.location", metadata.event.location, 0x40);
  fixed_text(state, game_index, "metadata.event.round", metadata.event.round, 0x40);
  fixed_text(state, game_index, "metadata.event.type", metadata.event.type, 0x40);
  fixed_text(state, game_index, "metadata.event.group", metadata.event.group, 0x20);
  fixed_text(state, game_index, "metadata.event.board_number", metadata.event.board_number, 0x20);
  fixed_text(state, game_index, "metadata.event.time_control", metadata.event.time_control, 0x40);
  fixed_text(state, game_index, "metadata.result_text", metadata.result_text, 0x20);
  fixed_text(state, game_index, "metadata.game_type", metadata.game_type, 0x20);
  fixed_text(state, game_index, "metadata.referee", metadata.referee, 0x40);
  fixed_text(state, game_index, "metadata.recorder", metadata.recorder, 0x40);
  fixed_text(state, game_index, "metadata.commentator", metadata.commentator, 0x40);
  fixed_text(state, game_index, "metadata.commentator_uri", metadata.commentator_uri, 0x40);
  fixed_text(state, game_index, "metadata.creator", metadata.creator, 0x40);
  fixed_text(state, game_index, "metadata.creator_uri", metadata.creator_uri, 0x40);
  fixed_text(state, game_index, "metadata.record_created_at", metadata.record_created_at, 0x40);
  fixed_text(state, game_index, "metadata.record_modified_at", metadata.record_modified_at, 0x40);
  fixed_text(state, game_index, "metadata.provenance.source_uri", metadata.provenance.source_uri, 0x40);
  fixed_text(state, game_index, "metadata.provenance.source_category", metadata.provenance.source_category, 0x100);

  if (metadata.red_player.rating.has_value()) {
    fixed_text(state, game_index, "metadata.red_player.rating",
               std::optional<std::string>{std::to_string(*metadata.red_player.rating)}, 0x20);
  }
  if (metadata.black_player.rating.has_value()) {
    fixed_text(state, game_index, "metadata.black_player.rating",
               std::optional<std::string>{std::to_string(*metadata.black_player.rating)}, 0x20);
  }
  if (metadata.event.start_time.has_value() &&
      metadata.event.date_precision == core::DatePrecision::day &&
      metadata.event.start_time->size() == 10U) {
    fixed_text(state, game_index, "metadata.event.start_time",
               metadata.event.start_time, 0x40);
  } else {
    unsupported_optional(state, game_index, "metadata.event.start_time",
                         metadata.event.start_time);
  }

  unsupported_optional(state, game_index, "metadata.red_player.id", metadata.red_player.id);
  unsupported_optional(state, game_index, "metadata.red_player.country", metadata.red_player.country);
  unsupported_optional(state, game_index, "metadata.red_player.title", metadata.red_player.title);
  unsupported_optional(state, game_index, "metadata.black_player.id", metadata.black_player.id);
  unsupported_optional(state, game_index, "metadata.black_player.country", metadata.black_player.country);
  unsupported_optional(state, game_index, "metadata.black_player.title", metadata.black_player.title);
  unsupported_optional(state, game_index, "metadata.event.id", metadata.event.id);
  unsupported_optional(state, game_index, "metadata.event.organizer", metadata.event.organizer);
  unsupported_optional(state, game_index, "metadata.event.end_time", metadata.event.end_time);
  unsupported_optional(state, game_index, "metadata.opening.name", metadata.opening.name);
  unsupported_optional(state, game_index, "metadata.opening.id", metadata.opening.id);
  unsupported_optional(state, game_index, "metadata.provenance.import_note", metadata.provenance.import_note);

  if (metadata.opening.code.has_value()) {
    const auto& code = *metadata.opening.code;
    if (code.empty()) {
      unsupported(state, game_index, "metadata.opening.code",
                  "CBL cannot preserve a present-but-empty ECCO code");
    } else {
      const bool ascii = std::ranges::all_of(code, [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte > 0U && byte < 0x80U;
      });
      if (code.size() > 3U || !ascii) {
        diagnostic(state, ConversionSeverity::loss,
                   ConversionCode::cbl_write_text_too_long, game_index, {},
                   "metadata.opening.code",
                   "CBL ECCO code must contain one to three ASCII bytes");
        state.fatal = true;
      }
    }
  }
  if (!metadata.tags.empty()) {
    unsupported(state, game_index, "metadata.tags", "CBL v3 has no ordered tag list");
  }
  if (!metadata.result.has_value() ||
      *metadata.result == core::GameResult::unfinished ||
      *metadata.result == core::GameResult::aborted) {
    diagnostic(state, ConversionSeverity::loss,
               ConversionCode::cbl_write_result_normalized, game_index, {},
               "metadata.result",
               "CBL v3 normalizes this result to its unknown result value");
  }

  const auto& provenance = metadata.provenance;
  unsupported_optional(state, game_index, "metadata.provenance.source_format", provenance.source_format);
  unsupported_optional(state, game_index, "metadata.provenance.source_record_id", provenance.source_record_id);
  unsupported_optional(state, game_index, "metadata.provenance.source_format_version", provenance.source_format_version);
  unsupported_optional(state, game_index, "metadata.provenance.source_library_id", provenance.source_library_id);
  unsupported_optional(state, game_index, "metadata.provenance.source_library_name", provenance.source_library_name);

  for (const auto& [name_space, properties] : metadata.extensions) {
    for (const auto& [key, value] : properties) {
      const bool restored = name_space == kCblExtension &&
                            (key == "record_type" || key == "root_marker" ||
                             key == "source_controls");
      if (!restored) {
        diagnostic(state, ConversionSeverity::loss,
                   ConversionCode::cbl_write_extension_unsupported, game_index, {},
                   "metadata.extensions." + name_space + "." + key,
                   "extension property is not restored by the CBL v3 writer");
      }
      (void)value;
    }
  }
}

[[nodiscard]] std::size_t combined_comment_bytes(
    PreflightState& state, const core::MoveNode& node, std::size_t game_index,
    std::size_t node_index, const CblWriteLimits& limits) {
  if (node.annotations.empty()) {
    return 0;
  }
  std::size_t units = 0;
  for (std::size_t index = 0; index < node.annotations.size(); ++index) {
    const auto& annotation = node.annotations[index];
    if (index != 0) {
      units += 2;
    }
    const auto length = utf16_length(annotation.text);
    if (length.valid) {
      units += length.units;
    }
    if (annotation.kind != core::AnnotationKind::comment || annotation.before_move ||
        annotation.author.has_value() || annotation.language.has_value()) {
      diagnostic(state, ConversionSeverity::loss,
                 ConversionCode::cbl_write_annotation_normalized, game_index,
                 node_index,
                 "move_tree.nodes[" + std::to_string(node_index) + "].annotations[" +
                     std::to_string(index) + "]",
                 "CBL preserves annotation text but not its kind, timing, author, or language");
    }
  }
  if (node.annotations.size() > 1U) {
    diagnostic(state, ConversionSeverity::loss,
               ConversionCode::cbl_write_annotation_normalized, game_index,
               node_index,
               "move_tree.nodes[" + std::to_string(node_index) + "].annotations",
               "multiple annotations are joined with a blank line in CBL");
  }
  if (units > std::numeric_limits<std::uint32_t>::max() / 2U ||
      units * 2U > limits.max_comment_bytes) {
    diagnostic(state, ConversionSeverity::loss,
               ConversionCode::cbl_write_text_too_long, game_index, node_index,
               "move_tree.nodes[" + std::to_string(node_index) + "].annotations",
               "combined CBL comment exceeds the configured byte limit");
    state.fatal = true;
    return 0;
  }
  return 4U + units * 2U;
}

[[nodiscard]] core::Uuid choose_library_uuid(
    PreflightState& state, std::span<const core::GameModel> games,
    const CblWriteLibraryMetadata& library) {
  if (library.uuid.has_value() && !library.uuid->is_nil() &&
      (library.uuid->bytes[8] & 0xc0U) == 0x80U) {
    return *library.uuid;
  }
  std::string identity = "org.openxiangqi.cbl-writer/v1\nlibrary-name-sha256=" +
                         detail::sha256_hex(std::as_bytes(std::span{library.name})) +
                         "\ngame-count=" + std::to_string(games.size());
  for (const auto& game : games) {
    identity += "\ngame-uuid=" + game.uuid.to_string();
  }
  state.plan.report.diagnostics.push_back(
      {ConversionSeverity::warning,
       ConversionCode::cbl_write_library_uuid_derived,
       {}, {}, {}, "library.uuid",
       "library UUID was derived deterministically from the library name and ordered games"});
  return detail::uuid_v5(*core::Uuid::parse(kWriterUuidNamespace), identity);
}

}  // namespace

CblWritePreflightOutcome preflight_cbl_write(
    std::span<const core::GameModel> games, const CblWriteOptions& options) {
  if (games.size() > options.limits.max_games) {
    return write_error(CblWriteErrorCode::too_many_games, "games",
                       "CBL game count exceeds the configured limit",
                       options.limits.max_games, games.size());
  }
  const auto capacity =
      std::max({std::size_t{128}, options.minimum_directory_capacity, games.size()});
  if (capacity > options.limits.max_directory_entries ||
      capacity > std::numeric_limits<std::uint32_t>::max()) {
    return write_error(CblWriteErrorCode::resource_limit,
                       "directory_capacity",
                       "CBL directory capacity exceeds the supported limit",
                       options.limits.max_directory_entries, capacity);
  }

  PreflightState state;
  state.plan.report.source_game_count = games.size();
  state.plan.directory_capacity = capacity;
  state.plan.record_sizes.reserve(games.size());
  plain_fixed_text(state, "library.name", options.library.name, 0x300);
  plain_fixed_text(state, "library.author", options.library.author, 0x40);
  plain_fixed_text(state, "library.author_email", options.library.author_email, 0x40);
  plain_fixed_text(state, "library.created_at", options.library.created_at, 0x40);
  plain_fixed_text(state, "library.modified_at", options.library.modified_at, 0x40);
  state.plan.library_uuid = choose_library_uuid(state, games, options.library);

  std::size_t directory_bytes = 0;
  if (!checked_multiply(capacity, kDirectoryEntrySize, directory_bytes) ||
      !checked_add(kDirectoryOffset, directory_bytes,
                   state.plan.projected_file_size)) {
    return write_error(CblWriteErrorCode::integer_overflow, "directory_capacity",
                       "CBL directory size overflows size_t");
  }

  for (std::size_t game_index = 0; game_index < games.size(); ++game_index) {
    const auto& game = games[game_index];
    const auto issues = core::validate(game);
    for (const auto& issue : issues) {
      if (issue.severity == core::ValidationSeverity::error) {
        diagnostic(state, ConversionSeverity::loss,
                   ConversionCode::cbl_write_invalid_game_model, game_index, {},
                   issue.path, issue.message);
        state.fatal = true;
      }
    }
    check_metadata(state, game, game_index);

    std::size_t record_size = kRecordPrefixSize;
    std::size_t node_bytes = 0;
    const auto move_node_count = game.move_tree.nodes.empty()
                                     ? 0U
                                     : game.move_tree.nodes.size() - 1U;
    if (!checked_multiply(move_node_count, 4U, node_bytes) ||
        !checked_add(record_size, node_bytes, record_size)) {
      return write_error(CblWriteErrorCode::integer_overflow, "move_tree",
                         "CBL Record size overflows size_t");
    }
    for (std::size_t node_index = 0; node_index < game.move_tree.nodes.size(); ++node_index) {
      const auto comment_bytes = combined_comment_bytes(
          state, game.move_tree.nodes[node_index], game_index, node_index,
          options.limits);
      if (!checked_add(record_size, comment_bytes, record_size)) {
        return write_error(CblWriteErrorCode::integer_overflow, "annotations",
                           "CBL Record comment size overflows size_t");
      }
    }
    if (record_size > options.limits.max_record_bytes ||
        record_size > std::numeric_limits<std::uint32_t>::max()) {
      diagnostic(state, ConversionSeverity::loss,
                 ConversionCode::cbl_write_text_too_long, game_index, {},
                 "record_size", "CBL Record exceeds the configured byte limit");
      state.fatal = true;
    }
    state.plan.record_sizes.push_back(record_size);
    const auto block_count = record_size / kBlockSize +
                             (record_size % kBlockSize == 0 ? 0U : 1U);
    std::size_t allocated = 0;
    if (!checked_multiply(block_count, kBlockSize, allocated) ||
        !checked_add(state.plan.projected_file_size, allocated,
                     state.plan.projected_file_size)) {
      return write_error(CblWriteErrorCode::integer_overflow, "output_size",
                         "CBL projected output size overflows size_t");
    }
  }

  if (state.plan.projected_file_size > options.limits.max_output_bytes) {
    return write_error(CblWriteErrorCode::resource_limit, "output_size",
                       "CBL projected output exceeds the configured limit",
                       options.limits.max_output_bytes,
                       state.plan.projected_file_size);
  }
  const bool strict_loss = options.mode == ConversionMode::strict &&
                           state.plan.report.has_loss();
  state.plan.report.rejected = state.fatal || strict_loss;
  state.plan.report.converted_game_count =
      state.plan.report.rejected ? 0U : games.size();
  return state.plan;
}

}  // namespace oxq::convert
