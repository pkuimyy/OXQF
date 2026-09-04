#pragma once

#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <vector>

namespace oxq::convert::detail {

// Encodes a complete CCB v3 Record. The caller must first accept the result
// of preflight_cbl_write for the same GameModel and options.
[[nodiscard]] std::vector<std::byte> encode_cbl_record(
    const core::GameModel& game);

}  // namespace oxq::convert::detail
