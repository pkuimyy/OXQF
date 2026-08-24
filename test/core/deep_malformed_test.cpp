#include "codec/container.hpp"
#include "codec/crc32c.hpp"

#include <oxq/core/codec_error.hpp>
#include <oxq/core/reader.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
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

[[nodiscard]] std::uint32_t read_u32(const std::vector<std::byte>& bytes, std::size_t offset) {
  std::uint32_t result = 0;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    result |= static_cast<std::uint32_t>(
                  std::to_integer<std::uint8_t>(bytes[offset + shift / 8]))
              << shift;
  }
  return result;
}

void write_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xffU);
  }
}

[[nodiscard]] std::size_t entry_offset(const std::vector<std::byte>& bytes,
                                       std::uint32_t type) {
  const std::size_t table_offset = 64;
  const auto count = read_u32(bytes, 0x20);
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t entry = table_offset + index * 40;
    if (read_u32(bytes, entry) == type) {
      return entry;
    }
  }
  return bytes.size();
}

void repair_checksums(std::vector<std::byte>& bytes, std::uint32_t type) {
  const std::size_t entry = entry_offset(bytes, type);
  const auto offset = static_cast<std::size_t>(read_u32(bytes, entry + 8));
  const auto size = static_cast<std::size_t>(read_u32(bytes, entry + 16));
  write_u32(bytes, entry + 32,
            oxq::core::detail::crc32c(
                std::span<const std::byte>{bytes}.subspan(offset, size)));
  const auto count = static_cast<std::size_t>(read_u32(bytes, 0x20));
  write_u32(bytes, 0x38,
            oxq::core::detail::crc32c(
                std::span<const std::byte>{bytes}.subspan(64, count * 40)));
  write_u32(bytes, 0x3c,
            oxq::core::detail::crc32c(std::span<const std::byte>{bytes}.first(0x3c)));
}

[[nodiscard]] bool fails_with(const std::vector<std::byte>& bytes,
                              oxq::core::CodecErrorCode code,
                              std::uint32_t section_type) {
  const auto result = oxq::core::read_oxq(bytes);
  if (!std::holds_alternative<oxq::core::CodecError>(result)) {
    return false;
  }
  const auto& error = std::get<oxq::core::CodecError>(result);
  return error.code == code && error.section_type == section_type;
}

}  // namespace

int main() {
  const std::filesystem::path vectors{OXQF_VECTOR_DIRECTORY};
  const auto source = read_file(vectors / "variation-zh.oxq");
  const auto inspected = oxq::core::detail::inspect_container(source);
  if (!std::holds_alternative<oxq::core::detail::ContainerView>(inspected)) {
    return 1;
  }
  const auto& sections = std::get<oxq::core::detail::ContainerView>(inspected).sections;
  const auto offset_of = [&sections](std::uint32_t type) {
    return std::ranges::find(sections, type, &oxq::core::detail::SectionView::type)->offset;
  };

  auto invalid_string = source;
  invalid_string[offset_of(5) + 12] = std::byte{0xc0};
  repair_checksums(invalid_string, 5);
  if (!fails_with(invalid_string, oxq::core::CodecErrorCode::invalid_utf8, 5)) {
    return 2;
  }

  auto invalid_metadata = source;
  write_u32(invalid_metadata, offset_of(1) + 16, 9);
  repair_checksums(invalid_metadata, 1);
  if (!fails_with(invalid_metadata, oxq::core::CodecErrorCode::invalid_string_ref, 1)) {
    return 3;
  }

  auto invalid_position = source;
  invalid_position[offset_of(2) + 17] = std::byte{90};
  repair_checksums(invalid_position, 2);
  if (!fails_with(invalid_position, oxq::core::CodecErrorCode::invalid_position, 2)) {
    return 4;
  }

  auto invalid_move = source;
  invalid_move[offset_of(3) + 64] = std::byte{90};
  repair_checksums(invalid_move, 3);
  if (!fails_with(invalid_move, oxq::core::CodecErrorCode::invalid_move, 3)) {
    return 5;
  }

  auto invalid_annotation = source;
  write_u16(invalid_annotation, offset_of(4) + 24, 3);
  repair_checksums(invalid_annotation, 4);
  if (!fails_with(invalid_annotation, oxq::core::CodecErrorCode::invalid_annotation, 4)) {
    return 6;
  }
  return 0;
}
