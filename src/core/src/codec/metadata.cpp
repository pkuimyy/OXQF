#include "codec/metadata.hpp"

#include "codec/binary.hpp"
#include "codec/restricted_json.hpp"
#include "date_time.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace oxq::core::detail {
namespace {

constexpr std::uint32_t kMetadataSectionType = 1;
constexpr std::uint8_t kCritical = 1;
constexpr std::uint8_t kRepeated = 2;

[[nodiscard]] CodecError error(CodecErrorCode code, std::size_t offset, std::string field,
                               std::string message, std::optional<std::uint64_t> expected = {},
                               std::optional<std::uint64_t> actual = {}) {
  return {code, offset, kMetadataSectionType, std::move(field), std::move(message), expected, actual};
}

[[nodiscard]] std::optional<MetadataValueType> standard_type(std::uint16_t tag) noexcept {
  switch (tag) {
    case 0x0007:
    case 0x0008:
      return MetadataValueType::i32;
    case 0x0022:
    case 0x0030:
      return MetadataValueType::u32;
    case 0x0001:
    case 0x0002:
    case 0x0003:
    case 0x0004:
    case 0x0005:
    case 0x0006:
    case 0x0009:
    case 0x000a:
    case 0x000b:
    case 0x000c:
    case 0x000d:
    case 0x000e:
    case 0x0010:
    case 0x0011:
    case 0x0012:
    case 0x0013:
    case 0x0014:
    case 0x0015:
    case 0x0016:
    case 0x0017:
    case 0x0018:
    case 0x0020:
    case 0x0021:
    case 0x0031:
    case 0x0040:
    case 0x0041:
    case 0x0042:
    case 0x0050:
    case 0x0051:
    case 0x0052:
    case 0x0060:
    case 0x0061:
    case 0x0062:
    case 0x0063:
    case 0x0064:
    case 0x0065:
    case 0x0066:
    case 0x0067:
    case 0x0100:
    case 0x0101:
    case 0x0102:
    case 0x0103:
    case 0x0104:
    case 0x0105:
    case 0x0106:
    case 0x0107:
    case 0x7fff:
      return MetadataValueType::string_ref;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] bool known_value_type(std::uint8_t value) noexcept {
  return value >= static_cast<std::uint8_t>(MetadataValueType::u32) &&
         value <= static_cast<std::uint8_t>(MetadataValueType::boolean);
}

[[nodiscard]] std::optional<std::size_t> fixed_length(std::uint8_t value_type) noexcept {
  switch (static_cast<MetadataValueType>(value_type)) {
    case MetadataValueType::u32:
    case MetadataValueType::i32:
    case MetadataValueType::string_ref:
      return 4;
    case MetadataValueType::u64:
    case MetadataValueType::i64:
      return 8;
    case MetadataValueType::boolean:
      return 1;
    case MetadataValueType::bytes:
      return std::nullopt;
  }
  return std::nullopt;
}

[[nodiscard]] bool unsigned_less(std::string_view left, std::string_view right) noexcept {
  return std::lexicographical_compare(
      left.begin(), left.end(), right.begin(), right.end(),
      [](char lhs, char rhs) {
        return static_cast<unsigned char>(lhs) < static_cast<unsigned char>(rhs);
      });
}

}  // namespace

MetadataResult read_metadata(std::span<const std::byte> input, const ContainerView& container,
                             const StringPoolView& strings, const MetadataLimits& limits) {
  const auto section = std::ranges::find(container.sections, kMetadataSectionType,
                                         &SectionView::type);
  if (section == container.sections.end()) {
    return error(CodecErrorCode::invalid_section_table, 0, "section_type",
                 "GAME_METADATA Section is missing");
  }
  const auto payload = input.subspan(section->offset, section->size);
  if (payload.size() < 8) {
    return error(CodecErrorCode::invalid_metadata, section->offset, "header",
                 "GAME_METADATA is shorter than its 8-byte header", 8, payload.size());
  }
  const auto version = read_u16(payload, 0);
  const auto header_size = read_u16(payload, 2);
  if (version != 1 || header_size != 8) {
    return error(CodecErrorCode::unsupported_version, section->offset, "section_version",
                 "unsupported GAME_METADATA header", 1, version);
  }
  const auto count = static_cast<std::size_t>(read_u32(payload, 4));
  if (count > limits.max_fields) {
    return error(CodecErrorCode::resource_limit, section->offset + 4, "field_count",
                 "Metadata Field count limit exceeded", limits.max_fields, count);
  }
  if (count > (payload.size() - 8) / 8) {
    return error(CodecErrorCode::invalid_metadata, section->offset + 4, "field_count",
                 "field_count cannot fit in GAME_METADATA", {}, count);
  }

  MetadataView result;
  result.fields.reserve(count);
  std::size_t cursor = 8;
  std::optional<std::uint16_t> previous_tag;
  std::optional<std::string_view> previous_repeated_tag;
  std::array<std::uint8_t, 65'536> seen_tags{};
  for (std::size_t index = 0; index < count; ++index) {
    if (!contains(payload, cursor, 8)) {
      return error(CodecErrorCode::invalid_metadata, section->offset + cursor, "field",
                   "Metadata Field header is truncated");
    }
    const auto tag = read_u16(payload, cursor);
    const auto value_type = std::to_integer<std::uint8_t>(payload[cursor + 2]);
    const auto flags = std::to_integer<std::uint8_t>(payload[cursor + 3]);
    const auto length = static_cast<std::size_t>(read_u32(payload, cursor + 4));
    const auto expected_type = standard_type(tag);
    const bool standard = expected_type.has_value();

    if (tag == 0 || (flags & ~3U) != 0) {
      return error(CodecErrorCode::invalid_metadata, section->offset + cursor, "tag_or_flags",
                   "Metadata tag is zero or field_flags contains reserved bits");
    }
    if (standard) {
      const std::uint8_t expected_flags = tag == 0x0051 ? kRepeated : 0;
      if ((flags & kCritical) != 0 || flags != expected_flags ||
          value_type != static_cast<std::uint8_t>(*expected_type)) {
        return error(CodecErrorCode::invalid_metadata, section->offset + cursor + 2,
                     "value_type_or_flags", "standard Metadata Field type or flags mismatch");
      }
    } else {
      ++result.unknown_field_count;
      if ((flags & kCritical) != 0) {
        return error(CodecErrorCode::invalid_metadata, section->offset + cursor, "tag",
                     "unknown critical Metadata Field cannot be skipped", {}, tag);
      }
      if (!known_value_type(value_type)) {
        ++result.unknown_value_type_count;
      }
    }

    if (known_value_type(value_type)) {
      if (const auto required = fixed_length(value_type); required.has_value() && length != *required) {
        return error(CodecErrorCode::invalid_metadata, section->offset + cursor + 4,
                     "value_length", "Metadata value has the wrong fixed length", *required,
                     length);
      }
    }

    std::size_t value_begin = 0;
    std::size_t value_end = 0;
    if (!checked_add(cursor, 8, value_begin) || !checked_add(value_begin, length, value_end) ||
        value_end > payload.size()) {
      return error(CodecErrorCode::invalid_metadata, section->offset + cursor + 4, "value_length",
                   "Metadata Field extends beyond GAME_METADATA", {}, length);
    }
    const auto raw_value = payload.subspan(value_begin, length);
    std::optional<std::string_view> string_value;
    if (value_type == static_cast<std::uint8_t>(MetadataValueType::string_ref)) {
      const auto reference = read_u32(raw_value, 0);
      const auto* record = strings.find_record(reference);
      if (reference == 0 || record == nullptr) {
        return error(CodecErrorCode::invalid_string_ref, section->offset + value_begin,
                     "string_ref", "Metadata STRING_REF does not point to a String Record",
                     {}, reference);
      }
      string_value = record->value;
    } else if (value_type == static_cast<std::uint8_t>(MetadataValueType::boolean) &&
               std::to_integer<std::uint8_t>(raw_value[0]) > 1) {
      return error(CodecErrorCode::invalid_metadata, section->offset + value_begin, "BOOL",
                   "Metadata BOOL must be 0 or 1");
    }
    if (tag == 0x0022 && read_u32(raw_value, 0) > 6) {
      return error(CodecErrorCode::invalid_metadata, section->offset + value_begin,
                   "DATE_PRECISION", "DATE_PRECISION is outside 0..6");
    }
    if (tag == 0x0030 && read_u32(raw_value, 0) > 5) {
      return error(CodecErrorCode::invalid_metadata, section->offset + value_begin, "RESULT",
                   "RESULT is outside 0..5");
    }

    const std::size_t padding = (4 - value_end % 4) % 4;
    std::size_t field_end = 0;
    if (!checked_add(value_end, padding, field_end) || field_end > payload.size()) {
      return error(CodecErrorCode::invalid_metadata, section->offset + value_end, "padding",
                   "Metadata Field padding extends beyond GAME_METADATA");
    }
    if (!std::ranges::all_of(payload.subspan(value_end, padding),
                             [](std::byte byte) { return byte == std::byte{0}; })) {
      return error(CodecErrorCode::invalid_metadata, section->offset + value_end, "padding",
                   "Metadata Field padding must be zero");
    }

    if ((seen_tags[tag] & 1U) != 0 &&
        ((flags & kRepeated) == 0 || (seen_tags[tag] & 2U) == 0)) {
      return error(CodecErrorCode::invalid_metadata, section->offset + cursor, "tag",
                   "non-repeated Metadata tag appears more than once", {}, tag);
    }
    seen_tags[tag] = static_cast<std::uint8_t>(1U | ((flags & kRepeated) == 0 ? 0U : 2U));
    if (previous_tag.has_value() && tag < *previous_tag) {
      result.canonical_order = false;
    }
    if (tag == 0x0051 && string_value.has_value()) {
      if (previous_repeated_tag.has_value() && !unsigned_less(*previous_repeated_tag, *string_value)) {
        result.canonical_order = false;
      }
      previous_repeated_tag = string_value;
    } else {
      previous_repeated_tag.reset();
    }
    previous_tag = tag;
    std::optional<std::size_t> string_data_offset;
    if (value_type == static_cast<std::uint8_t>(MetadataValueType::string_ref)) {
      string_data_offset = strings.find_record(read_u32(raw_value, 0))->data_offset;
    }
    result.fields.push_back({tag, value_type, flags, raw_value, string_value,
                             section->offset + value_begin, string_data_offset, standard});
    cursor = field_end;
  }
  if (cursor != payload.size()) {
    return error(CodecErrorCode::invalid_metadata, section->offset + cursor, "trailing_data",
                 "GAME_METADATA contains bytes beyond field_count records", cursor,
                 payload.size());
  }
  return result;
}

DecodedMetadataResult decode_metadata(const MetadataView& metadata,
                                      std::size_t max_extended_metadata_bytes) {
  DecodedMetadata result;
  const MetadataFieldView* start_time_field = nullptr;
  const MetadataFieldView* end_time_field = nullptr;
  const MetadataFieldView* date_precision_field = nullptr;
  const auto text = [](const MetadataFieldView& field) {
    return std::string{*field.string_value};
  };
  const auto signed_32 = [](const MetadataFieldView& field) {
    return std::bit_cast<std::int32_t>(read_u32(field.raw_value, 0));
  };

  for (const auto& field : metadata.fields) {
    switch (field.tag) {
      case 0x0001: result.value.red_player.name = text(field); break;
      case 0x0002: result.value.black_player.name = text(field); break;
      case 0x0003: result.value.red_player.id = text(field); break;
      case 0x0004: result.value.black_player.id = text(field); break;
      case 0x0005: result.value.red_player.country = text(field); break;
      case 0x0006: result.value.black_player.country = text(field); break;
      case 0x0007: result.value.red_player.rating = signed_32(field); break;
      case 0x0008: result.value.black_player.rating = signed_32(field); break;
      case 0x0009: result.value.red_player.title = text(field); break;
      case 0x000a: result.value.black_player.title = text(field); break;
      case 0x000b: result.value.red_player.team = text(field); break;
      case 0x000c: result.value.black_player.team = text(field); break;
      case 0x000d: result.value.red_player.time_used = text(field); break;
      case 0x000e: result.value.black_player.time_used = text(field); break;
      case 0x0010: result.value.event.name = text(field); break;
      case 0x0011: result.value.event.id = text(field); break;
      case 0x0012: result.value.event.location = text(field); break;
      case 0x0013: result.value.event.organizer = text(field); break;
      case 0x0014: result.value.event.round = text(field); break;
      case 0x0015: result.value.event.type = text(field); break;
      case 0x0016: result.value.event.group = text(field); break;
      case 0x0017: result.value.event.board_number = text(field); break;
      case 0x0018: result.value.event.time_control = text(field); break;
      case 0x0020:
        result.value.event.start_time = text(field);
        start_time_field = &field;
        break;
      case 0x0021:
        result.value.event.end_time = text(field);
        end_time_field = &field;
        break;
      case 0x0022:
        result.value.event.date_precision = static_cast<DatePrecision>(read_u32(field.raw_value, 0));
        date_precision_field = &field;
        break;
      case 0x0030:
        result.value.result = static_cast<GameResult>(read_u32(field.raw_value, 0));
        break;
      case 0x0031: result.value.result_text = text(field); break;
      case 0x0040: result.value.opening.name = text(field); break;
      case 0x0041: result.value.opening.code = text(field); break;
      case 0x0042: result.value.opening.id = text(field); break;
      case 0x0050: result.value.title = text(field); break;
      case 0x0051: result.value.tags.push_back(text(field)); break;
      case 0x0052: result.value.game_type = text(field); break;
      case 0x0060: result.value.referee = text(field); break;
      case 0x0061: result.value.recorder = text(field); break;
      case 0x0062: result.value.commentator = text(field); break;
      case 0x0063: result.value.commentator_uri = text(field); break;
      case 0x0064: result.value.creator = text(field); break;
      case 0x0065: result.value.creator_uri = text(field); break;
      case 0x0066: result.value.record_created_at = text(field); break;
      case 0x0067: result.value.record_modified_at = text(field); break;
      case 0x0100: result.value.provenance.source_format = text(field); break;
      case 0x0101: result.value.provenance.source_record_id = text(field); break;
      case 0x0102: result.value.provenance.source_uri = text(field); break;
      case 0x0103: result.value.provenance.import_note = text(field); break;
      case 0x0104: result.value.provenance.source_format_version = text(field); break;
      case 0x0105: result.value.provenance.source_library_id = text(field); break;
      case 0x0106: result.value.provenance.source_library_name = text(field); break;
      case 0x0107: result.value.provenance.source_category = text(field); break;
      case 0x7fff: {
        const auto parsed = parse_restricted_metadata_json(*field.string_value,
                                                           max_extended_metadata_bytes);
        if (std::holds_alternative<RestrictedJsonError>(parsed)) {
          const auto& json_error = std::get<RestrictedJsonError>(parsed);
          return error(CodecErrorCode::invalid_metadata,
                       field.string_data_offset.value_or(field.value_offset) + json_error.offset,
                       "EXTENDED_METADATA", json_error.message);
        }
        auto document = std::get<RestrictedJsonDocument>(parsed);
        result.value.extensions = std::move(document.value);
        result.canonical_extensions = document.canonical;
        break;
      }
      default:
        if (field.standard) {
          return error(CodecErrorCode::invalid_metadata, field.value_offset, "tag",
                       "standard Metadata tag has no GameMetadata projection", {}, field.tag);
        }
        break;
    }
  }

  const bool has_date = start_time_field != nullptr || end_time_field != nullptr;
  if (has_date && date_precision_field == nullptr) {
    const auto* field = start_time_field != nullptr ? start_time_field : end_time_field;
    return error(CodecErrorCode::invalid_metadata, field->value_offset, "DATE_PRECISION",
                 "DATE_PRECISION is required when START_TIME or END_TIME is present");
  }
  if (!has_date && date_precision_field != nullptr) {
    return error(CodecErrorCode::invalid_metadata, date_precision_field->value_offset,
                 "DATE_PRECISION",
                 "DATE_PRECISION must be absent when both date fields are absent");
  }
  if (date_precision_field != nullptr) {
    const auto expected = *result.value.event.date_precision;
    const auto validate_date = [&](const MetadataFieldView* field) -> std::optional<CodecError> {
      if (field == nullptr) {
        return std::nullopt;
      }
      const auto actual = date_time_precision(*field->string_value);
      if (!actual.has_value() ||
          (expected != DatePrecision::unknown && *actual != expected)) {
        return error(CodecErrorCode::invalid_metadata,
                     field->string_data_offset.value_or(field->value_offset),
                     field->tag == 0x0020 ? "START_TIME" : "END_TIME",
                     "date/time is invalid or does not match DATE_PRECISION");
      }
      return std::nullopt;
    };
    if (const auto invalid = validate_date(start_time_field); invalid.has_value()) {
      return *invalid;
    }
    if (const auto invalid = validate_date(end_time_field); invalid.has_value()) {
      return *invalid;
    }
  }
  return result;
}

}  // namespace oxq::core::detail
