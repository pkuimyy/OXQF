#pragma once

#include <oxq/convert/conversion_report.hpp>
#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace oxq::convert {

enum class CblErrorCode : std::uint8_t {
  invalid_magic,
  unsupported_version,
  truncated_input,
  integer_overflow,
  resource_limit,
  invalid_utf16,
  invalid_directory,
  resource_out_of_range,
  invalid_record,
  invalid_comment,
  invalid_move_tree,
  invalid_game_model,
};

struct CblError {
  CblErrorCode code{CblErrorCode::truncated_input};
  std::size_t offset{};
  std::optional<std::size_t> physical_slot;
  std::string field;
  std::string message;
  std::optional<std::uint64_t> expected;
  std::optional<std::uint64_t> actual;
};

struct CblReaderLimits {
  std::size_t max_file_size{2U * 1024U * 1024U * 1024U};
  std::size_t max_directory_entries{1'000'000};
  std::size_t max_total_blocks{1'000'000};
  std::size_t max_resource_bytes{64U * 1024U * 1024U};
  std::size_t max_nodes{10'000'000};
  std::size_t max_tree_depth{1'000'000};
  std::size_t max_comment_bytes{16U * 1024U * 1024U};
};

struct CblLibraryInfo {
  core::Uuid uuid;
  std::string name;
  std::string author;
  std::string author_email;
  std::string created_at;
  std::string modified_at;
  std::size_t directory_capacity{};
  std::size_t allocated_resource_count{};
  std::size_t live_game_count{};
  std::size_t deleted_game_count{};
  std::size_t live_non_game_count{};
  std::size_t total_blocks{};
  std::size_t trailing_bytes{};
};

struct CblReadOptions {
  ConversionMode mode{ConversionMode::lenient};
  CblReaderLimits limits;
};

struct CblReadResult {
  CblLibraryInfo library;
  std::vector<core::GameModel> games;
  ConversionReport report;
};

using CblReadOutcome = std::variant<CblReadResult, CblError>;

using CblInspectOutcome = std::variant<CblLibraryInfo, CblError>;

// Inspects the v3 container, directory, and physical resource ranges without
// decoding game Record semantics. The input bytes are never modified.
[[nodiscard]] CblInspectOutcome inspect_cbl(
    std::span<const std::byte> input,
    const CblReaderLimits& limits = {});

// Converts every live CCB Record to an independent GameModel, ordered by the
// directory display index. Structural failures return CblError. In strict
// mode, semantic loss returns a result with report.rejected set and no games.
[[nodiscard]] CblReadOutcome read_cbl(
    std::span<const std::byte> input,
    const CblReadOptions& options = {});

}  // namespace oxq::convert
