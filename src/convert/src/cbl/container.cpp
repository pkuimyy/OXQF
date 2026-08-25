#include "cbl/container.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace oxq::convert::detail {
namespace {

constexpr std::size_t kLibraryHeaderSize = 0x440;
constexpr std::size_t kDirectoryOffset = 0x10440;
constexpr std::size_t kDirectoryEntrySize = 0x114;
constexpr std::size_t kBlockSize = 0x1000;
constexpr std::array<std::uint8_t, 16> kLibraryMagic{
    0x43, 0x43, 0x42, 0x72, 0x69, 0x64, 0x67, 0x65,
    0x4c, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x00};
constexpr std::array<std::uint8_t, 16> kRecordMagic{
    0x43, 0x43, 0x42, 0x72, 0x69, 0x64, 0x67, 0x65,
    0x20, 0x52, 0x65, 0x63, 0x6f, 0x72, 0x64, 0x00};

[[nodiscard]] CblError error(CblErrorCode code, std::size_t offset, std::string field,
                             std::string message,
                             std::optional<std::size_t> physical_slot = {},
                             std::optional<std::uint64_t> expected = {},
                             std::optional<std::uint64_t> actual = {}) {
  return {code, offset, physical_slot, std::move(field), std::move(message), expected, actual};
}

[[nodiscard]] bool checked_add(std::size_t left, std::size_t right,
                               std::size_t& output) noexcept {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  output = left + right;
  return true;
}

[[nodiscard]] bool checked_multiply(std::size_t left, std::size_t right,
                                    std::size_t& output) noexcept {
  if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  output = left * right;
  return true;
}

[[nodiscard]] bool contains(std::span<const std::byte> input, std::size_t offset,
                            std::size_t size) noexcept {
  return offset <= input.size() && size <= input.size() - offset;
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

[[nodiscard]] StringOutcome decode_utf16_slot(std::span<const std::byte> input,
                                               std::size_t offset, std::size_t size,
                                               std::string field,
                                               std::optional<std::size_t> slot = {}) {
  if (!contains(input, offset, size) || size % 2 != 0) {
    return error(CblErrorCode::truncated_input, offset, std::move(field),
                 "UTF-16LE slot is outside the file", slot);
  }
  std::string result;
  result.reserve(size / 2);
  for (std::size_t cursor = 0; cursor < size; cursor += 2) {
    const auto first = read_u16(input, offset + cursor);
    if (first == 0) {
      return result;
    }
    std::uint32_t code_point = first;
    if (first >= 0xd800U && first <= 0xdbffU) {
      if (cursor + 4 > size) {
        return error(CblErrorCode::invalid_utf16, offset + cursor, std::move(field),
                     "high surrogate is truncated by the fixed slot", slot);
      }
      const auto second = read_u16(input, offset + cursor + 2);
      if (second < 0xdc00U || second > 0xdfffU) {
        return error(CblErrorCode::invalid_utf16, offset + cursor, std::move(field),
                     "high surrogate is not followed by a low surrogate", slot);
      }
      code_point = 0x10000U +
                   ((static_cast<std::uint32_t>(first) - 0xd800U) << 10U) +
                   (static_cast<std::uint32_t>(second) - 0xdc00U);
      cursor += 2;
    } else if (first >= 0xdc00U && first <= 0xdfffU) {
      return error(CblErrorCode::invalid_utf16, offset + cursor, std::move(field),
                   "unpaired low surrogate in fixed slot", slot);
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

[[nodiscard]] CblResourceKind classify(std::uint8_t flags) noexcept {
  if ((flags & 0x01U) == 0) {
    return CblResourceKind::empty;
  }
  switch (flags & 0x07U) {
    case 0x03:
      return CblResourceKind::live_non_game;
    case 0x05:
      return CblResourceKind::deleted_game;
    case 0x07:
      return CblResourceKind::live_game;
    default:
      return CblResourceKind::allocated_unknown;
  }
}

[[nodiscard]] bool matches_magic(std::span<const std::byte> input, std::size_t offset,
                                 const std::array<std::uint8_t, 16>& magic) noexcept {
  return contains(input, offset, magic.size()) &&
         std::ranges::equal(input.subspan(offset, magic.size()), magic,
                            {}, [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
}

}  // namespace

CblContainerOutcome inspect_cbl_container(std::span<const std::byte> input,
                                          const CblReaderLimits& limits) {
  if (input.size() > limits.max_file_size) {
    return error(CblErrorCode::resource_limit, 0, "file_size",
                 "CBL file size limit exceeded", {}, limits.max_file_size, input.size());
  }
  if (!contains(input, 0, kLibraryHeaderSize) || !contains(input, 0, kDirectoryOffset)) {
    return error(CblErrorCode::truncated_input, input.size(), "library_header",
                 "CBL is shorter than the fixed Library/Directory prefix",
                 {}, kDirectoryOffset, input.size());
  }
  for (std::size_t index = 0; index < kLibraryMagic.size(); ++index) {
    if (std::to_integer<std::uint8_t>(input[index]) != kLibraryMagic[index]) {
      return error(CblErrorCode::invalid_magic, index, "library_magic",
                   "CBL Library Magic differs", {}, kLibraryMagic[index],
                   std::to_integer<std::uint8_t>(input[index]));
    }
  }
  const auto version = read_u32(input, 0x10);
  if (version != 3) {
    return error(CblErrorCode::unsupported_version, 0x10, "library_version",
                 "only CBL Library version 3 is supported", {}, 3, version);
  }
  const auto capacity = static_cast<std::size_t>(read_u32(input, 0x3c));
  if (capacity > limits.max_directory_entries) {
    return error(CblErrorCode::resource_limit, 0x3c, "directory_capacity",
                 "CBL directory entry limit exceeded", {},
                 limits.max_directory_entries, capacity);
  }
  std::size_t directory_size = 0;
  std::size_t directory_end = 0;
  if (!checked_multiply(capacity, kDirectoryEntrySize, directory_size) ||
      !checked_add(kDirectoryOffset, directory_size, directory_end)) {
    return error(CblErrorCode::integer_overflow, 0x3c, "directory_capacity",
                 "CBL directory range overflowed");
  }
  if (!contains(input, kDirectoryOffset, directory_size)) {
    return error(CblErrorCode::truncated_input, input.size(), "directory",
                 "CBL directory is truncated", {}, directory_end, input.size());
  }

  CblContainerView result;
  result.input = input;
  result.library.uuid = decode_windows_guid(input, 0x14);
  result.library.directory_capacity = capacity;
  struct HeaderString {
    std::size_t offset;
    std::size_t size;
    const char* field;
    std::string CblLibraryInfo::*destination;
  };
  constexpr std::array<HeaderString, 5> strings{{
      {0x040, 0x300, "library_name", &CblLibraryInfo::name},
      {0x340, 0x040, "library_author", &CblLibraryInfo::author},
      {0x380, 0x040, "library_author_email", &CblLibraryInfo::author_email},
      {0x3c0, 0x040, "library_created_at", &CblLibraryInfo::created_at},
      {0x400, 0x040, "library_modified_at", &CblLibraryInfo::modified_at},
  }};
  for (const auto& string : strings) {
    auto decoded = decode_utf16_slot(input, string.offset, string.size, string.field);
    if (std::holds_alternative<CblError>(decoded)) {
      return std::get<CblError>(std::move(decoded));
    }
    result.library.*(string.destination) = std::get<std::string>(std::move(decoded));
  }

  result.entries.reserve(capacity);
  std::size_t blocks = 0;
  for (std::size_t slot = 0; slot < capacity; ++slot) {
    const auto entry_offset = kDirectoryOffset + slot * kDirectoryEntrySize;
    CblDirectoryEntryView entry;
    entry.physical_slot = slot;
    entry.resource_flags = std::to_integer<std::uint8_t>(input[entry_offset]);
    entry.display_index = read_u32(input, entry_offset + 4);
    entry.block_count = read_u32(input, entry_offset + 8);
    entry.used_size = read_u32(input, entry_offset + 12);
    entry.kind = classify(entry.resource_flags);
    std::size_t preceding_block_bytes = 0;
    if (!checked_multiply(static_cast<std::size_t>(entry.block_count), kBlockSize,
                          entry.allocated_size) ||
        !checked_multiply(blocks, kBlockSize, preceding_block_bytes) ||
        !checked_add(directory_end, preceding_block_bytes, entry.resource_offset)) {
      return error(CblErrorCode::integer_overflow, entry_offset + 8, "block_count",
                   "CBL resource range overflowed", slot);
    }
    if (entry.used_size > entry.allocated_size) {
      return error(CblErrorCode::invalid_directory, entry_offset + 12, "used_size",
                   "resource used_size exceeds allocated blocks", slot,
                   entry.allocated_size, entry.used_size);
    }
    if (entry.used_size > limits.max_resource_bytes) {
      return error(CblErrorCode::resource_limit, entry_offset + 12, "used_size",
                   "CBL resource size limit exceeded", slot,
                   limits.max_resource_bytes, entry.used_size);
    }
    if (entry.block_count > limits.max_total_blocks - blocks) {
      return error(CblErrorCode::resource_limit, entry_offset + 8, "total_blocks",
                   "CBL total block limit exceeded", slot,
                   limits.max_total_blocks, blocks + entry.block_count);
    }
    blocks += entry.block_count;
    if (!contains(input, entry.resource_offset, entry.allocated_size)) {
      return error(CblErrorCode::resource_out_of_range, entry_offset + 8, "allocated_size",
                   "CBL resource blocks extend beyond the file", slot,
                   entry.resource_offset + entry.allocated_size, input.size());
    }
    if (entry.kind != CblResourceKind::empty) {
      auto uuid = decode_utf16_slot(input, entry_offset + 0x14, 80,
                                    "directory_uuid", slot);
      if (std::holds_alternative<CblError>(uuid)) {
        return std::get<CblError>(std::move(uuid));
      }
      entry.uuid_text = std::get<std::string>(std::move(uuid));
      auto title = decode_utf16_slot(input, entry_offset + 0x64, 176,
                                     "directory_title", slot);
      if (std::holds_alternative<CblError>(title)) {
        return std::get<CblError>(std::move(title));
      }
      entry.title = std::get<std::string>(std::move(title));
      ++result.library.allocated_resource_count;
    }
    switch (entry.kind) {
      case CblResourceKind::live_game:
        if (entry.used_size < kRecordMagic.size() ||
            !matches_magic(input, entry.resource_offset, kRecordMagic)) {
          return error(CblErrorCode::invalid_record, entry.resource_offset, "record_magic",
                       "live CCB resource does not start with Record Magic", slot);
        }
        ++result.library.live_game_count;
        break;
      case CblResourceKind::deleted_game:
        ++result.library.deleted_game_count;
        break;
      case CblResourceKind::live_non_game:
        ++result.library.live_non_game_count;
        break;
      case CblResourceKind::empty:
      case CblResourceKind::allocated_unknown:
        break;
    }
    result.entries.push_back(std::move(entry));
  }
  result.library.total_blocks = blocks;
  std::size_t expected_size = 0;
  std::size_t block_bytes = 0;
  if (!checked_multiply(blocks, kBlockSize, block_bytes) ||
      !checked_add(directory_end, block_bytes, expected_size)) {
    return error(CblErrorCode::integer_overflow, 0x3c, "file_closure",
                 "CBL expected file size overflowed");
  }
  if (input.size() < expected_size) {
    return error(CblErrorCode::resource_out_of_range, input.size(), "file_closure",
                 "CBL file is shorter than its allocated block pool", {},
                 expected_size, input.size());
  }
  result.library.trailing_bytes = input.size() - expected_size;
  return result;
}

}  // namespace oxq::convert::detail
