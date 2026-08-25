#pragma once

#include <oxq/convert/cbl_reader.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace oxq::convert::detail {

enum class CblResourceKind : std::uint8_t {
  empty,
  live_non_game,
  deleted_game,
  live_game,
  allocated_unknown,
};

struct CblDirectoryEntryView {
  std::size_t physical_slot{};
  std::uint8_t resource_flags{};
  std::uint32_t display_index{};
  std::uint32_t block_count{};
  std::uint32_t used_size{};
  std::size_t resource_offset{};
  std::size_t allocated_size{};
  CblResourceKind kind{CblResourceKind::empty};
  std::string uuid_text;
  std::string title;
};

struct CblContainerView {
  CblLibraryInfo library;
  std::vector<CblDirectoryEntryView> entries;
  std::span<const std::byte> input;
};

using CblContainerOutcome = std::variant<CblContainerView, CblError>;

[[nodiscard]] CblContainerOutcome inspect_cbl_container(
    std::span<const std::byte> input,
    const CblReaderLimits& limits = {});

}  // namespace oxq::convert::detail
