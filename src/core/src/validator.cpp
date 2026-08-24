#include <oxq/core/validator.hpp>

#include <oxq/core/reader.hpp>
#include <oxq/core/state_validation.hpp>

#include <cstddef>
#include <span>
#include <utility>
#include <variant>

namespace oxq::core {

ValidatorOutcome validate_oxq(std::span<const std::byte> input, const ReaderLimits& limits) {
  auto result = read_oxq(input, limits);
  if (std::holds_alternative<CodecError>(result)) {
    return std::get<CodecError>(std::move(result));
  }
  return std::get<ReaderResult>(std::move(result)).diagnostics;
}

StateValidatorOutcome validate_oxq_state(std::span<const std::byte> input,
                                         const ReaderLimits& limits) {
  auto result = read_oxq(input, limits);
  if (std::holds_alternative<CodecError>(result)) {
    return std::get<CodecError>(std::move(result));
  }
  auto read = std::get<ReaderResult>(std::move(result));
  StateValidatorResult validated;
  validated.diagnostics = read.diagnostics;
  validated.issues = validate_state(read.game);
  return validated;
}

}  // namespace oxq::core
