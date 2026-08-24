#pragma once

#include <oxq/core/game_model.hpp>
#include <oxq/core/validation.hpp>

#include <vector>

namespace oxq::core {

// Performs model validation first. Position replay starts only when the model
// structure is valid, so malformed trees never drive state traversal.
[[nodiscard]] std::vector<ValidationIssue> validate_state(const GameModel& game);

}  // namespace oxq::core
