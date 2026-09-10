#pragma once

#include <oxq/format/document.hpp>
#include <oxq/format/validation.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oxq::editor {

enum class EditorErrorCode {
  invalid_document,
  invalid_argument,
  node_not_found,
  duplicate_move,
  revision_conflict,
  validation_failed,
  history_empty,
  resource_limit,
  internal_invariant,
  invalid_position,
  move_number_overflow,
};

struct EditorError {
  EditorErrorCode code{};
  std::string message;
  std::vector<format::ValidationIssue> validation_issues;
  std::optional<format::NodeId> node_id;
  std::optional<std::uint64_t> expected_revision;
  std::optional<std::uint64_t> actual_revision;

  friend bool operator==(const EditorError&, const EditorError&) = default;
};

using Status = std::optional<EditorError>;

}  // namespace oxq::editor
