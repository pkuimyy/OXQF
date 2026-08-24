#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>
#include <oxq/core/reader.hpp>

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

}  // namespace

int main() {
  const std::filesystem::path vectors{OXQF_VECTOR_DIRECTORY};
  const auto minimal = oxq::core::read_oxq(read_file(vectors / "minimal.oxq"));
  if (!std::holds_alternative<oxq::core::ReaderResult>(minimal)) {
    return 1;
  }
  const auto& minimal_result = std::get<oxq::core::ReaderResult>(minimal);
  if (minimal_result.game.uuid.to_string() != "01980000-0000-7000-8000-000000000001" ||
      !minimal_result.game.initial_position.pieces.empty() ||
      minimal_result.game.move_tree.nodes.size() != 1 ||
      !minimal_result.diagnostics.canonical_ordering() ||
      minimal_result.diagnostics.skipped_unknown_sections != 0) {
    return 2;
  }

  const auto variation = oxq::core::read_oxq(read_file(vectors / "variation-zh.oxq"));
  if (!std::holds_alternative<oxq::core::ReaderResult>(variation)) {
    return 3;
  }
  const auto& variation_result = std::get<oxq::core::ReaderResult>(variation);
  if (variation_result.game.uuid.to_string() != "01980000-0000-7000-8000-000000000002" ||
      variation_result.game.metadata.title != "对局示例" ||
      variation_result.game.initial_position.pieces.size() != 5 ||
      variation_result.game.move_tree.nodes.size() != 4 ||
      variation_result.game.move_tree.nodes[0].children != std::vector<std::size_t>{1, 3} ||
      variation_result.game.move_tree.nodes[0].annotations.size() != 1 ||
      variation_result.game.move_tree.nodes[0].annotations[0].language != "zh-Hans" ||
      !variation_result.diagnostics.canonical_ordering()) {
    return 4;
  }

  const auto unknown = oxq::core::read_oxq(read_file(vectors / "unknown-noncritical.oxq"));
  if (!std::holds_alternative<oxq::core::ReaderResult>(unknown) ||
      std::get<oxq::core::ReaderResult>(unknown).diagnostics.skipped_unknown_sections != 1) {
    return 5;
  }

  const auto invalid = oxq::core::read_oxq(read_file(vectors / "invalid-magic-crlf.oxq"));
  if (!std::holds_alternative<oxq::core::CodecError>(invalid) ||
      std::get<oxq::core::CodecError>(invalid).code !=
          oxq::core::CodecErrorCode::invalid_magic ||
      std::get<oxq::core::CodecError>(invalid).offset != 4) {
    return 6;
  }

  oxq::core::ReaderLimits limits;
  limits.max_nodes = 3;
  const auto limited = oxq::core::read_oxq(read_file(vectors / "variation-zh.oxq"), limits);
  if (!std::holds_alternative<oxq::core::CodecError>(limited) ||
      std::get<oxq::core::CodecError>(limited).code !=
          oxq::core::CodecErrorCode::resource_limit) {
    return 7;
  }
  return 0;
}
