#include "codec/container.hpp"
#include "codec/position.hpp"

#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> characters{std::istreambuf_iterator<char>{stream},
                                     std::istreambuf_iterator<char>{}};
  std::vector<std::byte> result;
  result.reserve(characters.size());
  for (const char character : characters) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return result;
}

void write_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

[[nodiscard]] bool error_is(const oxq::core::detail::PositionResult& result,
                            oxq::core::CodecErrorCode code, std::size_t offset) {
  if (!std::holds_alternative<oxq::core::CodecError>(result)) {
    return false;
  }
  const auto& error = std::get<oxq::core::CodecError>(result);
  return error.code == code && error.offset == offset && error.section_type == 2;
}

}  // namespace

int main() {
  const std::filesystem::path vectors{OXQF_VECTOR_DIRECTORY};
  const auto minimal_bytes = read_file(vectors / "minimal.oxq");
  const auto minimal_container_result = oxq::core::detail::inspect_container(minimal_bytes);
  if (!std::holds_alternative<oxq::core::detail::ContainerView>(minimal_container_result)) {
    return 1;
  }
  const auto minimal_result = oxq::core::detail::read_position(
      minimal_bytes, std::get<oxq::core::detail::ContainerView>(minimal_container_result));
  if (!std::holds_alternative<oxq::core::detail::PositionView>(minimal_result)) {
    return 2;
  }
  const auto& minimal = std::get<oxq::core::detail::PositionView>(minimal_result);
  if (minimal.value.side_to_move != oxq::core::Side::red ||
      minimal.value.fullmove_number != 1 || !minimal.value.pieces.empty() ||
      !minimal.canonical_order) {
    return 3;
  }

  const auto variation_bytes = read_file(vectors / "variation-zh.oxq");
  const auto variation_container_result = oxq::core::detail::inspect_container(variation_bytes);
  if (!std::holds_alternative<oxq::core::detail::ContainerView>(variation_container_result)) {
    return 4;
  }
  const auto variation_result = oxq::core::detail::read_position(
      variation_bytes, std::get<oxq::core::detail::ContainerView>(variation_container_result));
  if (!std::holds_alternative<oxq::core::detail::PositionView>(variation_result)) {
    return 5;
  }
  const auto& variation = std::get<oxq::core::detail::PositionView>(variation_result);
  const std::vector<oxq::core::Piece> expected{
      {oxq::core::Side::red, oxq::core::PieceType::king, 4},
      {oxq::core::Side::red, oxq::core::PieceType::cannon, 19},
      {oxq::core::Side::red, oxq::core::PieceType::rook, 27},
      {oxq::core::Side::black, oxq::core::PieceType::rook, 63},
      {oxq::core::Side::black, oxq::core::PieceType::king, 85},
  };
  if (variation.value.pieces != expected || !variation.canonical_order) {
    return 6;
  }

  std::vector<std::byte> position{
      std::byte{1}, std::byte{0}, std::byte{16}, std::byte{0},
      std::byte{1}, std::byte{0}, std::byte{1}, std::byte{0},
      std::byte{7}, std::byte{0}, std::byte{2}, std::byte{0},
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{0x85}, std::byte{80}, std::byte{0}, std::byte{0},
      std::byte{0x01}, std::byte{4}, std::byte{0}, std::byte{0},
  };
  oxq::core::detail::ContainerView synthetic;
  synthetic.sections.push_back({2, 1, 0, position.size(), 0});
  const auto noncanonical_result = oxq::core::detail::read_position(position, synthetic);
  if (!std::holds_alternative<oxq::core::detail::PositionView>(noncanonical_result)) {
    return 7;
  }
  const auto& noncanonical = std::get<oxq::core::detail::PositionView>(noncanonical_result);
  if (noncanonical.canonical_order || noncanonical.value.side_to_move != oxq::core::Side::black ||
      noncanonical.value.fullmove_number != 7 || noncanonical.value.pieces.size() != 2) {
    return 8;
  }

  auto invalid = position;
  invalid[6] = std::byte{2};
  if (!error_is(oxq::core::detail::read_position(invalid, synthetic),
                oxq::core::CodecErrorCode::invalid_position, 6)) {
    return 9;
  }
  invalid = position;
  invalid[16] = std::byte{0};
  if (!error_is(oxq::core::detail::read_position(invalid, synthetic),
                oxq::core::CodecErrorCode::invalid_position, 16)) {
    return 10;
  }
  invalid = position;
  invalid[17] = std::byte{90};
  if (!error_is(oxq::core::detail::read_position(invalid, synthetic),
                oxq::core::CodecErrorCode::invalid_position, 17)) {
    return 11;
  }
  invalid = position;
  invalid[21] = std::byte{80};
  if (!error_is(oxq::core::detail::read_position(invalid, synthetic),
                oxq::core::CodecErrorCode::invalid_position, 21)) {
    return 12;
  }
  invalid = position;
  invalid[18] = std::byte{1};
  if (!error_is(oxq::core::detail::read_position(invalid, synthetic),
                oxq::core::CodecErrorCode::invalid_position, 18)) {
    return 13;
  }
  invalid = position;
  write_u16(invalid, 10, 33);
  if (!error_is(oxq::core::detail::read_position(invalid, synthetic),
                oxq::core::CodecErrorCode::resource_limit, 10)) {
    return 14;
  }
  invalid = position;
  write_u16(invalid, 10, 1);
  if (!error_is(oxq::core::detail::read_position(invalid, synthetic),
                oxq::core::CodecErrorCode::invalid_position, 10)) {
    return 15;
  }
  return 0;
}
