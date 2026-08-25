#pragma once

#include "cbl/container.hpp"

#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace oxq::convert::detail {

struct CblRecordMetadata {
  std::string name;
  std::string url_or_category;
  std::string source;
  std::string contest_type;
  std::string contest;
  std::string round;
  std::string group;
  std::string table;
  std::string date;
  std::string site;
  std::string time_rule;
  std::string red;
  std::string red_team;
  std::string red_time;
  std::string red_rating;
  std::string black;
  std::string black_team;
  std::string black_time;
  std::string black_rating;
  std::string referee;
  std::string recorder;
  std::string commentator;
  std::string commentator_uri;
  std::string creator;
  std::string creator_uri;
  std::string created_at;
  std::string modified_at;
  std::string ecco_code;
  std::uint32_t record_type{};
  std::string record_kind;
  std::uint32_t result{};
  std::string result_text;
};

struct CblRecordView {
  core::Uuid guid;
  CblRecordMetadata metadata;
  core::Position position;
  std::uint32_t source_fullmove_number{};
  std::uint32_t root_marker{};
  std::uint16_t root_control{};
  std::size_t node_stream_offset{};
  std::size_t used_size{};
};

using CblRecordOutcome = std::variant<CblRecordView, CblError>;

[[nodiscard]] CblRecordOutcome read_cbl_record(
    const CblContainerView& container,
    const CblDirectoryEntryView& entry);

}  // namespace oxq::convert::detail
