#include "codec/binary.hpp"
#include "codec/container.hpp"
#include "codec/crc32c.hpp"

#include <oxq/core/codec_error.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
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

[[nodiscard]] bool valid(const std::filesystem::path& path, std::size_t section_count) {
  const auto bytes = read_file(path);
  const auto result = oxq::core::detail::inspect_container(bytes);
  if (!std::holds_alternative<oxq::core::detail::ContainerView>(result)) {
    return false;
  }
  return std::get<oxq::core::detail::ContainerView>(result).sections.size() == section_count;
}

[[nodiscard]] bool invalid_magic(const std::filesystem::path& path, std::size_t offset) {
  const auto bytes = read_file(path);
  const auto result = oxq::core::detail::inspect_container(bytes);
  if (!std::holds_alternative<oxq::core::CodecError>(result)) {
    return false;
  }
  const auto& error = std::get<oxq::core::CodecError>(result);
  return error.code == oxq::core::CodecErrorCode::invalid_magic && error.offset == offset;
}

}  // namespace

int main() {
  using oxq::core::detail::checked_add;
  using oxq::core::detail::checked_multiply;

  std::size_t output = 0;
  if (!checked_add(2, 3, output) || output != 5 ||
      checked_add(static_cast<std::size_t>(-1), 1, output) ||
      !checked_multiply(6, 7, output) || output != 42 ||
      checked_multiply(static_cast<std::size_t>(-1), 2, output)) {
    return 1;
  }

  const std::string check = "123456789";
  const auto bytes = std::as_bytes(std::span{check.data(), check.size()});
  if (oxq::core::detail::crc32c(bytes) != 0xe3069283U) {
    return 2;
  }

  const std::filesystem::path vectors{OXQF_VECTOR_DIRECTORY};
  if (!valid(vectors / "minimal.oxq", 5) ||
      !valid(vectors / "variation-zh.oxq", 5) ||
      !valid(vectors / "unknown-noncritical.oxq", 6)) {
    return 3;
  }
  if (!invalid_magic(vectors / "invalid-magic-high-bit.oxq", 0) ||
      !invalid_magic(vectors / "invalid-magic-crlf.oxq", 4) ||
      !invalid_magic(vectors / "invalid-magic-eof.oxq", 6)) {
    return 4;
  }
  return 0;
}
