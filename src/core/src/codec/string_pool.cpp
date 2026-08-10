#include "codec/string_pool.hpp"

#include "codec/binary.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace oxq::core::detail {
namespace {

constexpr std::uint32_t kStringPoolType = 5;

[[nodiscard]] CodecError error(CodecErrorCode code, std::size_t offset, std::string field,
                               std::string message, std::optional<std::uint64_t> expected = {},
                               std::optional<std::uint64_t> actual = {}) {
  return {code, offset, kStringPoolType, std::move(field), std::move(message), expected, actual};
}

[[nodiscard]] bool unsigned_less(std::string_view left, std::string_view right) noexcept {
  return std::lexicographical_compare(
      left.begin(), left.end(), right.begin(), right.end(),
      [](char lhs, char rhs) {
        return static_cast<unsigned char>(lhs) < static_cast<unsigned char>(rhs);
      });
}

}  // namespace

std::optional<std::string_view> StringPoolView::find(std::uint32_t reference) const noexcept {
  const auto found = std::ranges::lower_bound(records, reference, {}, &StringRecordView::reference);
  if (found == records.end() || found->reference != reference) {
    return std::nullopt;
  }
  return found->value;
}

StringPoolResult read_string_pool(std::span<const std::byte> input, const ContainerView& container,
                                  const StringPoolLimits& limits) {
  const auto section = std::ranges::find(container.sections, kStringPoolType, &SectionView::type);
  if (section == container.sections.end()) {
    return error(CodecErrorCode::invalid_section_table, 0, "section_type",
                 "STRING_POOL Section is missing");
  }
  if (section->size > limits.max_total_bytes) {
    return error(CodecErrorCode::resource_limit, section->offset, "string_pool_size",
                 "STRING_POOL byte limit exceeded", limits.max_total_bytes, section->size);
  }
  const auto payload = input.subspan(section->offset, section->size);
  if (payload.size() < 8) {
    return error(CodecErrorCode::invalid_string_ref, section->offset, "header",
                 "STRING_POOL is shorter than its 8-byte header", 8, payload.size());
  }
  const auto version = read_u16(payload, 0);
  const auto header_size = read_u16(payload, 2);
  if (version != 1 || header_size != 8) {
    return error(CodecErrorCode::unsupported_version, section->offset, "section_version",
                 "unsupported STRING_POOL header", 1, version);
  }
  const auto count = static_cast<std::size_t>(read_u32(payload, 4));
  if (count > limits.max_strings) {
    return error(CodecErrorCode::resource_limit, section->offset + 4, "string_count",
                 "string count limit exceeded", limits.max_strings, count);
  }
  if (count > (payload.size() - 8) / 4) {
    return error(CodecErrorCode::invalid_string_ref, section->offset + 4, "string_count",
                 "string_count cannot fit in the Section", {}, count);
  }

  StringPoolView result;
  result.records.reserve(count);
  std::size_t cursor = 8;
  std::optional<std::string_view> previous;
  for (std::size_t index = 0; index < count; ++index) {
    if (!contains(payload, cursor, 4)) {
      return error(CodecErrorCode::invalid_string_ref, section->offset + cursor, "byte_length",
                   "String Record length is truncated");
    }
    const auto reference = static_cast<std::uint32_t>(cursor);
    const auto length = static_cast<std::size_t>(read_u32(payload, cursor));
    if (length > limits.max_string_bytes) {
      return error(CodecErrorCode::resource_limit, section->offset + cursor, "byte_length",
                   "single string byte limit exceeded", limits.max_string_bytes, length);
    }
    std::size_t data_begin = 0;
    std::size_t data_end = 0;
    if (!checked_add(cursor, 4, data_begin) || !checked_add(data_begin, length, data_end) ||
        data_end > payload.size()) {
      return error(CodecErrorCode::invalid_string_ref, section->offset + cursor, "byte_length",
                   "String Record extends beyond STRING_POOL", {}, length);
    }
    const auto* characters = reinterpret_cast<const char*>(payload.data() + data_begin);
    const std::string_view value{characters, length};
    if (const auto invalid = first_invalid_utf8(value); invalid.has_value()) {
      return error(CodecErrorCode::invalid_utf8, section->offset + data_begin + *invalid,
                   "utf8_bytes", "String Record contains invalid UTF-8");
    }

    std::size_t record_end = 0;
    const std::size_t padding = (4 - data_end % 4) % 4;
    if (!checked_add(data_end, padding, record_end) || record_end > payload.size()) {
      return error(CodecErrorCode::invalid_string_ref, section->offset + data_end, "padding",
                   "String Record padding extends beyond STRING_POOL");
    }
    if (!std::ranges::all_of(payload.subspan(data_end, padding),
                             [](std::byte byte) { return byte == std::byte{0}; })) {
      return error(CodecErrorCode::invalid_string_ref, section->offset + data_end, "padding",
                   "String Record padding must be zero");
    }

    if (previous.has_value() && !unsigned_less(*previous, value)) {
      result.canonical_order = false;
    }
    previous = value;
    result.records.push_back({reference, value});
    cursor = record_end;
  }
  if (cursor != payload.size()) {
    return error(CodecErrorCode::invalid_string_ref, section->offset + cursor, "trailing_data",
                 "STRING_POOL contains bytes beyond string_count records", cursor, payload.size());
  }
  return result;
}

}  // namespace oxq::core::detail
