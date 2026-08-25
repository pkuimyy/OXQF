#include "cbl/record_writer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oxq::convert::detail {
namespace {

constexpr std::string_view kExtensionNamespace = "org.openxiangqi.cbl";
constexpr std::array<std::uint8_t, 16> kRecordMagic{
    0x43, 0x43, 0x42, 0x72, 0x69, 0x64, 0x67, 0x65,
    0x20, 0x52, 0x65, 0x63, 0x6f, 0x72, 0x64, 0x00};

void write_u16(std::span<std::byte> output, std::size_t offset,
               std::uint16_t value) noexcept {
  output[offset] = static_cast<std::byte>(value & 0xffU);
  output[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_u32(std::span<std::byte> output, std::size_t offset,
               std::uint32_t value) noexcept {
  for (unsigned index = 0; index < 4; ++index) {
    output[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void write_windows_guid(std::span<std::byte> output, std::size_t offset,
                        const core::Uuid& uuid) noexcept {
  constexpr std::array<std::size_t, 16> order{
      3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
  for (std::size_t index = 0; index < order.size(); ++index) {
    output[offset + order[index]] = static_cast<std::byte>(uuid.bytes[index]);
  }
}

[[nodiscard]] std::uint32_t next_code_point(std::string_view text,
                                            std::size_t& cursor) noexcept {
  const auto first = static_cast<unsigned char>(text[cursor++]);
  if (first < 0x80U) {
    return first;
  }
  std::size_t continuation_count = 0;
  std::uint32_t result = 0;
  if ((first & 0xe0U) == 0xc0U) {
    continuation_count = 1;
    result = first & 0x1fU;
  } else if ((first & 0xf0U) == 0xe0U) {
    continuation_count = 2;
    result = first & 0x0fU;
  } else {
    continuation_count = 3;
    result = first & 0x07U;
  }
  for (std::size_t index = 0; index < continuation_count; ++index) {
    result = (result << 6U) |
             (static_cast<unsigned char>(text[cursor++]) & 0x3fU);
  }
  return result;
}

void write_utf16_slot(std::span<std::byte> output, std::size_t offset,
                      std::size_t slot_size, std::string_view text) noexcept {
  std::size_t cursor = 0;
  std::size_t destination = offset;
  const auto content_end = offset + slot_size - 4U;
  while (cursor < text.size()) {
    const auto code_point = next_code_point(text, cursor);
    if (code_point <= 0xffffU) {
      if (destination > content_end) {
        return;
      }
      write_u16(output, destination, static_cast<std::uint16_t>(code_point));
      destination += 2;
    } else {
      if (destination > content_end || content_end - destination < 2U) {
        return;
      }
      const auto value = code_point - 0x10000U;
      write_u16(output, destination,
                static_cast<std::uint16_t>(0xd800U + (value >> 10U)));
      write_u16(output, destination + 2,
                static_cast<std::uint16_t>(0xdc00U + (value & 0x3ffU)));
      destination += 4;
    }
  }
  // The zero-initialized output already provides the required NUL and the
  // rest of the fixed slot. Preflight guarantees destination is in bounds.
}

[[nodiscard]] const core::ExtensionValue* extension_value(
    const core::GameModel& game, std::string_view key) noexcept {
  const auto name_space = game.metadata.extensions.find(kExtensionNamespace);
  if (name_space == game.metadata.extensions.end()) {
    return nullptr;
  }
  const auto property = name_space->second.find(key);
  return property == name_space->second.end() ? nullptr : &property->second;
}

[[nodiscard]] std::optional<std::uint32_t> decimal_extension(
    const core::GameModel& game, std::string_view key,
    std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max()) noexcept {
  const auto* value = extension_value(game, key);
  if (value == nullptr || !std::holds_alternative<std::string>(*value)) {
    return std::nullopt;
  }
  const auto& text = std::get<std::string>(*value);
  std::uint32_t result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size() || result > maximum) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] std::optional<std::uint32_t> hex_extension(
    const core::GameModel& game, std::string_view key) noexcept {
  const auto* value = extension_value(game, key);
  if (value == nullptr || !std::holds_alternative<std::string>(*value)) {
    return std::nullopt;
  }
  const auto& text = std::get<std::string>(*value);
  std::uint32_t result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                      result, 16);
  if (text.size() != 8U || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] std::uint16_t source_root_opaque_control(
    const core::GameModel& game) noexcept {
  const auto* value = extension_value(game, "source_controls");
  if (value == nullptr ||
      !std::holds_alternative<std::vector<std::string>>(*value)) {
    return 0;
  }
  const auto& controls = std::get<std::vector<std::string>>(*value);
  if (controls.size() != game.move_tree.nodes.size() || controls.empty()) {
    return 0;
  }
  std::uint16_t control = 0;
  const auto parsed = std::from_chars(controls[0].data(),
                                      controls[0].data() + controls[0].size(),
                                      control, 16);
  if (controls[0].size() != 4U || parsed.ec != std::errc{} ||
      parsed.ptr != controls[0].data() + controls[0].size()) {
    return 0;
  }
  return control & 0xfff8U;
}

[[nodiscard]] std::uint8_t piece_code(const core::Piece& piece) noexcept {
  std::uint8_t kind = 0;
  switch (piece.type) {
    case core::PieceType::rook: kind = 1; break;
    case core::PieceType::horse: kind = 2; break;
    case core::PieceType::elephant: kind = 3; break;
    case core::PieceType::advisor: kind = 4; break;
    case core::PieceType::king: kind = 5; break;
    case core::PieceType::cannon: kind = 6; break;
    case core::PieceType::pawn: kind = 7; break;
  }
  return static_cast<std::uint8_t>(
      (piece.side == core::Side::red ? 0x10U : 0x20U) + kind);
}

[[nodiscard]] std::uint32_t result_value(
    const core::GameModel& game) noexcept {
  if (const auto source = decimal_extension(game, "result", 4);
      source.has_value()) {
    return *source;
  }
  if (!game.metadata.result.has_value()) {
    return 0;
  }
  switch (*game.metadata.result) {
    case core::GameResult::red_win: return 1;
    case core::GameResult::black_win: return 2;
    case core::GameResult::draw: return 3;
    case core::GameResult::unknown:
    case core::GameResult::unfinished:
    case core::GameResult::aborted: return 0;
  }
  return 0;
}

}  // namespace

std::vector<std::byte> encode_cbl_record_prefix(const core::GameModel& game) {
  std::vector<std::byte> output(cbl_record_prefix_size);
  std::ranges::transform(kRecordMagic, output.begin(), [](const auto byte) {
    return static_cast<std::byte>(byte);
  });
  write_u32(output, 0x10, 0x02000000U);
  write_windows_guid(output, 0x14, game.uuid);

  const auto& metadata = game.metadata;
  const auto write_optional = [&output](std::size_t offset, std::size_t size,
                                        const std::optional<std::string>& value) {
    if (value.has_value()) {
      write_utf16_slot(output, offset, size, *value);
    }
  };
  write_optional(0x0b4, 0x80, metadata.title);
  write_optional(0x134, 0x100, metadata.provenance.source_category);
  write_optional(0x234, 0x40, metadata.provenance.source_uri);
  write_optional(0x274, 0x40, metadata.event.type);
  write_optional(0x2b4, 0x40, metadata.event.name);
  write_optional(0x2f4, 0x40, metadata.event.round);
  write_optional(0x334, 0x20, metadata.event.group);
  write_optional(0x354, 0x20, metadata.event.board_number);
  if (metadata.event.date_precision == core::DatePrecision::day) {
    write_optional(0x374, 0x40, metadata.event.start_time);
  }
  write_optional(0x3b4, 0x40, metadata.event.location);
  write_optional(0x3f4, 0x40, metadata.event.time_control);
  write_optional(0x434, 0x40, metadata.red_player.name);
  write_optional(0x474, 0x40, metadata.red_player.team);
  write_optional(0x4b4, 0x40, metadata.red_player.time_used);
  if (metadata.red_player.rating.has_value()) {
    write_utf16_slot(output, 0x4f4, 0x20,
                     std::to_string(*metadata.red_player.rating));
  }
  write_optional(0x514, 0x40, metadata.black_player.name);
  write_optional(0x554, 0x40, metadata.black_player.team);
  write_optional(0x594, 0x40, metadata.black_player.time_used);
  if (metadata.black_player.rating.has_value()) {
    write_utf16_slot(output, 0x5d4, 0x20,
                     std::to_string(*metadata.black_player.rating));
  }
  write_optional(0x5f4, 0x40, metadata.referee);
  write_optional(0x634, 0x40, metadata.recorder);
  write_optional(0x674, 0x40, metadata.commentator);
  write_optional(0x6b4, 0x40, metadata.commentator_uri);
  write_optional(0x6f4, 0x40, metadata.creator);
  write_optional(0x734, 0x40, metadata.creator_uri);
  write_optional(0x774, 0x40, metadata.record_created_at);
  write_optional(0x7b4, 0x40, metadata.record_modified_at);
  if (metadata.opening.code.has_value()) {
    std::ranges::transform(*metadata.opening.code, output.begin() + 0x7f4,
                           [](const char character) {
                             return static_cast<std::byte>(
                                 static_cast<unsigned char>(character));
                           });
  }
  write_u32(output, 0x7f8,
            decimal_extension(game, "record_type", 4).value_or(0));
  write_optional(0x7fc, 0x20, metadata.game_type);
  write_u32(output, 0x81c, result_value(game));
  write_optional(0x820, 0x20, metadata.result_text);
  write_u32(output, 0x840,
            game.initial_position.side_to_move == core::Side::red ? 1U : 2U);
  write_u32(output, 0x844,
            decimal_extension(game, "source_fullmove_number")
                .value_or(game.initial_position.fullmove_number));

  for (const auto& piece : game.initial_position.pieces) {
    const auto rank = static_cast<std::uint8_t>(piece.square / 9U);
    const auto file = static_cast<std::uint8_t>(piece.square % 9U);
    const auto disk_square = static_cast<std::uint8_t>((9U - rank) * 9U + file);
    output[0x848 + disk_square] = static_cast<std::byte>(piece_code(piece));
  }

  write_u32(output, 0x8a2,
            hex_extension(game, "root_marker").value_or(0xffffffffU));
  std::uint16_t root_control = source_root_opaque_control(game);
  if (game.move_tree.nodes[0].children.empty()) {
    root_control |= 0x0001U;
  }
  if (!game.move_tree.nodes[0].annotations.empty()) {
    root_control |= 0x0004U;
  }
  write_u16(output, 0x8a6, root_control);
  // Root reserved at 0x8a8 remains zero.
  return output;
}

}  // namespace oxq::convert::detail
