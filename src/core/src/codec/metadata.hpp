#pragma once

#include "codec/container.hpp"
#include "codec/string_pool.hpp"

#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>

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
  std::size_t value_offset{};
  std::optional<std::size_t> string_data_offset;
  bool standard{};
};

struct MetadataView {
  std::vector<MetadataFieldView> fields;
  bool canonical_order{true};
  std::size_t unknown_field_count{};
  std::size_t unknown_value_type_count{};
};

using MetadataResult = std::variant<MetadataView, CodecError>;

struct DecodedMetadata {
  GameMetadata value;
  bool canonical_extensions{true};
};

using DecodedMetadataResult = std::variant<DecodedMetadata, CodecError>;

// Returned spans and string_views borrow from input and strings.
[[nodiscard]] MetadataResult read_metadata(
    std::span<const std::byte> input,
    const ContainerView& container,
    const StringPoolView& strings,
    const MetadataLimits& limits = {});

[[nodiscard]] DecodedMetadataResult decode_metadata(
    const MetadataView& metadata,
    std::size_t max_extended_metadata_bytes = 1024U * 1024U);

}  // namespace oxq::core::detail
