#pragma once

#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <vector>

namespace oxq::convert::detail {

inline constexpr std::size_t cbl_record_prefix_size = 0x8aa;

// Encodes the fixed CCB v3 Record prefix through the Root Header. The caller
// must run preflight_cbl_write first; comments and move nodes are appended by
// the tree writer in a later stage.
[[nodiscard]] std::vector<std::byte> encode_cbl_record_prefix(
    const core::GameModel& game);

}  // namespace oxq::convert::detail
