#include <oxq/convert/cbl_reader.hpp>

#include "cbl/container.hpp"
#include "cbl/hash.hpp"
#include "cbl/record.hpp"
#include "cbl/tree.hpp"

#include <oxq/core/validation.hpp>

#include <algorithm>
#include <charconv>
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

constexpr std::string_view kExtensionNamespace = "org.openxiangqi.cbl";
constexpr std::string_view kUuidNamespace = "8e4fd752-6a26-5a9b-b295-0f2e0973c943";

[[nodiscard]] bool usable_uuid(const core::Uuid& uuid) noexcept {
  return !uuid.is_nil() && (uuid.bytes[8] & 0xc0U) == 0x80U;
}

[[nodiscard]] std::optional<core::Uuid> parse_directory_uuid(
    std::string_view text) noexcept {
  if (text.size() == 38 && text.front() == '{' && text.back() == '}') {
    text.remove_prefix(1);
    text.remove_suffix(1);
  }
  auto parsed = core::Uuid::parse(text);
  return parsed.has_value() && usable_uuid(*parsed) ? parsed : std::nullopt;
}

[[nodiscard]] std::string hex_value(std::uint32_t value, std::size_t width) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(width, '0');
  for (std::size_t index = 0; index < width; ++index) {
    result[width - index - 1] = digits[value & 0x0fU];
    value >>= 4U;
  }
  return result;
}

void set_if_present(std::optional<std::string>& destination,
                    const std::string& source) {
  if (!source.empty()) {
    destination = source;
  }
}

[[nodiscard]] std::optional<std::int32_t> parse_integer(
    std::string_view text) noexcept {
  std::int32_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] bool leap_year(unsigned year) noexcept {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

[[nodiscard]] bool valid_day_date(std::string_view value) noexcept {
  if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
    return false;
  }
  const auto digit = [&value](std::size_t index) {
    return value[index] >= '0' && value[index] <= '9';
  };
  for (const auto index : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U}) {
    if (!digit(index)) {
      return false;
    }
  }
  const auto number = [&value](std::size_t offset, std::size_t length) {
    unsigned result = 0;
    for (std::size_t index = 0; index < length; ++index) {
      result = result * 10U + static_cast<unsigned>(value[offset + index] - '0');
    }
    return result;
  };
  const auto year = number(0, 4);
  const auto month = number(5, 2);
  const auto day = number(8, 2);
  if (year == 0 || month == 0 || month > 12 || day == 0) {
    return false;
  }
  constexpr unsigned month_days[] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
  const auto maximum = month == 2 && leap_year(year) ? 29U : month_days[month - 1];
  return day <= maximum;
}

void add_diagnostic(ConversionReport& report, ConversionSeverity severity,
                    ConversionCode code, std::size_t game_index,
                    std::size_t physical_slot, std::string field,
                    std::string message) {
  report.diagnostics.push_back({severity, code, game_index, physical_slot, {},
                                std::move(field), std::move(message)});
}

