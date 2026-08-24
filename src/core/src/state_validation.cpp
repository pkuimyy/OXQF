#include <oxq/core/state_validation.hpp>

#include <oxq/core/game_model.hpp>
#include <oxq/core/validation.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace oxq::core {
namespace {

[[nodiscard]] Side opposite(Side side) noexcept {
  return side == Side::red ? Side::black : Side::red;
}

struct Undo {
  std::uint8_t from{};
  std::uint8_t to{};
  Piece moving;
  std::optional<Piece> captured;
};

struct Frame {
  std::size_t node{};
  std::size_t next_child{};
  std::optional<Undo> undo;
};

}  // namespace

std::vector<ValidationIssue> validate_state(const GameModel& game) {
  auto issues = validate(game);
  if (has_errors(issues)) {
    return issues;
  }

  std::array<std::optional<Piece>, 90> board;
  for (const auto& piece : game.initial_position.pieces) {
    board[piece.square] = piece;
  }
  Side side_to_move = game.initial_position.side_to_move;
  std::vector<Frame> stack{{0, 0, std::nullopt}};

  while (!stack.empty()) {
    auto& frame = stack.back();
    const auto& node = game.move_tree.nodes[frame.node];
    if (frame.next_child == node.children.size()) {
      if (frame.undo.has_value()) {
        const auto undo = *frame.undo;
        side_to_move = opposite(side_to_move);
        board[undo.from] = undo.moving;
        board[undo.to] = undo.captured;
      }
      stack.pop_back();
      continue;
    }

    const auto child_index = node.children[frame.next_child++];
    const auto& child = game.move_tree.nodes[child_index];
    const auto move = *child.move;
    const std::string path =
        "move_tree.nodes[" + std::to_string(child_index) + "].move";
    if (!board[move.from_square].has_value()) {
      issues.push_back({ValidationSeverity::error, ValidationCode::missing_source_piece,
                        path + ".from_square", "move origin does not contain a piece"});
      continue;
    }
    const Piece moving = *board[move.from_square];
    if (moving.side != side_to_move) {
      issues.push_back({ValidationSeverity::error, ValidationCode::wrong_side_to_move,
                        path + ".from_square",
                        "move origin contains a piece belonging to the other side"});
    }
    if (board[move.to_square].has_value() &&
        board[move.to_square]->side == moving.side) {
      issues.push_back({ValidationSeverity::error,
                        ValidationCode::destination_occupied_by_same_side,
                        path + ".to_square",
                        "move destination contains a piece belonging to the moving side"});
      continue;
    }

    Undo undo{move.from_square, move.to_square, moving, board[move.to_square]};
    board[move.to_square] = moving;
    board[move.from_square].reset();
    side_to_move = opposite(side_to_move);
    stack.push_back({child_index, 0, undo});
  }
  return issues;
}

}  // namespace oxq::core
