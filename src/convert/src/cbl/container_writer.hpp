#pragma once

#include <oxq/convert/cbl_writer.hpp>

#include <cstddef>
#include <span>
#include <variant>
#include <vector>

namespace oxq::convert::detail {

// Encodes a complete CBL v3 Library after preflight has accepted the same
// ordered games and options.
using CblContainerWriteOutcome =
    std::variant<std::vector<std::byte>, CblWriteError>;

[[nodiscard]] CblContainerWriteOutcome encode_cbl_container(
    std::span<const core::GameModel> games,
    const CblWriteOptions& options,
    const CblWritePlan& plan);

}  // namespace oxq::convert::detail
