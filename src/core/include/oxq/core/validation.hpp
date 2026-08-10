#pragma once

#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace oxq::core {

enum class ValidationSeverity {
  error,
  warning,
};

enum class ValidationCode {
  nil_uuid,
  invalid_uuid_variant,
  invalid_side,
  invalid_fullmove_number,
  too_many_pieces,
  invalid_piece_type,
  invalid_square,
  duplicate_square,
  empty_tree,
  too_many_nodes,
  invalid_root,
  invalid_parent,
  invalid_child,
  duplicate_child,
  unreachable_node,
  tree_cycle,
  tree_too_deep,
  invalid_move,
  invalid_annotation,
  too_many_annotations,
  string_too_long,
  invalid_utf8,
  invalid_extension_namespace,
  invalid_extension_key,
  empty_extension_namespace,
  empty_extension_array,
};

struct ValidationIssue {
  ValidationSeverity severity{ValidationSeverity::error};
  ValidationCode code{};
  std::string path;
  std::string message;

  friend bool operator==(const ValidationIssue&, const ValidationIssue&) = default;
};

struct ValidationLimits {
  std::size_t max_pieces{32};
  std::size_t max_nodes{10'000'000};
  std::size_t max_annotations{10'000'000};
  std::size_t max_tree_depth{1'000'000};
  std::size_t max_string_bytes{16U * 1024U * 1024U};
};

[[nodiscard]] std::vector<ValidationIssue> validate(
    const GameModel& game,
    const ValidationLimits& limits = {});

[[nodiscard]] bool has_errors(const std::vector<ValidationIssue>& issues) noexcept;

}  // namespace oxq::core