[[nodiscard]] core::Uuid choose_game_uuid(
    std::span<const std::byte> source, const detail::CblDirectoryEntryView& entry,
    const detail::CblRecordView& record, std::size_t game_index,
    ConversionReport& report, std::optional<std::string>& source_hash) {
  const auto directory_uuid = parse_directory_uuid(entry.uuid_text);
  const bool record_valid = usable_uuid(record.guid);
  if (record_valid && directory_uuid.has_value()) {
    if (record.guid == *directory_uuid) {
      return record.guid;
    }
    add_diagnostic(report, ConversionSeverity::warning,
                   ConversionCode::cbl_uuid_mismatch, game_index,
                   entry.physical_slot, "uuid",
                   "CBL Directory UUID differs from Record GUID; Record GUID was selected");
    return record.guid;
  }
  if (record_valid) {
    add_diagnostic(report, ConversionSeverity::warning,
                   ConversionCode::cbl_directory_uuid_invalid, game_index,
                   entry.physical_slot, "directory_uuid",
                   "CBL Directory UUID is not a usable RFC 9562 UUID");
    return record.guid;
  }
  if (directory_uuid.has_value()) {
    add_diagnostic(report, ConversionSeverity::warning,
                   ConversionCode::cbl_record_uuid_invalid, game_index,
                   entry.physical_slot, "record_guid",
                   "CBL Record GUID is invalid; Directory UUID was selected");
    return *directory_uuid;
  }

  if (!source_hash.has_value()) {
    source_hash = detail::sha256_hex(source);
  }
  const auto record_bytes = source.subspan(entry.resource_offset, entry.used_size);
  const std::string name =
      "org.openxiangqi.cbl/v1\nsource-sha256=" + *source_hash +
      "\nphysical-slot=" + std::to_string(entry.physical_slot) +
      "\nrecord-sha256=" + detail::sha256_hex(record_bytes);
  const auto name_space = core::Uuid::parse(kUuidNamespace);
  add_diagnostic(report, ConversionSeverity::warning,
                 ConversionCode::cbl_uuid_derived, game_index,
                 entry.physical_slot, "uuid",
                 "CBL UUIDs are invalid; a stable UUIDv5 was derived from source bytes");
  return detail::uuid_v5(*name_space, name);
}

void map_rating(const std::string& source,
                std::optional<std::int32_t>& destination,
                core::ExtensionProperties& extension, std::string extension_key,
                ConversionReport& report, std::size_t game_index,
                std::size_t physical_slot, std::string field) {
  if (source.empty()) {
    return;
  }
  const auto rating = parse_integer(source);
  if (rating.has_value()) {
    destination = *rating;
    return;
  }
  extension[std::move(extension_key)] = source;
  add_diagnostic(report, ConversionSeverity::warning,
                 ConversionCode::cbl_invalid_rating, game_index, physical_slot,
                 std::move(field),
                 "CBL rating is not a complete signed decimal integer; raw text was preserved");
}

