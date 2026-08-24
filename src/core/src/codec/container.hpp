#pragma once

#include <oxq/core/codec_error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace oxq::core::detail {

struct ContainerLimits {
  std::size_t max_file_size{1024U * 1024U * 1024U};
  std::size_t max_sections{1024};
};

struct SectionView {
  std::uint32_t type{};
  std::uint32_t flags{};
  std::size_t offset{};
  std::size_t size{};
  std::uint32_t crc32c{};
};

struct ContainerView {
  std::array<std::uint8_t, 16> uuid{};
  std::vector<SectionView> sections;
  bool canonical_order{true};
  std::size_t unknown_section_count{};
};

using ContainerResult = std::variant<ContainerView, CodecError>;

[[nodiscard]] ContainerResult inspect_container(
    std::span<const std::byte> input,
    const ContainerLimits& limits = {});

}  // namespace oxq::core::detail
