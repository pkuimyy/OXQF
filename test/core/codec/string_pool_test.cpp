#include "codec/container.hpp"
#include "codec/string_pool.hpp"

#include <oxq/core/codec_error.hpp>

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

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<std::byte>((value >> static_cast<unsigned>(index * 8U)) & 0xffU);
  }
}

[[nodiscard]] bool error_is(const oxq::core::detail::StringPoolResult& result,
                            oxq::core::CodecErrorCode code, std::size_t offset) {
  if (!std::holds_alternative<oxq::core::CodecError>(result)) {
    return false;
  }
  const auto& error = std::get<oxq::core::CodecError>(result);
  return error.code == code && error.offset == offset && error.section_type == 5;
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
  const auto& minimal_pool = std::get<oxq::core::detail::StringPoolView>(minimal_pool_result);
  if (!minimal_pool.records.empty() || !minimal_pool.canonical_order ||
      minimal_pool.find(0).has_value()) {
    return 3;
  }

  auto bytes = read_file(vectors / "variation-zh.oxq");
  const auto container_result = oxq::core::detail::inspect_container(bytes);
  if (!std::holds_alternative<oxq::core::detail::ContainerView>(container_result)) {
    return 4;
  }
  const auto container = std::get<oxq::core::detail::ContainerView>(container_result);
  const auto section = std::ranges::find(container.sections, 5U,
                                         &oxq::core::detail::SectionView::type);
  if (section == container.sections.end()) {
    return 5;
  }
  const auto pool_result = oxq::core::detail::read_string_pool(bytes, container);
  if (!std::holds_alternative<oxq::core::detail::StringPoolView>(pool_result)) {
    return 6;
  }
  const auto& pool = std::get<oxq::core::detail::StringPoolView>(pool_result);
  if (pool.records.size() != 3 || !pool.canonical_order || pool.find(8) != "zh-Hans" ||
      pool.find(20) != "对局示例" || pool.find(36) != "根注释\n第二行" ||
      pool.find(9).has_value()) {
    return 7;
  }

  auto invalid_utf8 = bytes;
  invalid_utf8[section->offset + 12] = std::byte{0xc0};
  if (!error_is(oxq::core::detail::read_string_pool(invalid_utf8, container),
                oxq::core::CodecErrorCode::invalid_utf8, section->offset + 12)) {
    return 8;
  }

  auto invalid_padding = bytes;
  invalid_padding[section->offset + 19] = std::byte{1};
  if (!error_is(oxq::core::detail::read_string_pool(invalid_padding, container),
                oxq::core::CodecErrorCode::invalid_string_ref, section->offset + 19)) {
    return 9;
  }

  auto impossible_count = bytes;
  write_u32(impossible_count, section->offset + 4, 1000);
  if (!error_is(oxq::core::detail::read_string_pool(impossible_count, container),
                oxq::core::CodecErrorCode::invalid_string_ref, section->offset + 4)) {
    return 10;
  }

  auto trailing_record = bytes;
  write_u32(trailing_record, section->offset + 4, 2);
  if (!error_is(oxq::core::detail::read_string_pool(trailing_record, container),
                oxq::core::CodecErrorCode::invalid_string_ref, section->offset + 36)) {
    return 11;
  }

  oxq::core::detail::StringPoolLimits limits;
  limits.max_strings = 2;
  if (!error_is(oxq::core::detail::read_string_pool(bytes, container, limits),
                oxq::core::CodecErrorCode::resource_limit, section->offset + 4)) {
    return 12;
  }

  const std::vector<std::byte> noncanonical{
      std::byte{1},  std::byte{0}, std::byte{8}, std::byte{0}, std::byte{2}, std::byte{0},
      std::byte{0},  std::byte{0}, std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{'b'}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0},
      std::byte{0},  std::byte{0}, std::byte{'a'}, std::byte{0}, std::byte{0}, std::byte{0},
  };
  oxq::core::detail::ContainerView synthetic;
  synthetic.sections.push_back({5, 1, 0, noncanonical.size(), 0});
  const auto noncanonical_result = oxq::core::detail::read_string_pool(noncanonical, synthetic);
  if (!std::holds_alternative<oxq::core::detail::StringPoolView>(noncanonical_result) ||
      std::get<oxq::core::detail::StringPoolView>(noncanonical_result).canonical_order) {
    return 13;
  }

  const std::vector<std::byte> empty_string{
      std::byte{1}, std::byte{0}, std::byte{8}, std::byte{0}, std::byte{1}, std::byte{0},
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
  };
  synthetic.sections[0].size = empty_string.size();
  const auto empty_result = oxq::core::detail::read_string_pool(empty_string, synthetic);
  if (!std::holds_alternative<oxq::core::detail::StringPoolView>(empty_result)) {
    return 14;
  }
  const auto empty_value = std::get<oxq::core::detail::StringPoolView>(empty_result).find(8);
  if (!empty_value.has_value() || !empty_value->empty()) {
    return 15;
  }

  const std::vector<std::byte> decomposed_nfc{
      std::byte{1},   std::byte{0},    std::byte{8},    std::byte{0},
      std::byte{1},   std::byte{0},    std::byte{0},    std::byte{0},
      std::byte{3},   std::byte{0},    std::byte{0},    std::byte{0},
      std::byte{'e'}, std::byte{0xcc}, std::byte{0x81}, std::byte{0},
  };
  synthetic.sections[0].size = decomposed_nfc.size();
  const auto decomposed_result =
      oxq::core::detail::read_string_pool(decomposed_nfc, synthetic);
  if (!std::holds_alternative<oxq::core::detail::StringPoolView>(decomposed_result) ||
      std::get<oxq::core::detail::StringPoolView>(decomposed_result).canonical_nfc) {
    return 16;
  }
  return 0;
}
