#include "codec/annotation.hpp"
#include "codec/container.hpp"
#include "codec/move_tree.hpp"
#include "codec/string_pool.hpp"

#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

void write_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> static_cast<unsigned>(index * 8U)) & 0xffU);
  }
}

struct ParsedFixture {
  oxq::core::detail::ContainerView container;
  oxq::core::detail::AnnotationView annotations;
};

[[nodiscard]] std::variant<ParsedFixture, int> parse_fixture(
    const std::vector<std::byte>& bytes) {
  const auto container_result = oxq::core::detail::inspect_container(bytes);
  if (!std::holds_alternative<oxq::core::detail::ContainerView>(container_result)) {
    return 1;
  }
  auto container = std::get<oxq::core::detail::ContainerView>(container_result);
  const auto pool_result = oxq::core::detail::read_string_pool(bytes, container);
  if (!std::holds_alternative<oxq::core::detail::StringPoolView>(pool_result)) {
    return 2;
  }
  const auto annotation_result = oxq::core::detail::read_annotations(
      bytes, container, std::get<oxq::core::detail::StringPoolView>(pool_result));
  if (!std::holds_alternative<oxq::core::detail::AnnotationView>(annotation_result)) {
    return 3;
  }
  return ParsedFixture{std::move(container),
                       std::get<oxq::core::detail::AnnotationView>(annotation_result)};
}

[[nodiscard]] bool error_is(const oxq::core::detail::MoveTreeResult& result,
                            oxq::core::CodecErrorCode code, std::size_t offset,
                            std::uint32_t section_type) {
  if (!std::holds_alternative<oxq::core::CodecError>(result)) {
    return false;
  }
  const auto& error = std::get<oxq::core::CodecError>(result);
  return error.code == code && error.offset == offset && error.section_type == section_type;
}

}  // namespace

