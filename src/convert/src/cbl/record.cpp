#include "cbl/record.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace oxq::convert::detail {
namespace {

constexpr std::size_t kRecordPrefixSize = 0x8aa;
constexpr std::array<std::uint8_t, 16> kRecordMagic{
    0x43, 0x43, 0x42, 0x72, 0x69, 0x64, 0x67, 0x65,
    0x20, 0x52, 0x65, 0x63, 0x6f, 0x72, 0x64, 0x00};

[[nodiscard]] CblError error(CblErrorCode code, std::size_t offset, std::string field,
                             std::string message, std::size_t slot,
                             std::optional<std::uint64_t> expected = {},
                             std::optional<std::uint64_t> actual = {}) {
  return {code, offset, slot, std::move(field), std::move(message), expected, actual};
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::byte> input,
                                     std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[offset])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[offset + 1])) << 8U);
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> input,
                                     std::size_t offset) noexcept {
  std::uint32_t result = 0;
  for (unsigned index = 0; index < 4; ++index) {
    result |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[offset + index]))
              << (index * 8U);
  }
  return result;
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

using StringOutcome = std::variant<std::string, CblError>;

[[nodiscard]] StringOutcome decode_utf16_slot(std::span<const std::byte> record,
                                               std::size_t relative_offset,
                                               std::size_t size,
                                               const CblDirectoryEntryView& entry,
                                               std::string field) {
  std::string result;
  result.reserve(size / 2);
  for (std::size_t cursor = 0; cursor < size; cursor += 2) {
    const auto first = read_u16(record, relative_offset + cursor);
    if (first == 0) {
      return result;
    }
    std::uint32_t code_point = first;
    if (first >= 0xd800U && first <= 0xdbffU) {
      if (cursor + 4 > size) {
        return error(CblErrorCode::invalid_utf16,
                     entry.resource_offset + relative_offset + cursor, std::move(field),
                     "high surrogate is truncated by the fixed Record slot",
                     entry.physical_slot);
      }
      const auto second = read_u16(record, relative_offset + cursor + 2);
      if (second < 0xdc00U || second > 0xdfffU) {
        return error(CblErrorCode::invalid_utf16,
                     entry.resource_offset + relative_offset + cursor, std::move(field),
                     "high surrogate is not followed by a low surrogate",
                     entry.physical_slot);
      }
      code_point = 0x10000U +
                   ((static_cast<std::uint32_t>(first) - 0xd800U) << 10U) +
                   (static_cast<std::uint32_t>(second) - 0xdc00U);
      cursor += 2;
    } else if (first >= 0xdc00U && first <= 0xdfffU) {
      return error(CblErrorCode::invalid_utf16,
                   entry.resource_offset + relative_offset + cursor, std::move(field),
                   "unpaired low surrogate in Record slot", entry.physical_slot);
    }
    append_utf8(result, code_point);
  }
  return result;
}

[[nodiscard]] core::Uuid decode_windows_guid(std::span<const std::byte> input,
                                             std::size_t offset) {
  core::Uuid result;
  constexpr std::array<std::size_t, 16> order{
      3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
  for (std::size_t index = 0; index < order.size(); ++index) {
    result.bytes[index] = std::to_integer<std::uint8_t>(input[offset + order[index]]);
  }
  return result;
}

[[nodiscard]] StringOutcome decode_ascii_slot(std::span<const std::byte> record,
                                               std::size_t relative_offset,
                                               std::size_t size,
                                               const CblDirectoryEntryView& entry,
                                               std::string field) {
  std::string result;
  for (std::size_t cursor = 0; cursor < size; ++cursor) {
    const auto value = std::to_integer<std::uint8_t>(record[relative_offset + cursor]);
    if (value == 0) {
      return result;
    }
    if (value > 0x7fU) {
      return error(CblErrorCode::invalid_record,
                   entry.resource_offset + relative_offset + cursor, std::move(field),
                   "ASCII Record slot contains a non-ASCII byte", entry.physical_slot);
    }
    result.push_back(static_cast<char>(value));
  }
  return result;
}

[[nodiscard]] std::optional<core::Piece> decode_piece(std::uint8_t code,
                                                       std::uint8_t square) {
  if (code == 0) {
    return std::nullopt;
  }
  core::Side side;
  std::uint8_t kind_code = 0;
  if (code >= 0x11 && code <= 0x17) {
    side = core::Side::red;
    kind_code = static_cast<std::uint8_t>(code - 0x10);
  } else if (code >= 0x21 && code <= 0x27) {
    side = core::Side::black;
    kind_code = static_cast<std::uint8_t>(code - 0x20);
  } else {
    return std::nullopt;
  }
  constexpr std::array kinds{
      core::PieceType::pawn,
      core::PieceType::rook,
      core::PieceType::horse,
      core::PieceType::elephant,
      core::PieceType::advisor,
      core::PieceType::king,
      core::PieceType::cannon,
      core::PieceType::pawn,
  };
  return core::Piece{side, kinds[kind_code], square};
}

}  // namespace

