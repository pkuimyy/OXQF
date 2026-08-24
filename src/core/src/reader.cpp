#include <oxq/core/reader.hpp>

#include "codec/annotation.hpp"
#include "codec/container.hpp"
#include "codec/metadata.hpp"
#include "codec/move_tree.hpp"
#include "codec/position.hpp"
#include "codec/string_pool.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <variant>

namespace oxq::core {

bool ReaderDiagnostics::canonical_ordering() const noexcept {
  return canonical_section_order && canonical_string_pool_order && canonical_string_nfc &&
         canonical_metadata_order && canonical_extended_metadata && canonical_piece_order &&
         canonical_node_order;
}

ReaderOutcome read_oxq(std::span<const std::byte> input, const ReaderLimits& limits) {
  const auto container_result = detail::inspect_container(
      input, {limits.max_file_size, limits.max_sections});
  if (std::holds_alternative<CodecError>(container_result)) {
    return std::get<CodecError>(container_result);
  }
  const auto& container = std::get<detail::ContainerView>(container_result);

  const auto string_pool_result = detail::read_string_pool(
      input, container,
      {limits.max_strings, limits.max_string_bytes, limits.max_total_string_bytes});
  if (std::holds_alternative<CodecError>(string_pool_result)) {
    return std::get<CodecError>(string_pool_result);
  }
  const auto& strings = std::get<detail::StringPoolView>(string_pool_result);

  const auto metadata_result = detail::read_metadata(
      input, container, strings, {limits.max_metadata_fields});
  if (std::holds_alternative<CodecError>(metadata_result)) {
    return std::get<CodecError>(metadata_result);
  }
  const auto& metadata = std::get<detail::MetadataView>(metadata_result);
  auto decoded_metadata_result =
      detail::decode_metadata(metadata, limits.max_extended_metadata_bytes);
  if (std::holds_alternative<CodecError>(decoded_metadata_result)) {
    return std::get<CodecError>(decoded_metadata_result);
  }
  auto decoded_metadata = std::get<detail::DecodedMetadata>(std::move(decoded_metadata_result));

  auto position_result = detail::read_position(input, container);
  if (std::holds_alternative<CodecError>(position_result)) {
    return std::get<CodecError>(position_result);
  }
  auto position = std::get<detail::PositionView>(std::move(position_result));

  const auto annotation_result = detail::read_annotations(
      input, container, strings, {limits.max_annotations});
  if (std::holds_alternative<CodecError>(annotation_result)) {
    return std::get<CodecError>(annotation_result);
  }
  const auto& annotations = std::get<detail::AnnotationView>(annotation_result);

  auto move_tree_result = detail::read_move_tree(
      input, container, annotations, {limits.max_nodes, limits.max_tree_depth});
  if (std::holds_alternative<CodecError>(move_tree_result)) {
    return std::get<CodecError>(move_tree_result);
  }
  auto move_tree = std::get<detail::DecodedMoveTree>(std::move(move_tree_result));

  ReaderResult result;
  result.game.uuid.bytes = container.uuid;
  result.game.metadata = std::move(decoded_metadata.value);
  result.game.initial_position = std::move(position.value);
  result.game.move_tree = std::move(move_tree.value);
  result.diagnostics.canonical_section_order = container.canonical_order;
  result.diagnostics.canonical_string_pool_order = strings.canonical_order;
  result.diagnostics.canonical_string_nfc = strings.canonical_nfc;
  result.diagnostics.canonical_metadata_order = metadata.canonical_order;
  result.diagnostics.canonical_extended_metadata = decoded_metadata.canonical_extensions;
  result.diagnostics.canonical_piece_order = position.canonical_order;
  result.diagnostics.canonical_node_order = move_tree.canonical_order;
  result.diagnostics.skipped_unknown_sections = container.unknown_section_count;
  result.diagnostics.skipped_unknown_metadata_fields = metadata.unknown_field_count;
  result.diagnostics.skipped_unknown_metadata_value_types = metadata.unknown_value_type_count;
  return result;
}

}  // namespace oxq::core
