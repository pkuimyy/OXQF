#include <oxq/core/game_model.hpp>
#include <oxq/core/validation.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using oxq::core::GameModel;
using oxq::core::Move;
using oxq::core::MoveNode;
using oxq::core::Piece;
using oxq::core::PieceType;
using oxq::core::Side;
using oxq::core::ValidationCode;

[[nodiscard]] bool has_code(const std::vector<oxq::core::ValidationIssue>& issues,
                            ValidationCode code) {
  return std::ranges::any_of(issues, [code](const auto& issue) { return issue.code == code; });
}

[[nodiscard]] GameModel valid_game() {
  GameModel game;
  game.uuid = *oxq::core::Uuid::parse("01980000-0000-7000-8000-000000000010");
  game.metadata.title = "";  // Present-but-empty is distinct from std::nullopt.
  game.metadata.tags = {"研究", "待复核"};
  game.metadata.extensions["org.openxiangqi.cbl"]["display_index"] = std::string{"12"};
  game.metadata.extensions["com.example.application"]["collection"] =
      std::vector<std::string>{"研究", "待复核"};
  game.initial_position.pieces = {
      Piece{Side::red, PieceType::king, 4},
      Piece{Side::black, PieceType::king, 85},
  };
  game.move_tree.nodes[0].children = {1, 2};
  oxq::core::Annotation root_annotation;
  root_annotation.text = "根注释\n第二行";
  root_annotation.language = "zh-Hans";
  game.move_tree.nodes[0].annotations.push_back(std::move(root_annotation));
  MoveNode first_move;
  first_move.parent = 0;
  first_move.move = Move{4, 13};
  game.move_tree.nodes.push_back(std::move(first_move));
  MoveNode variation;
  variation.parent = 0;
  variation.move = Move{4, 5};
  game.move_tree.nodes.push_back(std::move(variation));
  return game;
}

}  // namespace

int main() {
  const auto parsed = oxq::core::Uuid::parse("01980000-0000-7000-8000-000000000010");
  if (!parsed.has_value() || parsed->to_string() != "01980000-0000-7000-8000-000000000010" ||
      oxq::core::Uuid::parse("not-a-uuid").has_value()) {
    return 1;
  }

  const GameModel game = valid_game();
  if (oxq::core::has_errors(oxq::core::validate(game))) {
    return 2;
  }
  const GameModel copied = game;
  GameModel move_source = copied;
  GameModel moved = std::move(move_source);
  if (!(moved == game) || !moved.metadata.title.has_value() || !moved.metadata.title->empty()) {
    return 3;
  }

  GameModel invalid = game;
  invalid.uuid = {};
  invalid.initial_position.pieces.push_back(Piece{Side::red, PieceType::rook, 4});
  invalid.move_tree.nodes[1].parent = 2;
  invalid.metadata.extensions["Invalid Namespace"]["Bad-Key"] = std::vector<std::string>{};
  invalid.metadata.tags.push_back(std::string{"\xc0\x80", 2});
  const auto issues = oxq::core::validate(invalid);
  if (!has_code(issues, ValidationCode::nil_uuid) ||
      !has_code(issues, ValidationCode::duplicate_square) ||
      !has_code(issues, ValidationCode::invalid_parent) ||
      !has_code(issues, ValidationCode::invalid_extension_namespace) ||
      !has_code(issues, ValidationCode::invalid_extension_key) ||
      !has_code(issues, ValidationCode::empty_extension_array) ||
      !has_code(issues, ValidationCode::invalid_utf8)) {
    return 4;
  }

  GameModel invalid_variant = game;
  invalid_variant.uuid.bytes[8] = 0;
  if (!has_code(oxq::core::validate(invalid_variant), ValidationCode::invalid_uuid_variant)) {
    return 5;
  }

  GameModel deep = valid_game();
  deep.move_tree.nodes = {MoveNode{}};
  constexpr std::size_t depth = 20'000;
  deep.move_tree.nodes.reserve(depth + 1);
  for (std::size_t index = 1; index <= depth; ++index) {
    deep.move_tree.nodes[index - 1].children.push_back(index);
    MoveNode node;
    node.parent = index - 1;
    node.move = Move{0, 1};
    deep.move_tree.nodes.push_back(std::move(node));
  }
  oxq::core::ValidationLimits limits;
  limits.max_tree_depth = depth - 1;
  const auto deep_issues = oxq::core::validate(deep, limits);
  if (!has_code(deep_issues, ValidationCode::tree_too_deep)) {
    return 6;
  }

  return 0;
}
