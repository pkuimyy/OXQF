#include <oxq/core/codec_error.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/validator.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
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

[[nodiscard]] bool rejected(std::span<const std::byte> input) {
  return std::holds_alternative<oxq::core::CodecError>(oxq::core::validate_oxq(input));
}

}  // namespace

int main() {
  const std::filesystem::path vectors{OXQF_VECTOR_DIRECTORY};
  for (const auto* name : {"minimal.oxq", "variation-zh.oxq"}) {
    const auto valid = read_file(vectors / name);
    if (!std::holds_alternative<oxq::core::ReaderDiagnostics>(
            oxq::core::validate_oxq(valid))) {
      return 1;
    }
    for (std::size_t size = 0; size < valid.size(); ++size) {
      if (!rejected(std::span<const std::byte>{valid}.first(size))) {
        return 2;
      }
    }
    for (std::size_t offset = 0; offset < valid.size(); ++offset) {
      for (unsigned bit = 0; bit < 8; ++bit) {
        auto mutated = valid;
        mutated[offset] ^= static_cast<std::byte>(1U << bit);
        if (!rejected(mutated)) {
          return 3;
        }
      }
    }
  }

  std::uint32_t state = 0x6f787131U;
  for (std::size_t iteration = 0; iteration < 1000; ++iteration) {
    state = state * 1664525U + 1013904223U;
    std::vector<std::byte> input(state % 1025U);
    for (auto& byte : input) {
      state = state * 1664525U + 1013904223U;
      byte = static_cast<std::byte>(state >> 24U);
    }
    static_cast<void>(oxq::core::validate_oxq(input));
  }
  return 0;
}
