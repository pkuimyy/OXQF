#pragma once

#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

namespace oxq::core::detail {

struct RestrictedJsonError {
  std::size_t offset{};
  std::string message;
};

struct RestrictedJsonDocument {
  ExtensionMetadata value;
  bool canonical{};
};

using RestrictedJsonResult = std::variant<RestrictedJsonDocument, RestrictedJsonError>;

[[nodiscard]] RestrictedJsonResult parse_restricted_metadata_json(
    std::string_view input,
    std::size_t max_bytes = 1024U * 1024U);

[[nodiscard]] std::string serialize_restricted_metadata_json(const ExtensionMetadata& value);

}  // namespace oxq::core::detail
