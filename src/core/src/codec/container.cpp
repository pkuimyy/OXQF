#include "codec/container.hpp"

#include "codec/binary.hpp"
#include "codec/crc32c.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace oxq::core::detail {
namespace {

constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kSectionEntrySize = 40;
constexpr std::array<std::uint8_t, 8> kMagic{0x89, 0x4f, 0x58, 0x51,
                                             0x0d, 0x0a, 0x1a, 0x0a};

[[nodiscard]] CodecError error(CodecErrorCode code, std::size_t offset, std::string field,
                               std::string message, std::optional<std::uint64_t> expected = {},
                               std::optional<std::uint64_t> actual = {},
                               std::optional<std::uint32_t> section = {}) {
  return {code, offset, section, std::move(field), std::move(message), expected, actual};
}

[[nodiscard]] bool to_size(std::uint64_t value, std::size_t& result) noexcept {
  if (value > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  result = static_cast<std::size_t>(value);
  return true;
}

[[nodiscard]] bool overlaps(std::size_t left_offset, std::size_t left_size,
                            std::size_t right_offset, std::size_t right_size) noexcept {
  return left_offset < right_offset + right_size && right_offset < left_offset + left_size;
}

[[nodiscard]] bool all_zero(std::span<const std::byte> input, std::size_t begin,
                            std::size_t end) noexcept {
  return std::ranges::all_of(input.subspan(begin, end - begin),
                             [](std::byte value) { return value == std::byte{0}; });
}

}  // namespace

ContainerResult inspect_container(std::span<const std::byte> input, const ContainerLimits& limits) {
  if (input.size() > limits.max_file_size) {
    return error(CodecErrorCode::resource_limit, 0, "file_size", "file size limit exceeded",
                 limits.max_file_size, input.size());
  }
  if (input.size() < kHeaderSize) {
    return error(CodecErrorCode::invalid_header, input.size(), "header",
                 "file is shorter than the 64-byte header", kHeaderSize, input.size());
  }
  for (std::size_t index = 0; index < kMagic.size(); ++index) {
    if (std::to_integer<std::uint8_t>(input[index]) != kMagic[index]) {
      return error(CodecErrorCode::invalid_magic, index, "magic", "OXQ Magic byte mismatch",
                   kMagic[index], std::to_integer<std::uint8_t>(input[index]));
    }
  }

  const auto major = read_u16(input, 0x08);
  const auto minor = read_u16(input, 0x0a);
  const auto header_size = read_u32(input, 0x0c);
  if (major != 1 || minor > 0 || header_size != kHeaderSize) {
    return error(CodecErrorCode::unsupported_version, 0x08, "version",
                 "only the frozen v1.0 64-byte header is supported");
  }
  const auto stored_header_crc = read_u32(input, 0x3c);
  const auto actual_header_crc = crc32c(input.first(0x3c));
  if (stored_header_crc != actual_header_crc) {
    return error(CodecErrorCode::crc_mismatch, 0x3c, "header_crc32c", "Header CRC-32C mismatch",
                 stored_header_crc, actual_header_crc);
  }

  std::size_t declared_file_size = 0;
  if (!to_size(read_u64(input, 0x10), declared_file_size)) {
    return error(CodecErrorCode::integer_overflow, 0x10, "file_size",
                 "file_size does not fit the host size type");
  }
  if (declared_file_size != input.size()) {
    return error(CodecErrorCode::size_mismatch, 0x10, "file_size",
                 "declared and actual file sizes differ", declared_file_size, input.size());
  }

  std::size_t table_offset = 0;
  if (!to_size(read_u64(input, 0x18), table_offset)) {
    return error(CodecErrorCode::integer_overflow, 0x18, "section_table_offset",
                 "section table offset does not fit the host size type");
  }
  const auto section_count = static_cast<std::size_t>(read_u32(input, 0x20));
  if (section_count < 5) {
    return error(CodecErrorCode::invalid_section_table, 0x20, "section_count",
                 "five standard Sections are required", 5, section_count);
  }
  if (section_count > limits.max_sections) {
    return error(CodecErrorCode::resource_limit, 0x20, "section_count",
                 "Section count limit exceeded", limits.max_sections, section_count);
  }
  if (read_u32(input, 0x24) != 0) {
    return error(CodecErrorCode::invalid_header, 0x24, "file_flags",
                 "v1.0 file_flags must be zero");
  }

  bool uuid_nonzero = false;
  ContainerView result;
  for (std::size_t index = 0; index < result.uuid.size(); ++index) {
    result.uuid[index] = std::to_integer<std::uint8_t>(input[0x28 + index]);
    uuid_nonzero = uuid_nonzero || result.uuid[index] != 0;
  }
  if (!uuid_nonzero || (result.uuid[8] & 0xc0U) != 0x80U) {
    return error(CodecErrorCode::invalid_header, 0x28, "game_uuid",
                 "game UUID must be non-nil and use the RFC 9562 variant");
  }

  std::size_t table_size = 0;
  std::size_t table_end = 0;
  if (!checked_multiply(section_count, kSectionEntrySize, table_size) ||
      !checked_add(table_offset, table_size, table_end)) {
    return error(CodecErrorCode::integer_overflow, 0x18, "section_table",
                 "Section Table range overflows");
  }
  if (table_offset < header_size || table_end > input.size()) {
    return error(CodecErrorCode::invalid_section_table, 0x18, "section_table",
                 "Section Table is outside the file or overlaps the Header");
  }
  const auto stored_table_crc = read_u32(input, 0x38);
  const auto actual_table_crc = crc32c(input.subspan(table_offset, table_size));
  if (stored_table_crc != actual_table_crc) {
    return error(CodecErrorCode::crc_mismatch, 0x38, "section_table_crc32c",
                 "Section Table CRC-32C mismatch", stored_table_crc, actual_table_crc);
  }

  result.sections.reserve(section_count);
  std::array<bool, 6> standard_seen{};
  for (std::size_t index = 0; index < section_count; ++index) {
    const std::size_t entry = table_offset + index * kSectionEntrySize;
    const auto type = read_u32(input, entry);
    const auto flags = read_u32(input, entry + 4);
    if (type == 0 || std::ranges::any_of(result.sections,
                                         [type](const SectionView& item) { return item.type == type; })) {
      return error(CodecErrorCode::invalid_section_table, entry, "section_type",
                   "Section type is zero or duplicated", {}, {}, type);
    }
    const bool standard = type >= 1 && type <= 5;
    if (!standard) {
      ++result.unknown_section_count;
    }
    if ((flags & ~3U) != 0 || (standard && flags != 1U)) {
      return error(CodecErrorCode::invalid_section_table, entry + 4, "section_flags",
                   "Section flags are invalid for v1.0", {}, flags, type);
    }
    if ((flags & 2U) != 0) {
      return error(CodecErrorCode::unsupported_version, entry + 4, "section_flags",
                   "compressed Sections are not supported in v1.0", {}, flags, type);
    }
    if (!standard && (flags & 1U) != 0) {
      return error(CodecErrorCode::unknown_critical_section, entry, "section_type",
                   "unknown critical Section cannot be skipped", {}, type, type);
    }

    std::size_t offset = 0;
    std::size_t stored_size = 0;
    std::size_t logical_size = 0;
    if (!to_size(read_u64(input, entry + 8), offset) ||
        !to_size(read_u64(input, entry + 16), stored_size) ||
        !to_size(read_u64(input, entry + 24), logical_size)) {
      return error(CodecErrorCode::integer_overflow, entry + 8, "section_range",
                   "Section range does not fit the host size type", {}, {}, type);
    }
    if (!result.sections.empty() &&
        (type < result.sections.back().type || offset < result.sections.back().offset)) {
      result.canonical_order = false;
    }
    if (stored_size != logical_size) {
      return error(CodecErrorCode::invalid_section_table, entry + 24, "logical_size",
                   "stored_size and logical_size must match in v1.0", stored_size, logical_size,
                   type);
    }
    std::size_t end = 0;
    if (!checked_add(offset, stored_size, end)) {
      return error(CodecErrorCode::integer_overflow, entry + 8, "section_range",
                   "Section range overflows", {}, {}, type);
    }
    if (offset % 8 != 0 || end > input.size()) {
      return error(CodecErrorCode::section_out_of_range, entry + 8, "section_range",
                   "Section is misaligned or outside the file", {}, {}, type);
    }
    if (overlaps(offset, stored_size, 0, header_size) ||
        overlaps(offset, stored_size, table_offset, table_size)) {
      return error(CodecErrorCode::section_overlap, entry + 8, "section_range",
                   "Section overlaps the Header or Section Table", {}, {}, type);
    }
    for (const auto& previous : result.sections) {
      if (overlaps(offset, stored_size, previous.offset, previous.size)) {
        return error(CodecErrorCode::section_overlap, entry + 8, "section_range",
                     "Section Payloads overlap", {}, {}, type);
      }
    }
    if (read_u32(input, entry + 36) != 0) {
      return error(CodecErrorCode::invalid_section_table, entry + 36, "reserved",
                   "Section Entry reserved field must be zero", {}, {}, type);
    }
    const auto stored_crc = read_u32(input, entry + 32);
    const auto actual_crc = crc32c(input.subspan(offset, stored_size));
    if (stored_crc != actual_crc) {
      return error(CodecErrorCode::crc_mismatch, entry + 32, "payload_crc32c",
                   "Section Payload CRC-32C mismatch", stored_crc, actual_crc, type);
    }
    result.sections.push_back({type, flags, offset, stored_size, stored_crc});
    if (standard) {
      standard_seen[type] = true;
    }
  }
  for (std::size_t type = 1; type <= 5; ++type) {
    if (!standard_seen[type]) {
      return error(CodecErrorCode::invalid_section_table, table_offset, "section_type",
                   "a required standard Section is missing", type);
    }
  }

  struct Interval {
    std::size_t begin;
    std::size_t end;
  };
  std::vector<Interval> intervals{{0, header_size}, {table_offset, table_end}};
  intervals.reserve(result.sections.size() + 2);
  for (const auto& section : result.sections) {
    intervals.push_back({section.offset, section.offset + section.size});
  }
  std::ranges::sort(intervals, {}, &Interval::begin);
  std::size_t cursor = 0;
  for (const auto& interval : intervals) {
    if (cursor < interval.begin && !all_zero(input, cursor, interval.begin)) {
      return error(CodecErrorCode::invalid_section_table, cursor, "padding",
                   "bytes outside registered regions must be zero");
    }
    cursor = std::max(cursor, interval.end);
  }
  if (cursor < input.size() && !all_zero(input, cursor, input.size())) {
    return error(CodecErrorCode::invalid_section_table, cursor, "padding",
                 "bytes outside registered regions must be zero");
  }

  return result;
}

}  // namespace oxq::core::detail
