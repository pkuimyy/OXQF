#include <oxq/editor/console.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const auto input = std::string_view{reinterpret_cast<const char*>(data), size};
  static_cast<void>(oxq::editor::parse_console(input));
  return 0;
}
