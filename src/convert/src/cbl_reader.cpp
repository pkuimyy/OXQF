#include <oxq/convert/cbl_reader.hpp>

#include "cbl/container.hpp"

#include <utility>
#include <variant>

namespace oxq::convert {

CblInspectOutcome inspect_cbl(std::span<const std::byte> input,
                              const CblReaderLimits& limits) {
  auto result = detail::inspect_cbl_container(input, limits);
  if (std::holds_alternative<CblError>(result)) {
    return std::get<CblError>(std::move(result));
  }
  return std::get<detail::CblContainerView>(std::move(result)).library;
}

}  // namespace oxq::convert
