#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace oxq::core {

enum class CodecErrorCode {
  invalid_magic,
  unsupported_version,
  invalid_header,
  size_mismatch,
  integer_overflow,
  crc_mismatch,
  invalid_section_table,
  section_out_of_range,
  section_overlap,
  unknown_critical_section,
  invalid_utf8,
  invalid_string_ref,
  invalid_metadata,
  invalid_position,
  invalid_move,
  invalid_tree,
  invalid_annotation,
  resource_limit,
};

struct CodecError {
  CodecErrorCode code{};
  std::size_t offset{};
  std::optional<std::uint32_t> section_type;
  std::string field;
  std::string message;
  std::optional<std::uint64_t> expected;
  std::optional<std::uint64_t> actual;

  friend bool operator==(const CodecError&, const CodecError&) = default;
};

}  // namespace oxq::core
