#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oxq::convert {

enum class ConversionMode : std::uint8_t {
  lenient,
  strict,
};

enum class ConversionSeverity : std::uint8_t {
  warning,
  loss,
};

enum class ConversionCode : std::uint8_t {
  cbl_uuid_mismatch,
  cbl_directory_uuid_invalid,
  cbl_record_uuid_invalid,
  cbl_uuid_derived,
  cbl_title_mismatch,
  cbl_duplicate_display_index,
  cbl_invalid_rating,
  cbl_invalid_date,
  cbl_fullmove_normalized,
  cbl_multiple_result,
  cbl_unknown_result,
  cbl_unknown_record_type,
};

struct ConversionDiagnostic {
  ConversionSeverity severity{ConversionSeverity::warning};
  ConversionCode code{};
  std::optional<std::size_t> game_index;
  std::optional<std::size_t> physical_slot;
  std::optional<std::size_t> node_index;
  std::string field;
  std::string message;
};

struct ConversionReport {
  std::size_t source_game_count{};
  std::size_t converted_game_count{};
  bool rejected{};
  std::vector<ConversionDiagnostic> diagnostics;

  [[nodiscard]] bool has_warnings() const noexcept;
  [[nodiscard]] bool has_loss() const noexcept;
};

}  // namespace oxq::convert
