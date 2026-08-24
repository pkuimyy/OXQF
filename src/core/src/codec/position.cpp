#include "codec/position.hpp"

#include "codec/binary.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace oxq::core::detail {
namespace {

constexpr std::uint32_t kPositionSectionType = 2;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kPieceRecordSize = 4;
constexpr std::size_t kMaxPieces = 32;

[[nodiscard]] CodecError error(CodecErrorCode code, std::size_t offset, std::string field,
                               std::string message, std::optional<std::uint64_t> expected = {},
                               std::optional<std::uint64_t> actual = {}) {
  return {code, offset, kPositionSectionType, std::move(field), std::move(message), expected, actual};
}

}  // namespace

PositionResult read_position(std::span<const std::byte> input, const ContainerView& container) {
  const auto section = std::ranges::find(container.sections, kPositionSectionType,
                                         &SectionView::type);
  if (section == container.sections.end()) {
    return error(CodecErrorCode::invalid_section_table, 0, "section_type",
                 "POSITION Section is missing");
  }
  if (!contains(input, section->offset, section->size)) {
    return error(CodecErrorCode::section_out_of_range, section->offset, "section_range",
                 "POSITION Section extends beyond input");
  }
  const auto payload = input.subspan(section->offset, section->size);
  if (payload.size() < kHeaderSize) {
    return error(CodecErrorCode::invalid_position, section->offset, "header",
                 "POSITION is shorter than its 16-byte header", kHeaderSize, payload.size());
  }

  const auto version = read_u16(payload, 0);
  const auto header_size = read_u16(payload, 2);
  if (version != 1) {
    return error(CodecErrorCode::unsupported_version, section->offset, "section_version",
                 "unsupported POSITION Section version", 1, version);
  }
  if (header_size != kHeaderSize) {
    return error(CodecErrorCode::invalid_position, section->offset + 2, "header_size",
                 "POSITION header_size must be 16", kHeaderSize, header_size);
  }
  const auto variant = read_u16(payload, 4);
  if (variant != 1) {
    return error(CodecErrorCode::invalid_position, section->offset + 4, "variant",
                 "POSITION variant must identify Chinese chess", 1, variant);
  }
  const auto side_to_move = std::to_integer<std::uint8_t>(payload[6]);
  if (side_to_move > 1) {
    return error(CodecErrorCode::invalid_position, section->offset + 6, "side_to_move",
                 "POSITION side_to_move must be red or black", {}, side_to_move);
  }
  const auto position_flags = std::to_integer<std::uint8_t>(payload[7]);
  if (position_flags != 0) {
    return error(CodecErrorCode::invalid_position, section->offset + 7, "position_flags",
                 "POSITION flags must be zero in v1", 0, position_flags);
  }
  const auto fullmove_number = read_u16(payload, 8);
  if (fullmove_number == 0) {
    return error(CodecErrorCode::invalid_position, section->offset + 8, "fullmove_number",
                 "POSITION fullmove_number must be at least 1", 1, fullmove_number);
  }
  const auto piece_count = static_cast<std::size_t>(read_u16(payload, 10));
  if (piece_count > kMaxPieces) {
    return error(CodecErrorCode::resource_limit, section->offset + 10, "piece_count",
                 "POSITION piece_count exceeds the v1 limit", kMaxPieces, piece_count);
  }
  const auto reserved = read_u32(payload, 12);
  if (reserved != 0) {
    return error(CodecErrorCode::invalid_position, section->offset + 12, "reserved",
                 "POSITION reserved bytes must be zero", 0, reserved);
  }

  std::size_t records_size = 0;
  std::size_t expected_size = 0;
  if (!checked_multiply(piece_count, kPieceRecordSize, records_size) ||
      !checked_add(kHeaderSize, records_size, expected_size)) {
    return error(CodecErrorCode::integer_overflow, section->offset + 10, "piece_count",
                 "POSITION size calculation overflowed", {}, piece_count);
  }
  if (payload.size() != expected_size) {
    return error(CodecErrorCode::invalid_position, section->offset + 10, "piece_count",
                 "POSITION size does not match piece_count", expected_size, payload.size());
  }

  PositionView result;
  result.value.side_to_move = side_to_move == 0 ? Side::red : Side::black;
  result.value.fullmove_number = fullmove_number;
  result.value.pieces.reserve(piece_count);
  std::array<bool, 90> occupied{};
  std::optional<std::uint8_t> previous_square;

  for (std::size_t index = 0; index < piece_count; ++index) {
    const std::size_t cursor = kHeaderSize + index * kPieceRecordSize;
    const auto piece_code = std::to_integer<std::uint8_t>(payload[cursor]);
    const auto square = std::to_integer<std::uint8_t>(payload[cursor + 1]);
    const auto piece_flags = read_u16(payload, cursor + 2);
    const auto type = static_cast<std::uint8_t>(piece_code & 0x0fU);
    if ((piece_code & 0x70U) != 0 || type < 1 || type > 7) {
      return error(CodecErrorCode::invalid_position, section->offset + cursor, "piece_code",
                   "POSITION piece_code contains reserved bits or an invalid piece type", {},
                   piece_code);
    }
    if (square > 89) {
      return error(CodecErrorCode::invalid_position, section->offset + cursor + 1, "square",
                   "POSITION square is outside 0..89", 89, square);
    }
    if (piece_flags != 0) {
      return error(CodecErrorCode::invalid_position, section->offset + cursor + 2, "piece_flags",
                   "POSITION piece_flags must be zero in v1", 0, piece_flags);
    }
    if (occupied[square]) {
      return error(CodecErrorCode::invalid_position, section->offset + cursor + 1, "square",
                   "POSITION contains more than one piece on a square", {}, square);
    }
    occupied[square] = true;
    if (previous_square.has_value() && square <= *previous_square) {
      result.canonical_order = false;
    }
    previous_square = square;
    result.value.pieces.push_back(
        {static_cast<Side>((piece_code >> 7U) & 1U), static_cast<PieceType>(type), square});
  }
  return result;
}

}  // namespace oxq::core::detail
