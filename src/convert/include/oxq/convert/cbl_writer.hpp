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

enum class CblWriteErrorCode : std::uint8_t {
  too_many_games,
  integer_overflow,
  resource_limit,
  encoding_invariant,
};

struct CblWriteError {
  CblWriteErrorCode code{CblWriteErrorCode::resource_limit};
  std::optional<std::size_t> game_index;
  std::optional<std::size_t> node_index;
  std::string field;
  std::string message;
  std::optional<std::uint64_t> expected;
  std::optional<std::uint64_t> actual;
};

struct CblWriteLimits {
  std::size_t max_games{1'000'000};
  std::size_t max_directory_entries{1'000'000};
  std::size_t max_record_bytes{64U * 1024U * 1024U};
  std::size_t max_comment_bytes{16U * 1024U * 1024U};
  std::size_t max_output_bytes{2U * 1024U * 1024U * 1024U};
};

struct CblWriteLibraryMetadata {
  std::optional<core::Uuid> uuid;
  std::string name;
  std::string author;
  std::string author_email;
  std::string created_at;
  std::string modified_at;
};

struct CblWriteOptions {
  ConversionMode mode{ConversionMode::lenient};
  CblWriteLibraryMetadata library;
  std::size_t minimum_directory_capacity{128};
  CblWriteLimits limits;
};

struct CblWritePlan {
  core::Uuid library_uuid;
  std::size_t directory_capacity{};
  std::size_t projected_file_size{};
  std::vector<std::size_t> record_sizes;
  ConversionReport report;
};

using CblWritePreflightOutcome = std::variant<CblWritePlan, CblWriteError>;

struct CblWriteResult {
  core::Uuid library_uuid;
  std::size_t directory_capacity{};
  std::vector<std::byte> bytes;
  ConversionReport report;
};

using CblWriteOutcome = std::variant<CblWriteResult, CblWriteError>;

// Performs all model, expressibility, fixed-slot, and size checks needed by
// the CBL v3 writer without allocating the projected output buffer.
[[nodiscard]] CblWritePreflightOutcome preflight_cbl_write(
    std::span<const core::GameModel> games,
    const CblWriteOptions& options = {});

// Encodes a complete deterministic CBL v3 Library. A rejected strict or
// structurally invalid conversion returns CblWriteResult with no bytes.
[[nodiscard]] CblWriteOutcome write_cbl(
    std::span<const core::GameModel> games,
    const CblWriteOptions& options = {});

}  // namespace oxq::convert