int main() {
  const std::filesystem::path vectors{OXQF_VECTOR_DIRECTORY};
  const auto minimal_bytes = read_file(vectors / "minimal.oxq");
  const auto minimal_fixture_result = parse_fixture(minimal_bytes);
  if (!std::holds_alternative<ParsedFixture>(minimal_fixture_result)) {
    return 1;
  }
  const auto& minimal_fixture = std::get<ParsedFixture>(minimal_fixture_result);
  const auto minimal_result = oxq::core::detail::read_move_tree(
      minimal_bytes, minimal_fixture.container, minimal_fixture.annotations);
  if (!std::holds_alternative<oxq::core::detail::DecodedMoveTree>(minimal_result)) {
    return 2;
  }
  const auto& minimal = std::get<oxq::core::detail::DecodedMoveTree>(minimal_result);
  if (minimal.value.nodes.size() != 1 || minimal.value.nodes[0].parent.has_value() ||
      minimal.value.nodes[0].move.has_value() || !minimal.value.nodes[0].children.empty() ||
      !minimal.value.nodes[0].annotations.empty() || !minimal.canonical_order) {
    return 3;
  }

  auto bytes = read_file(vectors / "variation-zh.oxq");
  const auto fixture_result = parse_fixture(bytes);
  if (!std::holds_alternative<ParsedFixture>(fixture_result)) {
    return 4;
  }
  const auto fixture = std::get<ParsedFixture>(fixture_result);
  const auto tree_section = std::ranges::find(fixture.container.sections, 3U,
                                              &oxq::core::detail::SectionView::type);
  const auto annotation_section = std::ranges::find(fixture.container.sections, 4U,
                                                    &oxq::core::detail::SectionView::type);
  if (tree_section == fixture.container.sections.end() ||
      annotation_section == fixture.container.sections.end()) {
    return 5;
  }
  const auto tree_result =
      oxq::core::detail::read_move_tree(bytes, fixture.container, fixture.annotations);
  if (!std::holds_alternative<oxq::core::detail::DecodedMoveTree>(tree_result)) {
    return 6;
  }
  const auto& decoded = std::get<oxq::core::detail::DecodedMoveTree>(tree_result);
  const auto& nodes = decoded.value.nodes;
  if (!decoded.canonical_order || nodes.size() != 4 ||
      nodes[0].children != std::vector<std::size_t>{1, 3} ||
      nodes[1].children != std::vector<std::size_t>{2} || nodes[1].parent != 0 ||
      nodes[1].move != oxq::core::Move{27, 36} || nodes[2].parent != 1 ||
      nodes[2].move != oxq::core::Move{63, 54} || nodes[3].parent != 0 ||
      nodes[3].move != oxq::core::Move{19, 22} || nodes[0].annotations.size() != 1 ||
      nodes[0].annotations[0].text != "根注释\n第二行") {
    return 7;
  }

  auto invalid = bytes;
  invalid[tree_section->offset + 32] = std::byte{0};
  if (!error_is(oxq::core::detail::read_move_tree(
                    invalid, fixture.container, fixture.annotations),
                oxq::core::CodecErrorCode::invalid_tree, tree_section->offset + 16, 3)) {
    return 8;
  }
  invalid = bytes;
  invalid[tree_section->offset + 64] = std::byte{90};
  if (!error_is(oxq::core::detail::read_move_tree(
                    invalid, fixture.container, fixture.annotations),
                oxq::core::CodecErrorCode::invalid_move, tree_section->offset + 64, 3)) {
    return 9;
  }
  invalid = bytes;
  write_u32(invalid, tree_section->offset + 48, 2);
  if (!error_is(oxq::core::detail::read_move_tree(
                    invalid, fixture.container, fixture.annotations),
                oxq::core::CodecErrorCode::invalid_tree, tree_section->offset + 48, 3)) {
    return 10;
  }
  invalid = bytes;
  write_u32(invalid, tree_section->offset + 56, 1);
  if (!error_is(oxq::core::detail::read_move_tree(
                    invalid, fixture.container, fixture.annotations),
                oxq::core::CodecErrorCode::invalid_tree, tree_section->offset + 56, 3)) {
    return 11;
  }
  invalid = bytes;
  write_u32(invalid, tree_section->offset + 68, 0);
  if (!error_is(oxq::core::detail::read_move_tree(
                    invalid, fixture.container, fixture.annotations),
                oxq::core::CodecErrorCode::invalid_tree, tree_section->offset + 68, 3)) {
    return 12;
  }
  invalid = bytes;
  write_u32(invalid, tree_section->offset + 28, 0);
  if (!error_is(oxq::core::detail::read_move_tree(
                    invalid, fixture.container, fixture.annotations),
                oxq::core::CodecErrorCode::invalid_annotation,
                annotation_section->offset + 16, 4)) {
    return 13;
  }

  auto cycle_annotations = fixture.annotations;
  cycle_annotations.records[0].next_annotation_id = 1;
  if (!error_is(oxq::core::detail::read_move_tree(
                    bytes, fixture.container, cycle_annotations),
                oxq::core::CodecErrorCode::invalid_annotation,
                annotation_section->offset + 20, 4)) {
    return 14;
  }
  auto before_move_annotations = fixture.annotations;
  before_move_annotations.records[0].value.before_move = true;
  if (!error_is(oxq::core::detail::read_move_tree(
                    bytes, fixture.container, before_move_annotations),
                oxq::core::CodecErrorCode::invalid_annotation,
                annotation_section->offset + 26, 4)) {
    return 15;
  }

  oxq::core::detail::MoveTreeLimits limits;
  limits.max_nodes = 3;
  if (!error_is(oxq::core::detail::read_move_tree(
                    bytes, fixture.container, fixture.annotations, limits),
                oxq::core::CodecErrorCode::resource_limit, tree_section->offset + 8, 3)) {
    return 16;
  }
  limits.max_nodes = 4;
  limits.max_tree_depth = 1;
  if (!error_is(oxq::core::detail::read_move_tree(
                    bytes, fixture.container, fixture.annotations, limits),
                oxq::core::CodecErrorCode::resource_limit, tree_section->offset + 100, 3)) {
    return 17;
  }

  std::vector<std::byte> noncanonical(16 + 3 * 32, std::byte{0});
  write_u16(noncanonical, 0, 1);
  write_u16(noncanonical, 2, 16);
  write_u16(noncanonical, 4, 32);
  write_u32(noncanonical, 8, 3);
  write_u32(noncanonical, 16, 0xffffffffU);
  write_u32(noncanonical, 20, 2);
  write_u32(noncanonical, 24, 0xffffffffU);
  noncanonical[32] = std::byte{0xff};
  noncanonical[33] = std::byte{0xff};
  noncanonical[34] = std::byte{0xff};
  noncanonical[35] = std::byte{0xff};
  write_u32(noncanonical, 48, 0);
  write_u32(noncanonical, 52, 0xffffffffU);
  write_u32(noncanonical, 56, 0xffffffffU);
  noncanonical[64] = std::byte{1};
  noncanonical[65] = std::byte{2};
  write_u32(noncanonical, 68, 1);
  write_u32(noncanonical, 80, 0);
  write_u32(noncanonical, 84, 0xffffffffU);
  write_u32(noncanonical, 88, 1);
  noncanonical[96] = std::byte{3};
  noncanonical[97] = std::byte{4};
  write_u32(noncanonical, 100, 1);
  oxq::core::detail::ContainerView synthetic;
  synthetic.sections.push_back({3, 1, 0, noncanonical.size(), 0});
  const oxq::core::detail::AnnotationView no_annotations;
  const auto noncanonical_result =
      oxq::core::detail::read_move_tree(noncanonical, synthetic, no_annotations);
  if (!std::holds_alternative<oxq::core::detail::DecodedMoveTree>(noncanonical_result) ||
      std::get<oxq::core::detail::DecodedMoveTree>(noncanonical_result).canonical_order ||
      std::get<oxq::core::detail::DecodedMoveTree>(noncanonical_result)
              .value.nodes[0].children != std::vector<std::size_t>{2, 1}) {
    return 18;
  }
  return 0;
}
