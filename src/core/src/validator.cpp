#include <oxq/core/validator.hpp>

#include <oxq/core/reader.hpp>

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

}  // namespace oxq::core