void map_metadata(core::GameModel& game, const detail::CblContainerView& container,
                  const detail::CblDirectoryEntryView& entry,
                  const detail::CblRecordView& record,
                  const detail::CblMoveTreeView& tree, std::size_t game_index,
                  ConversionReport& report) {
  const auto& source = record.metadata;
  auto& target = game.metadata;
  auto& extension = target.extensions[std::string{kExtensionNamespace}];

  if (!source.name.empty()) {
    target.title = source.name;
  } else {
    set_if_present(target.title, entry.title);
  }
  if (!source.name.empty() && !entry.title.empty() && source.name != entry.title) {
    add_diagnostic(report, ConversionSeverity::warning,
                   ConversionCode::cbl_title_mismatch, game_index,
                   entry.physical_slot, "title",
                   "CBL Directory title differs from Record name; Record name was selected");
  }
  set_if_present(target.red_player.name, source.red);
  set_if_present(target.red_player.team, source.red_team);
  set_if_present(target.red_player.time_used, source.red_time);
  set_if_present(target.black_player.name, source.black);
  set_if_present(target.black_player.team, source.black_team);
  set_if_present(target.black_player.time_used, source.black_time);
  set_if_present(target.event.name, source.contest);
  set_if_present(target.event.location, source.site);
  set_if_present(target.event.round, source.round);
  set_if_present(target.event.type, source.contest_type);
  set_if_present(target.event.group, source.group);
  set_if_present(target.event.board_number, source.table);
  set_if_present(target.event.time_control, source.time_rule);
  set_if_present(target.referee, source.referee);
  set_if_present(target.recorder, source.recorder);
  set_if_present(target.commentator, source.commentator);
  set_if_present(target.commentator_uri, source.commentator_uri);
  set_if_present(target.creator, source.creator);
  set_if_present(target.creator_uri, source.creator_uri);
  set_if_present(target.record_created_at, source.created_at);
  set_if_present(target.record_modified_at, source.modified_at);
  set_if_present(target.opening.code, source.ecco_code);
  set_if_present(target.result_text, source.result_text);
  set_if_present(target.game_type, source.record_kind);

  if (!source.date.empty()) {
    if (valid_day_date(source.date)) {
      target.event.start_time = source.date;
      target.event.date_precision = core::DatePrecision::day;
    } else {
      extension["date_raw"] = source.date;
      add_diagnostic(report, ConversionSeverity::warning,
                     ConversionCode::cbl_invalid_date, game_index,
                     entry.physical_slot, "date",
                     "CBL date is not a valid YYYY-MM-DD date; raw text was preserved");
    }
  }
  map_rating(source.red_rating, target.red_player.rating, extension,
             "red_rating_raw", report, game_index, entry.physical_slot,
             "red_rating");
  map_rating(source.black_rating, target.black_player.rating, extension,
             "black_rating_raw", report, game_index, entry.physical_slot,
             "black_rating");

  switch (source.result) {
    case 0: target.result = core::GameResult::unknown; break;
    case 1: target.result = core::GameResult::red_win; break;
    case 2: target.result = core::GameResult::black_win; break;
    case 3: target.result = core::GameResult::draw; break;
    case 4:
      target.result = core::GameResult::unknown;
      add_diagnostic(report, ConversionSeverity::loss,
                     ConversionCode::cbl_multiple_result, game_index,
                     entry.physical_slot, "result",
                     "CBL multiple-result has no equivalent standard GameResult");
      break;
    default:
      target.result = core::GameResult::unknown;
      add_diagnostic(report, ConversionSeverity::loss,
                     ConversionCode::cbl_unknown_result, game_index,
                     entry.physical_slot, "result",
                     "CBL result value is outside the confirmed v3 domain");
      break;
  }

  if (source.record_type > 4) {
    add_diagnostic(report, ConversionSeverity::warning,
                   ConversionCode::cbl_unknown_record_type, game_index,
                   entry.physical_slot, "record_type",
                   "CBL RecordType is outside the confirmed v3 domain; raw value was preserved");
  }
  if (record.source_fullmove_number == 0 ||
      record.source_fullmove_number > std::numeric_limits<std::uint16_t>::max()) {
    add_diagnostic(report, ConversionSeverity::loss,
                   ConversionCode::cbl_fullmove_normalized, game_index,
                   entry.physical_slot, "fullmove_number",
                   "CBL fullmove number cannot be represented by GameModel "
                   "and was normalized to 1");
  }

  target.provenance.source_format = "CBL";
  target.provenance.source_format_version = "3";
  target.provenance.source_record_id = game.uuid.to_string();
  target.provenance.source_library_id = container.library.uuid.to_string();
  set_if_present(target.provenance.source_library_name, container.library.name);
  set_if_present(target.provenance.source_uri, source.source);
  set_if_present(target.provenance.source_category, source.url_or_category);

  extension["physical_slot"] = std::to_string(entry.physical_slot);
  extension["display_index"] = std::to_string(entry.display_index);
  extension["directory_uuid"] = entry.uuid_text;
  extension["directory_title"] = entry.title;
  extension["record_guid"] = record.guid.to_string();
  extension["record_type"] = std::to_string(source.record_type);
  extension["result"] = std::to_string(source.result);
  extension["source_fullmove_number"] =
      std::to_string(record.source_fullmove_number);
  extension["root_marker"] = hex_value(record.root_marker, 8);
  std::vector<std::string> controls;
  controls.reserve(tree.source_controls.size());
  for (const auto control : tree.source_controls) {
    controls.push_back(hex_value(control, 4));
  }
  extension["source_controls"] = std::move(controls);
  if (!source.record_kind.empty()) {
    extension["record_kind"] = source.record_kind;
  }
  if (!source.result_text.empty()) {
    extension["result_text"] = source.result_text;
  }
}

}  // namespace

