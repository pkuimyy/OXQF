#pragma once

#include <oxq/core/codec_error.hpp>
#include <oxq/core/reader.hpp>

#include <cstddef>
#include <span>
#include <variant>

namespace oxq::core {

using ValidatorOutcome = std::variant<ReaderDiagnostics, CodecError>;

[[nodiscard]] ValidatorOutcome validate_oxq(
    std::span<const std::byte> input,
    const ReaderLimits& limits = {});

}  // namespace oxq::core
