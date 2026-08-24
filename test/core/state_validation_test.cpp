#include <oxq/core/game_model.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/state_validation.hpp>
#include <oxq/core/validation.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> characters{std::istreambuf_iterator<char>{stream},
                                     std::istreambuf_iterator<char>{}};
  std::vector<std::byte> result;
  result.reserve(characters.size());
  for (const char character : characters) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return result;
}

[[nodiscard]] bool contains(const std::vector<oxq::core::ValidationIssue>& issues,
                            oxq::core::ValidationCode code) {
  return std::ranges::any_of(issues, [code](const auto& issue) { return issue.code == code; });
}

}  // namespace

int main() {
  const std::filesystem::path vectors{OXQF_VECTOR_DIRECTORY};
  const auto read = oxq::core::read_oxq(read_file(vectors / "variation-zh.oxq"));
  if (!std::holds_alternative<oxq::core::ReaderResult>(read)) {
    return 1;
  }
  const auto game = std::get<oxq::core::ReaderResult>(read).game;
  if (oxq::core::has_errors(oxq::core::validate_state(game))) {
    return 2;
  }

  auto missing = game;
  missing.move_tree.nodes[1].move = oxq::core::Move{0, 36};
  if (!contains(oxq::core::validate_state(missing),
                oxq::core::ValidationCode::missing_source_piece)) {
    return 3;
  }

  auto wrong_side = game;
  wrong_side.move_tree.nodes[1].move = oxq::core::Move{63, 36};
  if (!contains(oxq::core::validate_state(wrong_side),
                oxq::core::ValidationCode::wrong_side_to_move)) {
    return 4;
  }

  auto own_destination = game;
  own_destination.move_tree.nodes[1].move = oxq::core::Move{27, 19};
  if (!contains(oxq::core::validate_state(own_destination),
                oxq::core::ValidationCode::destination_occupied_by_same_side)) {
    return 5;
  }

  auto malformed = game;
  malformed.move_tree.nodes[1].parent.reset();
  const auto malformed_issues = oxq::core::validate_state(malformed);
  if (!contains(malformed_issues, oxq::core::ValidationCode::invalid_parent) ||
      contains(malformed_issues, oxq::core::ValidationCode::missing_source_piece)) {
    return 6;
  }
  return 0;
}
