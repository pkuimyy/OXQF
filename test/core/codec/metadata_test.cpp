#include "codec/container.hpp"
#include "codec/metadata.hpp"
#include "codec/string_pool.hpp"

#include <oxq/core/codec_error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
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
    bytes[offset + index] = static_cast<std::byte>((value >> static_cast<unsigned>(index * 8U)) & 0xffU);
  }
}

[[nodiscard]] bool error_is(const oxq::core::detail::MetadataResult& result,
                            oxq::core::CodecErrorCode code, std::size_t offset) {
  if (!std::holds_alternative<oxq::core::CodecError>(result)) {
    return false;
  }
  const auto& error = std::get<oxq::core::CodecError>(result);
  return error.code == code && error.offset == offset && error.section_type == 1;
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
  const auto minimal_metadata = oxq::core::detail::read_metadata(
      minimal_bytes, minimal_container,
      std::get<oxq::core::detail::StringPoolView>(minimal_pool_result));
  if (!std::holds_alternative<oxq::core::detail::MetadataView>(minimal_metadata) ||
      !std::get<oxq::core::detail::MetadataView>(minimal_metadata).fields.empty()) {
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
  const auto section = std::ranges::find(container.sections, 1U,
                                         &oxq::core::detail::SectionView::type);
  if (section == container.sections.end()) {
    return 6;
  }
  const auto metadata_result = oxq::core::detail::read_metadata(bytes, container, pool);
  if (!std::holds_alternative<oxq::core::detail::MetadataView>(metadata_result)) {
    return 7;
  }
  const auto& metadata = std::get<oxq::core::detail::MetadataView>(metadata_result);
  if (metadata.fields.size() != 1 || metadata.fields[0].tag != 0x0050 ||
      metadata.fields[0].string_value != "对局示例" || !metadata.fields[0].standard ||
      !metadata.canonical_order || metadata.unknown_field_count != 0) {
    return 8;
  }
  const auto decoded_title = oxq::core::detail::decode_metadata(metadata);
  if (!std::holds_alternative<oxq::core::detail::DecodedMetadata>(decoded_title) ||
      std::get<oxq::core::detail::DecodedMetadata>(decoded_title).value.title != "对局示例") {
    return 17;
  }

  auto wrong_type = bytes;
  wrong_type[section->offset + 10] = std::byte{1};
  if (!error_is(oxq::core::detail::read_metadata(wrong_type, container, pool),
                oxq::core::CodecErrorCode::invalid_metadata, section->offset + 10)) {
    return 9;
  }

  auto invalid_reference = bytes;
  write_u32(invalid_reference, section->offset + 16, 9);
  if (!error_is(oxq::core::detail::read_metadata(invalid_reference, container, pool),
                oxq::core::CodecErrorCode::invalid_string_ref, section->offset + 16)) {
    return 10;
  }

  auto unknown_critical = bytes;
  write_u16(unknown_critical, section->offset + 8, 0x2000);
  unknown_critical[section->offset + 11] = std::byte{1};
  if (!error_is(oxq::core::detail::read_metadata(unknown_critical, container, pool),
                oxq::core::CodecErrorCode::invalid_metadata, section->offset + 8)) {
    return 11;
  }

  auto unknown_type = bytes;
  write_u16(unknown_type, section->offset + 8, 0x2000);
  unknown_type[section->offset + 10] = std::byte{99};
  const auto unknown_result = oxq::core::detail::read_metadata(unknown_type, container, pool);
  if (!std::holds_alternative<oxq::core::detail::MetadataView>(unknown_result)) {
    return 12;
  }
  const auto& unknown = std::get<oxq::core::detail::MetadataView>(unknown_result);
  if (unknown.unknown_field_count != 1 || unknown.unknown_value_type_count != 1 ||
      unknown.fields[0].standard) {
    return 13;
  }

  std::vector<std::byte> duplicate(32);
  write_u16(duplicate, 0, 1);
  write_u16(duplicate, 2, 8);
  write_u32(duplicate, 4, 2);
  for (std::size_t field = 0; field < 2; ++field) {
    const std::size_t offset = 8 + field * 12;
    write_u16(duplicate, offset, 0x0050);
    duplicate[offset + 2] = std::byte{5};
    write_u32(duplicate, offset + 4, 4);
    write_u32(duplicate, offset + 8, 20);
  }
  oxq::core::detail::ContainerView synthetic;
  synthetic.sections.push_back({1, 1, 0, duplicate.size(), 0});
  if (!error_is(oxq::core::detail::read_metadata(duplicate, synthetic, pool),
                oxq::core::CodecErrorCode::invalid_metadata, 20)) {
    return 14;
  }

  std::vector<std::byte> invalid_boolean(20);
  write_u16(invalid_boolean, 0, 1);
  write_u16(invalid_boolean, 2, 8);
  write_u32(invalid_boolean, 4, 1);
  write_u16(invalid_boolean, 8, 0x2000);
  invalid_boolean[10] = std::byte{7};
  write_u32(invalid_boolean, 12, 1);
  invalid_boolean[16] = std::byte{2};
  synthetic.sections[0].size = invalid_boolean.size();
  if (!error_is(oxq::core::detail::read_metadata(invalid_boolean, synthetic, pool),
                oxq::core::CodecErrorCode::invalid_metadata, 16)) {
    return 15;
  }

  oxq::core::detail::MetadataLimits limits;
  limits.max_fields = 0;
  if (!error_is(oxq::core::detail::read_metadata(bytes, container, pool, limits),
                oxq::core::CodecErrorCode::resource_limit, section->offset + 4)) {
    return 16;
  }

  const std::array<std::byte, 4> rating_bytes{
      std::byte{0x35}, std::byte{0x08}, std::byte{0}, std::byte{0}};
  const std::array<std::byte, 4> result_bytes{
      std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
  const std::string extension_json =
      R"({"org.openxiangqi.cbl":{"display_index":"12"}})";
  oxq::core::detail::MetadataView projection;
  projection.fields.push_back(
      {0x0007, 2, 0, rating_bytes, std::nullopt, 100, std::nullopt, true});
  projection.fields.push_back(
      {0x0030, 1, 0, result_bytes, std::nullopt, 104, std::nullopt, true});
  projection.fields.push_back(
      {0x0051, 5, 2, {}, std::string_view{"研究"}, 108, 200, true});
  projection.fields.push_back(
      {0x7fff, 5, 0, {}, std::string_view{extension_json}, 112, 300, true});
  const auto projection_result = oxq::core::detail::decode_metadata(projection);
  if (!std::holds_alternative<oxq::core::detail::DecodedMetadata>(projection_result)) {
    return 18;
  }
  const auto& projected = std::get<oxq::core::detail::DecodedMetadata>(projection_result);
  if (projected.value.red_player.rating != 2101 ||
      projected.value.result != oxq::core::GameResult::red_win ||
      projected.value.tags != std::vector<std::string>{"研究"} ||
      std::get<std::string>(
          projected.value.extensions.at("org.openxiangqi.cbl").at("display_index")) != "12" ||
      !projected.canonical_extensions) {
    return 19;
  }

  const std::string invalid_json = R"({"org.example":{"value":1}})";
  projection.fields.back().string_value = invalid_json;
  projection.fields.back().string_data_offset = 400;
  const auto invalid_projection = oxq::core::detail::decode_metadata(projection);
  if (!std::holds_alternative<oxq::core::CodecError>(invalid_projection) ||
      std::get<oxq::core::CodecError>(invalid_projection).code !=
          oxq::core::CodecErrorCode::invalid_metadata ||
      std::get<oxq::core::CodecError>(invalid_projection).offset < 400) {
    return 20;
  }

  const std::array<std::byte, 4> day_precision{
      std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0}};
  oxq::core::detail::MetadataView dated;
  dated.fields.push_back(
      {0x0020, 5, 0, {}, std::string_view{"2024-02-29"}, 500, 600, true});
  dated.fields.push_back(
      {0x0022, 1, 0, day_precision, std::nullopt, 504, std::nullopt, true});
  const auto valid_date = oxq::core::detail::decode_metadata(dated);
  if (!std::holds_alternative<oxq::core::detail::DecodedMetadata>(valid_date) ||
      std::get<oxq::core::detail::DecodedMetadata>(valid_date).value.event.start_time !=
          "2024-02-29") {
    return 21;
  }
  dated.fields[0].string_value = "2023-02-29";
  const auto invalid_date = oxq::core::detail::decode_metadata(dated);
  if (!std::holds_alternative<oxq::core::CodecError>(invalid_date) ||
      std::get<oxq::core::CodecError>(invalid_date).code !=
          oxq::core::CodecErrorCode::invalid_metadata ||
      std::get<oxq::core::CodecError>(invalid_date).offset != 600) {
    return 22;
  }
  dated.fields.erase(dated.fields.begin());
  if (!std::holds_alternative<oxq::core::CodecError>(
          oxq::core::detail::decode_metadata(dated))) {
    return 23;
  }
  return 0;
}
