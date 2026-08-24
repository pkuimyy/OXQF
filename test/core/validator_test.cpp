#include <oxq/core/codec_error.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/validator.hpp>

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
  const auto valid = oxq::core::validate_oxq(read_file(vectors / "variation-zh.oxq"));
  if (!std::holds_alternative<oxq::core::ReaderDiagnostics>(valid) ||
      !std::get<oxq::core::ReaderDiagnostics>(valid).canonical_ordering()) {
    return 1;
  }

  const auto unknown =
      oxq::core::validate_oxq(read_file(vectors / "unknown-noncritical.oxq"));
  if (!std::holds_alternative<oxq::core::ReaderDiagnostics>(unknown) ||
      std::get<oxq::core::ReaderDiagnostics>(unknown).skipped_unknown_sections != 1) {
    return 2;
  }

  const auto invalid =
      oxq::core::validate_oxq(read_file(vectors / "invalid-magic-eof.oxq"));
  if (!std::holds_alternative<oxq::core::CodecError>(invalid) ||
      std::get<oxq::core::CodecError>(invalid).code !=
          oxq::core::CodecErrorCode::invalid_magic ||
      std::get<oxq::core::CodecError>(invalid).offset != 6) {
    return 3;
  }

  const auto state =
      oxq::core::validate_oxq_state(read_file(vectors / "variation-zh.oxq"));
  if (!std::holds_alternative<oxq::core::StateValidatorResult>(state) ||
      oxq::core::has_errors(std::get<oxq::core::StateValidatorResult>(state).issues)) {
    return 4;
  }
  return 0;
}
