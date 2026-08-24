#include <oxq/core/reader.hpp>
#include <oxq/core/state_validation.hpp>
#include <oxq/core/writer.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span{data, size});
  oxq::core::ReaderLimits limits;
  limits.max_file_size = 16U * 1024U * 1024U;
  limits.max_strings = 100'000;
  limits.max_total_string_bytes = 8U * 1024U * 1024U;
  limits.max_metadata_fields = 10'000;
  limits.max_nodes = 100'000;
  limits.max_annotations = 100'000;
  limits.max_tree_depth = 100'000;

  auto read = oxq::core::read_oxq(bytes, limits);
  if (!std::holds_alternative<oxq::core::ReaderResult>(read)) {
    return 0;
  }
  const auto& game = std::get<oxq::core::ReaderResult>(read).game;
  static_cast<void>(oxq::core::validate_state(game));
  auto written = oxq::core::write_oxq(game);
  if (std::holds_alternative<std::vector<std::byte>>(written)) {
    static_cast<void>(oxq::core::read_oxq(
        std::get<std::vector<std::byte>>(written), limits));
  }
  return 0;
}
