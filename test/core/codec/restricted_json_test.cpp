#include "codec/restricted_json.hpp"

#include <oxq/core/game_model.hpp>

#include <string>
#include <variant>
#include <vector>

int main() {
  using oxq::core::detail::RestrictedJsonDocument;
  using oxq::core::detail::RestrictedJsonError;

  const std::string canonical =
      R"({"com.example.application":{"collection":["研究","待复核"]},"org.openxiangqi.cbl":{"display_index":"12","root_marker":"ffffffff"}})";
  const auto parsed = oxq::core::detail::parse_restricted_metadata_json(canonical);
  if (!std::holds_alternative<RestrictedJsonDocument>(parsed)) {
    return 1;
  }
  const auto& document = std::get<RestrictedJsonDocument>(parsed);
  if (!document.canonical || document.value.size() != 2 ||
      std::get<std::string>(document.value.at("org.openxiangqi.cbl").at("display_index")) !=
          "12" ||
      std::get<std::vector<std::string>>(
          document.value.at("com.example.application").at("collection")) !=
          std::vector<std::string>{"研究", "待复核"} ||
      oxq::core::detail::serialize_restricted_metadata_json(document.value) != canonical) {
    return 2;
  }

  const std::string noncanonical =
      R"( { "org.openxiangqi.cbl" : { "note" : "\u4e2d\u6587\/" } } )";
  const auto normalized = oxq::core::detail::parse_restricted_metadata_json(noncanonical);
  if (!std::holds_alternative<RestrictedJsonDocument>(normalized) ||
      std::get<RestrictedJsonDocument>(normalized).canonical ||
      oxq::core::detail::serialize_restricted_metadata_json(
          std::get<RestrictedJsonDocument>(normalized).value) !=
          R"({"org.openxiangqi.cbl":{"note":"中文/"}})") {
    return 3;
  }

  const auto emoji = oxq::core::detail::parse_restricted_metadata_json(
      R"({"org.example":{"emoji":"\ud83d\ude00"}})");
  if (!std::holds_alternative<RestrictedJsonDocument>(emoji) ||
      std::get<std::string>(
          std::get<RestrictedJsonDocument>(emoji).value.at("org.example").at("emoji")) !=
          "😀") {
    return 4;
  }

  const std::vector<std::string> invalid{
      R"({})",
      R"({"Invalid":{"key":"value"}})",
      R"({"org.example":{}})",
      R"({"org.example":{"items":[]}})",
      R"({"org.example":{"value":1}})",
      R"({"org.example":{"key":"a","key":"b"}})",
      R"({"org.example":{"bad-key":"x"}})",
      R"({"org.example":{"value":"\ud800"}})",
  };
  for (const auto& input : invalid) {
    if (!std::holds_alternative<RestrictedJsonError>(
            oxq::core::detail::parse_restricted_metadata_json(input))) {
      return 5;
    }
  }

  if (!std::holds_alternative<RestrictedJsonError>(
          oxq::core::detail::parse_restricted_metadata_json(canonical, 4))) {
    return 6;
  }
  std::string invalid_utf8 = R"({"org.example":{"value":")";
  invalid_utf8.append("\xc0\x80", 2);
  invalid_utf8 += R"("}})";
  if (!std::holds_alternative<RestrictedJsonError>(
          oxq::core::detail::parse_restricted_metadata_json(invalid_utf8))) {
    return 7;
  }
  return 0;
}
