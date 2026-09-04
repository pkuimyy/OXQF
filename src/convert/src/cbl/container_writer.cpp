#include "cbl/container_writer.hpp"

#include "cbl/tree_writer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oxq::convert::detail {
namespace {

constexpr std::size_t kDirectoryOffset = 0x10440;
constexpr std::size_t kDirectoryEntrySize = 0x114;
constexpr std::size_t kBlockSize = 0x1000;
constexpr std::array<std::uint8_t, 16> kLibraryMagic{
    0x43, 0x43, 0x42, 0x72, 0x69, 0x64, 0x67, 0x65,
    0x4c, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x00};

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
  while (cursor < text.size()) {
    const auto code_point = next_code_point(text, cursor);
    if (code_point <= 0xffffU) {
      write_u16(output, destination, static_cast<std::uint16_t>(code_point));
      destination += 2;
    } else {
      const auto value = code_point - 0x10000U;
      write_u16(output, destination,
                static_cast<std::uint16_t>(0xd800U + (value >> 10U)));
      write_u16(output, destination + 2,
                static_cast<std::uint16_t>(0xdc00U + (value & 0x3ffU)));
      destination += 4;
    }
  }
  (void)slot_size;
  // Preflight guarantees room for the content and its zero terminator.
}

[[nodiscard]] std::string directory_uuid(const core::Uuid& uuid) {
  auto text = uuid.to_string();
  std::ranges::transform(text, text.begin(), [](const char character) {
    return character >= 'a' && character <= 'f'
               ? static_cast<char>(character - 'a' + 'A')
               : character;
  });
  return '{' + text + '}';
}

}  // namespace

CblContainerWriteOutcome encode_cbl_container(
    std::span<const core::GameModel> games,
    const CblWriteOptions& options,
    const CblWritePlan& plan) {
  std::vector<std::byte> output(plan.projected_file_size);
  std::ranges::transform(kLibraryMagic, output.begin(), [](const auto value) {
    return static_cast<std::byte>(value);
  });
  write_u32(output, 0x10, 3);
  write_windows_guid(output, 0x14, plan.library_uuid);
  write_u32(output, 0x34, 0x7fffffffU);
  write_u32(output, 0x3c,
            static_cast<std::uint32_t>(plan.directory_capacity));
  write_utf16_slot(output, 0x040, 0x300, options.library.name);
  write_utf16_slot(output, 0x340, 0x040, options.library.author);
  write_utf16_slot(output, 0x380, 0x040, options.library.author_email);
  write_utf16_slot(output, 0x3c0, 0x040, options.library.created_at);
  write_utf16_slot(output, 0x400, 0x040, options.library.modified_at);

  std::size_t resource_offset =
      kDirectoryOffset + plan.directory_capacity * kDirectoryEntrySize;
  for (std::size_t game_index = 0; game_index < games.size(); ++game_index) {
    const auto& game = games[game_index];
    auto record = encode_cbl_record(game);
    if (record.size() != plan.record_sizes[game_index]) {
      return CblWriteError{
          CblWriteErrorCode::encoding_invariant,
          game_index,
          {},
          "record_size",
          "CBL Record encoder size differs from its accepted preflight plan",
          plan.record_sizes[game_index],
          record.size()};
    }
    const auto block_count = record.size() / kBlockSize +
                             (record.size() % kBlockSize == 0 ? 0U : 1U);
    const auto entry_offset =
        kDirectoryOffset + game_index * kDirectoryEntrySize;
    output[entry_offset] = std::byte{0x07};
    write_u32(output, entry_offset + 4,
              static_cast<std::uint32_t>(game_index));
    write_u32(output, entry_offset + 8,
              static_cast<std::uint32_t>(block_count));
    write_u32(output, entry_offset + 12,
              static_cast<std::uint32_t>(record.size()));
    write_utf16_slot(output, entry_offset + 0x14, 80,
                     directory_uuid(game.uuid));
    if (game.metadata.title.has_value()) {
      write_utf16_slot(output, entry_offset + 0x64, 176,
                       *game.metadata.title);
    }
    std::ranges::copy(record, output.begin() +
                                  static_cast<std::ptrdiff_t>(resource_offset));
    resource_offset += block_count * kBlockSize;
  }
  if (resource_offset != output.size()) {
    return CblWriteError{
        CblWriteErrorCode::encoding_invariant,
        {},
        {},
        "output_size",
        "CBL container closure differs from its accepted preflight plan",
        output.size(),
        resource_offset};
  }
  return output;
}

}  // namespace oxq::convert::detail
