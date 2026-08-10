#pragma once

#include "codec/container.hpp"
#include "codec/string_pool.hpp"

#include <oxq/core/codec_error.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace oxq::core::detail {

enum class MetadataValueType : std::uint8_t {
  u32 = 1,
  i32 = 2,
  u64 = 3,
  i64 = 4,
  string_ref = 5,
  bytes = 6,
  boolean = 7,
};

struct MetadataLimits {
  std::size_t max_fields{65'536};
};

struct MetadataFieldView {
  std::uint16_t tag{};
  std::uint8_t value_type{};
  std::uint8_t flags{};
  std::span<const std::byte> raw_value;
  std::optional<std::string_view> string_value;
  bool standard{};
};

struct MetadataView {
  std::vector<MetadataFieldView> fields;
  bool canonical_order{true};
  std::size_t unknown_field_count{};
  std::size_t unknown_value_type_count{};
};

using MetadataResult = std::variant<MetadataView, CodecError>;

// Returned spans and string_views borrow from input and strings.
[[nodiscard]] MetadataResult read_metadata(
    std::span<const std::byte> input,
    const ContainerView& container,
    const StringPoolView& strings,
    const MetadataLimits& limits = {});

}  // namespace oxq::core::detail