bool ConversionReport::has_warnings() const noexcept {
  return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
    return diagnostic.severity == ConversionSeverity::warning;
  });
}

bool ConversionReport::has_loss() const noexcept {
  return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
    return diagnostic.severity == ConversionSeverity::loss;
  });
}

CblInspectOutcome inspect_cbl(std::span<const std::byte> input,
                              const CblReaderLimits& limits) {
  auto result = detail::inspect_cbl_container(input, limits);
  if (std::holds_alternative<CblError>(result)) {
    return std::get<CblError>(std::move(result));
  }
  return std::get<detail::CblContainerView>(std::move(result)).library;
}

CblReadOutcome read_cbl(std::span<const std::byte> input,
                        const CblReadOptions& options) {
  auto container_result = detail::inspect_cbl_container(input, options.limits);
  if (std::holds_alternative<CblError>(container_result)) {
    return std::get<CblError>(std::move(container_result));
  }
  auto container =
      std::get<detail::CblContainerView>(std::move(container_result));
  CblReadResult result;
  result.library = container.library;

  std::vector<const detail::CblDirectoryEntryView*> entries;
  entries.reserve(container.library.live_game_count);
  for (const auto& entry : container.entries) {
    if (entry.kind == detail::CblResourceKind::live_game) {
      entries.push_back(&entry);
    }
  }
  std::ranges::sort(entries, [](const auto* left, const auto* right) {
    if (left->display_index != right->display_index) {
      return left->display_index < right->display_index;
    }
    return left->physical_slot < right->physical_slot;
  });
  result.report.source_game_count = entries.size();
  result.games.reserve(entries.size());
  std::optional<std::string> source_hash;
  for (std::size_t game_index = 0; game_index < entries.size(); ++game_index) {
    const auto& entry = *entries[game_index];
    if (game_index > 0 &&
        entry.display_index == entries[game_index - 1]->display_index) {
      add_diagnostic(result.report, ConversionSeverity::warning,
                     ConversionCode::cbl_duplicate_display_index, game_index,
                     entry.physical_slot, "display_index",
                     "duplicate CBL display_index was ordered by physical slot");
    }
    auto record_result = detail::read_cbl_record(container, entry);
    if (std::holds_alternative<CblError>(record_result)) {
      return std::get<CblError>(std::move(record_result));
    }
    auto record = std::get<detail::CblRecordView>(std::move(record_result));
    auto tree_result = detail::read_cbl_move_tree(container, entry, record,
                                                  options.limits);
    if (std::holds_alternative<CblError>(tree_result)) {
      return std::get<CblError>(std::move(tree_result));
    }
    auto tree = std::get<detail::CblMoveTreeView>(std::move(tree_result));

    core::GameModel game;
    game.uuid = choose_game_uuid(input, entry, record, game_index,
                                 result.report, source_hash);
    game.initial_position = std::move(record.position);
    game.move_tree = tree.tree;
    map_metadata(game, container, entry, record, tree, game_index,
                 result.report);
    const auto validation = core::validate(game);
    if (core::has_errors(validation)) {
      const auto issue = std::ranges::find(validation, core::ValidationSeverity::error,
                                           &core::ValidationIssue::severity);
      return CblError{CblErrorCode::invalid_game_model,
                      entry.resource_offset,
                      entry.physical_slot,
                      issue != validation.end() ? issue->path : "game_model",
                      issue != validation.end()
                          ? "CBL mapping produced an invalid GameModel: " + issue->message
                          : "CBL mapping produced an invalid GameModel",
                      {}, {}};
    }
    result.games.push_back(std::move(game));
  }
  result.report.converted_game_count = result.games.size();
  if (options.mode == ConversionMode::strict && result.report.has_loss()) {
    result.games.clear();
    result.report.converted_game_count = 0;
    result.report.rejected = true;
  }
  return result;
}

}  // namespace oxq::convert
