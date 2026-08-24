#include "codec/annotation.hpp"
#include "codec/container.hpp"
#include "codec/string_pool.hpp"

#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
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

[[nodiscard]] bool error_is(const oxq::core::detail::AnnotationResult& result,
                            oxq::core::CodecErrorCode code, std::size_t offset) {
  if (!std::holds_alternative<oxq::core::CodecError>(result)) {
    return false;
  }
  const auto& error = std::get<oxq::core::CodecError>(result);
  return error.code == code && error.offset == offset && error.section_type == 4;
}

}  // namespace

int main() {
  const std::filesystem::path vectors{OXQF_VECTOR_DIRECTORY};
  const auto minimal_bytes = read_file(vectors / "minimal.oxq");
  const auto minimal_container_result = oxq::core::detail::inspect_container(minimal_bytes);
  if (!std::holds_alternative<oxq::core::detail::ContainerView>(minimal_container_result)) {
    return 1;
  }
  const auto& minimal_container =
      std::get<oxq::core::detail::ContainerView>(minimal_container_result);
  const auto minimal_pool_result =
      oxq::core::detail::read_string_pool(minimal_bytes, minimal_container);
  if (!std::holds_alternative<oxq::core::detail::StringPoolView>(minimal_pool_result)) {
    return 2;
  }
  const auto minimal_result = oxq::core::detail::read_annotations(
      minimal_bytes, minimal_container,
      std::get<oxq::core::detail::StringPoolView>(minimal_pool_result));
  if (!std::holds_alternative<oxq::core::detail::AnnotationView>(minimal_result) ||
      !std::get<oxq::core::detail::AnnotationView>(minimal_result).records.empty()) {
    return 3;
  }

  auto bytes = read_file(vectors / "variation-zh.oxq");
  const auto container_result = oxq::core::detail::inspect_container(bytes);
  if (!std::holds_alternative<oxq::core::detail::ContainerView>(container_result)) {
    return 4;
  }
  const auto container = std::get<oxq::core::detail::ContainerView>(container_result);
  const auto pool_result = oxq::core::detail::read_string_pool(bytes, container);
  if (!std::holds_alternative<oxq::core::detail::StringPoolView>(pool_result)) {
    return 5;
  }
  const auto pool = std::get<oxq::core::detail::StringPoolView>(pool_result);
  const auto section = std::ranges::find(container.sections, 4U,
                                         &oxq::core::detail::SectionView::type);
  if (section == container.sections.end()) {
    return 6;
  }
  const auto annotation_result = oxq::core::detail::read_annotations(bytes, container, pool);
  if (!std::holds_alternative<oxq::core::detail::AnnotationView>(annotation_result)) {
    return 7;
  }
  const auto& annotations = std::get<oxq::core::detail::AnnotationView>(annotation_result);
  if (annotations.records.size() != 1 || annotations.records[0].next_annotation_id != 0 ||
      annotations.records[0].value.kind != oxq::core::AnnotationKind::comment ||
      annotations.records[0].value.before_move ||
      annotations.records[0].value.text != "根注释\n第二行" ||
      annotations.records[0].value.author.has_value() ||
      annotations.records[0].value.language != "zh-Hans") {
    return 8;
  }

  auto optional_fields = bytes;
  write_u16(optional_fields, section->offset + 24, 2);
  write_u16(optional_fields, section->offset + 26, 1);
  write_u32(optional_fields, section->offset + 32, 20);
  const auto optional_result =
      oxq::core::detail::read_annotations(optional_fields, container, pool);
  if (!std::holds_alternative<oxq::core::detail::AnnotationView>(optional_result)) {
    return 18;
  }
  const auto& optional = std::get<oxq::core::detail::AnnotationView>(optional_result).records[0];
  if (optional.value.kind != oxq::core::AnnotationKind::source_note ||
      !optional.value.before_move || optional.value.author != "对局示例") {
    return 19;
  }

  auto invalid = bytes;
  write_u32(invalid, section->offset + 16, 2);
  if (!error_is(oxq::core::detail::read_annotations(invalid, container, pool),
                oxq::core::CodecErrorCode::invalid_annotation, section->offset + 16)) {
    return 9;
  }
  invalid = bytes;
  write_u32(invalid, section->offset + 20, 2);
  if (!error_is(oxq::core::detail::read_annotations(invalid, container, pool),
                oxq::core::CodecErrorCode::invalid_annotation, section->offset + 20)) {
    return 10;
  }
  invalid = bytes;
  write_u16(invalid, section->offset + 24, 3);
  if (!error_is(oxq::core::detail::read_annotations(invalid, container, pool),
                oxq::core::CodecErrorCode::invalid_annotation, section->offset + 24)) {
    return 11;
  }
  invalid = bytes;
  write_u16(invalid, section->offset + 26, 2);
  if (!error_is(oxq::core::detail::read_annotations(invalid, container, pool),
                oxq::core::CodecErrorCode::invalid_annotation, section->offset + 26)) {
    return 12;
  }
  invalid = bytes;
  write_u32(invalid, section->offset + 28, 0);
  if (!error_is(oxq::core::detail::read_annotations(invalid, container, pool),
                oxq::core::CodecErrorCode::invalid_string_ref, section->offset + 28)) {
    return 13;
  }
  invalid = bytes;
  write_u32(invalid, section->offset + 32, 9);
  if (!error_is(oxq::core::detail::read_annotations(invalid, container, pool),
                oxq::core::CodecErrorCode::invalid_string_ref, section->offset + 32)) {
    return 14;
  }
  invalid = bytes;
  write_u32(invalid, section->offset + 36, 9);
  if (!error_is(oxq::core::detail::read_annotations(invalid, container, pool),
                oxq::core::CodecErrorCode::invalid_string_ref, section->offset + 36)) {
    return 15;
  }

  oxq::core::detail::AnnotationLimits limits;
  limits.max_annotations = 0;
  if (!error_is(oxq::core::detail::read_annotations(bytes, container, pool, limits),
                oxq::core::CodecErrorCode::resource_limit, section->offset + 8)) {
    return 16;
  }
  invalid = bytes;
  write_u32(invalid, section->offset + 8, 0);
  if (!error_is(oxq::core::detail::read_annotations(invalid, container, pool),
                oxq::core::CodecErrorCode::invalid_annotation, section->offset + 8)) {
    return 17;
  }
  return 0;
}
