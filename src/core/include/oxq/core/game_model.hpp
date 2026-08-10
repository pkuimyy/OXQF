#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oxq::core {

struct Uuid {
  std::array<std::uint8_t, 16> bytes{};

  [[nodiscard]] bool is_nil() const noexcept;
  [[nodiscard]] std::string to_string() const;
  [[nodiscard]] static std::optional<Uuid> parse(std::string_view text) noexcept;

  friend bool operator==(const Uuid&, const Uuid&) = default;
};

enum class Side : std::uint8_t {
  red,
  black,
};

enum class PieceType : std::uint8_t {
  king = 1,
  advisor,
  elephant,
  horse,
  rook,
  cannon,
  pawn,
};

struct Piece {
  Side side{Side::red};
  PieceType type{PieceType::pawn};
  std::uint8_t square{};

  friend bool operator==(const Piece&, const Piece&) = default;
};

struct Position {
  Side side_to_move{Side::red};
  std::uint16_t fullmove_number{1};
  std::vector<Piece> pieces;

  friend bool operator==(const Position&, const Position&) = default;
};

struct Move {
  std::uint8_t from_square{};
  std::uint8_t to_square{};

  friend bool operator==(const Move&, const Move&) = default;
};

enum class AnnotationKind : std::uint8_t {
  comment,
  source_note,
};

struct Annotation {
  AnnotationKind kind{AnnotationKind::comment};
  bool before_move{false};
  std::string text;
  std::optional<std::string> author;
  std::optional<std::string> language;

  friend bool operator==(const Annotation&, const Annotation&) = default;
};

struct MoveNode {
  std::optional<std::size_t> parent;
  std::optional<Move> move;
  std::vector<std::size_t> children;
  std::vector<Annotation> annotations;

  friend bool operator==(const MoveNode&, const MoveNode&) = default;
};

struct MoveTree {
  std::vector<MoveNode> nodes{MoveNode{}};

  friend bool operator==(const MoveTree&, const MoveTree&) = default;
};

enum class DatePrecision : std::uint8_t {
  unknown,
  year,
  month,
  day,
  minute,
  second,
  subsecond,
};

enum class GameResult : std::uint8_t {
  unknown,
  red_win,
  black_win,
  draw,
  unfinished,
  aborted,
};

struct PlayerMetadata {
  std::optional<std::string> name;
  std::optional<std::string> id;
  std::optional<std::string> country;
  std::optional<std::int32_t> rating;
  std::optional<std::string> title;
  std::optional<std::string> team;
  std::optional<std::string> time_used;

  friend bool operator==(const PlayerMetadata&, const PlayerMetadata&) = default;
};

struct EventMetadata {
  std::optional<std::string> name;
  std::optional<std::string> id;
  std::optional<std::string> location;
  std::optional<std::string> organizer;
  std::optional<std::string> round;
  std::optional<std::string> type;
  std::optional<std::string> group;
  std::optional<std::string> board_number;
  std::optional<std::string> time_control;
  std::optional<std::string> start_time;
  std::optional<std::string> end_time;
  std::optional<DatePrecision> date_precision;

  friend bool operator==(const EventMetadata&, const EventMetadata&) = default;
};

struct OpeningMetadata {
  std::optional<std::string> name;
  std::optional<std::string> code;
  std::optional<std::string> id;

  friend bool operator==(const OpeningMetadata&, const OpeningMetadata&) = default;
};

struct Provenance {
  std::optional<std::string> source_format;
  std::optional<std::string> source_record_id;
  std::optional<std::string> source_uri;
  std::optional<std::string> import_note;
  std::optional<std::string> source_format_version;
  std::optional<std::string> source_library_id;
  std::optional<std::string> source_library_name;
  std::optional<std::string> source_category;

  friend bool operator==(const Provenance&, const Provenance&) = default;
};

using ExtensionValue = std::variant<std::string, std::vector<std::string>>;
using ExtensionProperties = std::map<std::string, ExtensionValue, std::less<>>;
using ExtensionMetadata = std::map<std::string, ExtensionProperties, std::less<>>;

struct GameMetadata {
  PlayerMetadata red_player;
  PlayerMetadata black_player;
  EventMetadata event;
  std::optional<GameResult> result;
  std::optional<std::string> result_text;
  OpeningMetadata opening;
  std::optional<std::string> title;
  std::vector<std::string> tags;
  std::optional<std::string> game_type;
  std::optional<std::string> referee;
  std::optional<std::string> recorder;
  std::optional<std::string> commentator;
  std::optional<std::string> commentator_uri;
  std::optional<std::string> creator;
  std::optional<std::string> creator_uri;
  std::optional<std::string> record_created_at;
  std::optional<std::string> record_modified_at;
  Provenance provenance;
  ExtensionMetadata extensions;

  friend bool operator==(const GameMetadata&, const GameMetadata&) = default;
};

struct GameModel {
  Uuid uuid;
  GameMetadata metadata;
  Position initial_position;
  MoveTree move_tree;

  friend bool operator==(const GameModel&, const GameModel&) = default;
};

}  // namespace oxq::core
