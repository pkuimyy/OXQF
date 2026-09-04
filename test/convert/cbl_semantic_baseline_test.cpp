#include <oxq/convert/cbl_reader.hpp>
#include <oxq/core/writer.hpp>

#include "cbl/hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::string_view, 12> kFiles{
    "cbl_00_empty.CBL",          "cbl_01_game_empty.CBL",
    "cbl_02_one_ply.CBL",        "cbl_03_two_plies.CBL",
    "cbl_04_mainline.CBL",       "cbl_05_variation.CBL",
    "cbl_06_nested_variation.CBL", "cbl_07_comments.CBL",
    "cbl_08_metadata.CBL",       "cbl_09_custom_position.CBL",
    "cbl_10_two_games.CBL",      "cbl_11_nested_folders.CBL",
};

[[nodiscard]] std::vector<std::byte> read_bytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  const std::vector<char> characters{std::istreambuf_iterator<char>{input},
                                     std::istreambuf_iterator<char>{}};
  std::vector<std::byte> bytes;
  bytes.reserve(characters.size());
  for (const char character : characters) {
    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return bytes;
}

[[nodiscard]] std::string json_string(std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result{"\""};
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (character < 0x20U) {
          result += "\\u00";
          result.push_back(hex[character >> 4U]);
          result.push_back(hex[character & 0x0fU]);
        } else {
          result.push_back(static_cast<char>(character));
        }
    }
  }
  result.push_back('"');
  return result;
}

[[nodiscard]] std::string hex_bytes(std::span<const std::byte> bytes) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2U);
  for (const auto byte : bytes) {
    const auto value = std::to_integer<unsigned char>(byte);
    result.push_back(hex[value >> 4U]);
    result.push_back(hex[value & 0x0fU]);
  }
  return result;
}

[[nodiscard]] std::string piece_name(oxq::core::PieceType type) {
  switch (type) {
    case oxq::core::PieceType::king: return "king";
    case oxq::core::PieceType::advisor: return "advisor";
    case oxq::core::PieceType::elephant: return "elephant";
    case oxq::core::PieceType::horse: return "horse";
    case oxq::core::PieceType::rook: return "rook";
    case oxq::core::PieceType::cannon: return "cannon";
    case oxq::core::PieceType::pawn: return "pawn";
  }
  return "unknown";
}

void append_game(std::string& output, std::vector<std::byte>* writer_bytes,
                 const oxq::core::GameModel& game, std::string_view indent) {
  const auto written = oxq::core::write_oxq(game);
  if (!std::holds_alternative<std::vector<std::byte>>(written)) {
    throw std::runtime_error("cannot encode normalized GameModel");
  }
  const auto& bytes = std::get<std::vector<std::byte>>(written);
  if (writer_bytes != nullptr) {
    auto size = static_cast<std::uint64_t>(bytes.size());
    for (std::size_t index = 0; index < sizeof(size); ++index) {
      writer_bytes->push_back(static_cast<std::byte>(size & 0xffU));
      size >>= 8U;
    }
    writer_bytes->insert(writer_bytes->end(), bytes.begin(), bytes.end());
  }
  output += std::string{indent} + "{\n";
  output += std::string{indent} + "  \"uuid\": " + json_string(game.uuid.to_string()) + ",\n";
  output += std::string{indent} + "  \"title\": ";
  output += game.metadata.title ? json_string(*game.metadata.title) : "null";
  output += ",\n";
  output += std::string{indent} + "  \"initial_position\": {\"side_to_move\": \"";
  output += game.initial_position.side_to_move == oxq::core::Side::red ? "red" : "black";
  output += "\", \"fullmove_number\": " + std::to_string(game.initial_position.fullmove_number) +
            ", \"pieces\": [";
  for (std::size_t index = 0; index < game.initial_position.pieces.size(); ++index) {
    if (index != 0) output += ", ";
    const auto& piece = game.initial_position.pieces[index];
    output += "{\"side\": \"";
    output += piece.side == oxq::core::Side::red ? "red" : "black";
    output += "\", \"type\": \"" + piece_name(piece.type) + "\", \"square\": " +
              std::to_string(piece.square) + "}";
  }
  output += "]},\n";
  output += std::string{indent} + "  \"move_tree\": [\n";
  for (std::size_t node_index = 0; node_index < game.move_tree.nodes.size(); ++node_index) {
    const auto& node = game.move_tree.nodes[node_index];
    output += std::string{indent} + "    {\"parent\": ";
    output += node.parent ? std::to_string(*node.parent) : "null";
    output += ", \"move\": ";
    if (node.move) {
      output += "[" + std::to_string(node.move->from_square) + ", " +
                std::to_string(node.move->to_square) + "]";
    } else {
      output += "null";
    }
    output += ", \"children\": [";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
      if (index != 0) output += ", ";
      output += std::to_string(node.children[index]);
    }
    output += "], \"annotations\": [";
    for (std::size_t index = 0; index < node.annotations.size(); ++index) {
      if (index != 0) output += ", ";
      const auto& annotation = node.annotations[index];
      output += "{\"kind\": \"";
      output += annotation.kind == oxq::core::AnnotationKind::comment ? "comment" : "source_note";
      output += "\", \"before_move\": ";
      output += annotation.before_move ? "true" : "false";
      output += ", \"text\": " + json_string(annotation.text) + "}";
    }
    output += "]}";
    output += node_index + 1U == game.move_tree.nodes.size() ? "\n" : ",\n";
  }
  output += std::string{indent} + "  ],\n";
  output += std::string{indent} + "  \"canonical_oxq_hex\": " + json_string(hex_bytes(bytes)) + "\n";
  output += std::string{indent} + "}";
}

