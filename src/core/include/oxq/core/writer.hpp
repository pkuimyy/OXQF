#pragma once

#include <oxq/core/game_model.hpp>
#include <oxq/core/validation.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace oxq::core {

enum class WriterErrorCode {
  invalid_model,
  resource_limit,
  integer_overflow,
};

struct WriterError {
  WriterErrorCode code{};
  std::string message;
  std::vector<ValidationIssue> validation_issues;
  std::optional<std::uint64_t> expected;
  std::optional<std::uint64_t> actual;
};

struct WriterLimits {
  ValidationLimits model;
  std::size_t max_file_size{1024U * 1024U * 1024U};
  std::size_t max_strings{10'000'000};
  std::size_t max_total_string_bytes{512U * 1024U * 1024U};
  std::size_t max_metadata_fields{65'536};
  std::size_t max_extended_metadata_bytes{1024U * 1024U};
};

using WriterOutcome = std::variant<std::vector<std::byte>, WriterError>;

[[nodiscard]] WriterOutcome write_oxq(
    const GameModel& game,
    const WriterLimits& limits = {});

}  // namespace oxq::core
