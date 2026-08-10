#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace oxq::core::detail {

[[nodiscard]] std::uint32_t crc32c(std::span<const std::byte> input) noexcept;

}  // namespace oxq::core::detail