CblRecordOutcome read_cbl_record(const CblContainerView& container,
                                 const CblDirectoryEntryView& entry) {
  if (entry.kind != CblResourceKind::live_game) {
    return error(CblErrorCode::invalid_record, entry.resource_offset, "resource_kind",
                 "only a live CCB resource can be decoded as a game Record",
                 entry.physical_slot);
  }
  if (entry.used_size < kRecordPrefixSize ||
      entry.resource_offset > container.input.size() ||
      entry.used_size > container.input.size() - entry.resource_offset) {
    return error(CblErrorCode::truncated_input, entry.resource_offset, "record_prefix",
                 "CCB Record is shorter than its fixed v3 prefix", entry.physical_slot,
                 kRecordPrefixSize, entry.used_size);
  }
  const auto record = container.input.subspan(entry.resource_offset, entry.used_size);
  for (std::size_t index = 0; index < kRecordMagic.size(); ++index) {
    if (std::to_integer<std::uint8_t>(record[index]) != kRecordMagic[index]) {
      return error(CblErrorCode::invalid_record, entry.resource_offset + index,
                   "record_magic", "CCB Record Magic differs", entry.physical_slot,
                   kRecordMagic[index], std::to_integer<std::uint8_t>(record[index]));
    }
  }
  const auto version = read_u32(record, 0x10);
  if (version != 0x02000000U) {
    return error(CblErrorCode::unsupported_version, entry.resource_offset + 0x10,
                 "record_version", "only the current CCB v3 Record layout is supported",
                 entry.physical_slot, 0x02000000U, version);
  }

  CblRecordView result;
  result.guid = decode_windows_guid(record, 0x14);
  struct TextField {
    std::size_t offset;
    std::size_t size;
    const char* name;
    std::string CblRecordMetadata::*destination;
  };
  constexpr std::array<TextField, 28> text_fields{{
      {0x0b4, 0x080, "name", &CblRecordMetadata::name},
      {0x134, 0x100, "url_or_category", &CblRecordMetadata::url_or_category},
      {0x234, 0x040, "source", &CblRecordMetadata::source},
      {0x274, 0x040, "contest_type", &CblRecordMetadata::contest_type},
      {0x2b4, 0x040, "contest", &CblRecordMetadata::contest},
      {0x2f4, 0x040, "round", &CblRecordMetadata::round},
      {0x334, 0x020, "group", &CblRecordMetadata::group},
      {0x354, 0x020, "table", &CblRecordMetadata::table},
      {0x374, 0x040, "date", &CblRecordMetadata::date},
      {0x3b4, 0x040, "site", &CblRecordMetadata::site},
      {0x3f4, 0x040, "time_rule", &CblRecordMetadata::time_rule},
      {0x434, 0x040, "red", &CblRecordMetadata::red},
      {0x474, 0x040, "red_team", &CblRecordMetadata::red_team},
      {0x4b4, 0x040, "red_time", &CblRecordMetadata::red_time},
      {0x4f4, 0x020, "red_rating", &CblRecordMetadata::red_rating},
      {0x514, 0x040, "black", &CblRecordMetadata::black},
      {0x554, 0x040, "black_team", &CblRecordMetadata::black_team},
      {0x594, 0x040, "black_time", &CblRecordMetadata::black_time},
      {0x5d4, 0x020, "black_rating", &CblRecordMetadata::black_rating},
      {0x5f4, 0x040, "referee", &CblRecordMetadata::referee},
      {0x634, 0x040, "recorder", &CblRecordMetadata::recorder},
      {0x674, 0x040, "commentator", &CblRecordMetadata::commentator},
      {0x6b4, 0x040, "commentator_uri", &CblRecordMetadata::commentator_uri},
      {0x6f4, 0x040, "creator", &CblRecordMetadata::creator},
      {0x734, 0x040, "creator_uri", &CblRecordMetadata::creator_uri},
      {0x774, 0x040, "created_at", &CblRecordMetadata::created_at},
      {0x7b4, 0x040, "modified_at", &CblRecordMetadata::modified_at},
      {0x7fc, 0x020, "record_kind", &CblRecordMetadata::record_kind},
  }};
  for (const auto& field : text_fields) {
    auto decoded = decode_utf16_slot(record, field.offset, field.size, entry, field.name);
    if (std::holds_alternative<CblError>(decoded)) {
      return std::get<CblError>(std::move(decoded));
    }
    result.metadata.*(field.destination) = std::get<std::string>(std::move(decoded));
  }
  auto ecco = decode_ascii_slot(record, 0x7f4, 4, entry, "ecco_code");
  if (std::holds_alternative<CblError>(ecco)) {
    return std::get<CblError>(std::move(ecco));
  }
  result.metadata.ecco_code = std::get<std::string>(std::move(ecco));
  result.metadata.record_type = read_u32(record, 0x7f8);
  result.metadata.result = read_u32(record, 0x81c);
  auto result_text = decode_utf16_slot(record, 0x820, 0x20, entry, "result_text");
  if (std::holds_alternative<CblError>(result_text)) {
    return std::get<CblError>(std::move(result_text));
  }
  result.metadata.result_text = std::get<std::string>(std::move(result_text));

  const auto side = read_u32(record, 0x840);
  if (side != 1 && side != 2) {
    return error(CblErrorCode::invalid_record, entry.resource_offset + 0x840,
                 "side_to_move", "CBL SideToMove must be 1 or 2",
                 entry.physical_slot, {}, side);
  }
  result.position.side_to_move = side == 1 ? core::Side::red : core::Side::black;
  result.source_fullmove_number = read_u32(record, 0x844);
  result.position.fullmove_number = static_cast<std::uint16_t>(
      result.source_fullmove_number == 0 ||
              result.source_fullmove_number > std::numeric_limits<std::uint16_t>::max()
          ? 1
          : result.source_fullmove_number);
  result.position.pieces.reserve(32);
  for (std::uint8_t disk_square = 0; disk_square < 90; ++disk_square) {
    const auto code = std::to_integer<std::uint8_t>(record[0x848 + disk_square]);
    const auto disk_rank = static_cast<std::uint8_t>(disk_square / 9);
    const auto file = static_cast<std::uint8_t>(disk_square % 9);
    const auto square = static_cast<std::uint8_t>((9 - disk_rank) * 9 + file);
    const auto piece = decode_piece(code, square);
    if (code != 0 && !piece.has_value()) {
      return error(CblErrorCode::invalid_record,
                   entry.resource_offset + 0x848 + disk_square,
                   "board_piece", "CBL Board contains an unknown piece code",
                   entry.physical_slot, {}, code);
    }
    if (piece.has_value()) {
      result.position.pieces.push_back(*piece);
    }
  }
  std::ranges::sort(result.position.pieces, {}, &core::Piece::square);
  result.root_marker = read_u32(record, 0x8a2);
  result.root_control = read_u16(record, 0x8a6);
  const auto root_reserved = read_u16(record, 0x8a8);
  if (root_reserved != 0) {
    return error(CblErrorCode::invalid_record, entry.resource_offset + 0x8a8,
                 "root_reserved", "CBL Root Header reserved field is nonzero",
                 entry.physical_slot, 0, root_reserved);
  }
  result.node_stream_offset = kRecordPrefixSize;
  result.used_size = entry.used_size;
  return result;
}

}  // namespace oxq::convert::detail
