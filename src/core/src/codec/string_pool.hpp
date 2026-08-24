#pragma once

#include "codec/container.hpp"

#include <oxq/core/codec_error.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace oxq::core::detail {

struct StringPoolLimits {
  std::size_t max_strings{10'000'000};
  std::size_t max_string_bytes{16U * 1024U * 1024U};
  std::size_t max_total_bytes{512U * 1024U * 1024U};
};

struct StringRecordView {
  std::uint32_t reference{};
  std::string_view value;
  std::size_t data_offset{};
};

struct StringPoolView {
  std::vector<StringRecordView> records;
  bool canonical_order{true};

  [[nodiscard]] std::optional<std::string_view> find(std::uint32_t reference) const noexcept;
  [[nodiscard]] const StringRecordView* find_record(std::uint32_t reference) const noexcept;
};

using StringPoolResult = std::variant<StringPoolView, CodecError>;

// Returned string_views borrow from input. The input bytes must outlive StringPoolView.
[[nodiscard]] StringPoolResult read_string_pool(
    std::span<const std::byte> input,
    const ContainerView& container,
    const StringPoolLimits& limits = {});

}  // namespace oxq::core::detail