[[nodiscard]] std::string build_baseline(
    const fs::path& directory, std::vector<std::byte>* writer_bytes = nullptr) {
  std::string output = "{\n  \"schema\": \"oxqf-cbl-semantic-baseline-v1\",\n  \"files\": [\n";
  for (std::size_t file_index = 0; file_index < kFiles.size(); ++file_index) {
    const auto outcome = oxq::convert::read_cbl(read_bytes(directory / kFiles[file_index]));
    if (!std::holds_alternative<oxq::convert::CblReadResult>(outcome)) {
      throw std::runtime_error("cannot decode golden CBL");
    }
    const auto& result = std::get<oxq::convert::CblReadResult>(outcome);
    if (result.report.rejected || result.report.has_loss()) {
      throw std::runtime_error("golden CBL has semantic loss");
    }
    output += "    {\n      \"file\": " + json_string(kFiles[file_index]) + ",\n";
    output += "      \"library_uuid\": " + json_string(result.library.uuid.to_string()) + ",\n";
    output += "      \"library_name\": " + json_string(result.library.name) + ",\n";
    output += "      \"games\": [\n";
    for (std::size_t game_index = 0; game_index < result.games.size(); ++game_index) {
      append_game(output, writer_bytes, result.games[game_index], "        ");
      output += game_index + 1U == result.games.size() ? "\n" : ",\n";
    }
    output += "      ]\n    }";
    output += file_index + 1U == kFiles.size() ? "\n" : ",\n";
  }
  output += "  ]\n}\n";
  return output;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const bool dump = argc == 2 && std::string_view(argv[1]) == "--dump";
    const bool fingerprint =
        argc == 2 && std::string_view(argv[1]) == "--fingerprint";
    if (argc != 1 && !dump && !fingerprint) {
      std::cerr <<
          "usage: oxq_convert_cbl_semantic_baseline_test [--dump|--fingerprint]\n";
      return 2;
    }

    const fs::path directory{OXQF_GOLD_BASELINE_DIRECTORY};
    std::vector<std::byte> writer_bytes;
    const auto actual = build_baseline(
        directory, fingerprint ? &writer_bytes : nullptr);
    if (dump) {
      std::cout << actual;
      return 0;
    }
    const auto expected_bytes = read_bytes(directory / "semantic-baseline.json");
    const std::string expected(reinterpret_cast<const char*>(expected_bytes.data()),
                               expected_bytes.size());
    if (actual != expected) {
      std::cerr << "semantic-baseline.json is stale; regenerate with --dump\n";
      return 1;
    }
    if (fingerprint) {
      std::cout << "sha256=" << oxq::convert::detail::sha256_hex(writer_bytes)
                << "\nframed_bytes=" << writer_bytes.size() << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
