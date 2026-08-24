#pragma once

#include <oxq/core/codec_error.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/validation.hpp>

#include <cstddef>
#include <span>
#include <variant>
#include <vector>

namespace oxq::core {

using ValidatorOutcome = std::variant<ReaderDiagnostics, CodecError>;

struct StateValidatorResult {
  ReaderDiagnostics diagnostics;
  std::vector<ValidationIssue> issues;
};

using StateValidatorOutcome = std::variant<StateValidatorResult, CodecError>;

[[nodiscard]] ValidatorOutcome validate_oxq(
    std::span<const std::byte> input,
    const ReaderLimits& limits = {});

[[nodiscard]] StateValidatorOutcome validate_oxq_state(
    std::span<const std::byte> input,
    const ReaderLimits& limits = {});

}  // namespace oxq::core
