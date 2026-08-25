#include <oxq/convert/cbl_reader.hpp>

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
  for (unsigned index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

[[nodiscard]] bool error_is(const oxq::convert::CblInspectOutcome& outcome,
                            oxq::convert::CblErrorCode code,
                            std::size_t offset) {
  return std::holds_alternative<oxq::convert::CblError>(outcome) &&
         std::get<oxq::convert::CblError>(outcome).code == code &&
         std::get<oxq::convert::CblError>(outcome).offset == offset;
}

}  // namespace

int main() {
  const std::filesystem::path baseline{OXQF_GOLD_BASELINE_DIRECTORY};
  struct Expected {
    std::string_view name;
    std::size_t games;
    std::size_t blocks;
  };
  constexpr Expected expected[] = {
      {"cbl_00_empty.CBL", 0, 0},
      {"cbl_01_game_empty.CBL", 1, 1},
      {"cbl_02_one_ply.CBL", 1, 1},
      {"cbl_03_two_plies.CBL", 1, 1},
      {"cbl_04_mainline.CBL", 1, 1},
      {"cbl_05_variation.CBL", 1, 1},
      {"cbl_06_nested_variation.CBL", 1, 1},
      {"cbl_07_comments.CBL", 1, 1},
      {"cbl_08_metadata.CBL", 1, 1},
      {"cbl_09_custom_position.CBL", 1, 1},
      {"cbl_10_two_games.CBL", 2, 2},
      {"cbl_11_nested_folders.CBL", 2, 2},
  };
  for (const auto& item : expected) {
    const auto outcome = oxq::convert::inspect_cbl(read_file(baseline / item.name));
    if (!std::holds_alternative<oxq::convert::CblLibraryInfo>(outcome)) {
      return 1;
    }
    const auto& info = std::get<oxq::convert::CblLibraryInfo>(outcome);
    if (info.directory_capacity != 128 || info.live_game_count != item.games ||
        info.allocated_resource_count != item.games || info.total_blocks != item.blocks ||
        info.deleted_game_count != 0 || info.live_non_game_count != 0 ||
        info.trailing_bytes != 0 || info.uuid.is_nil() || info.name.empty()) {
      return 2;
    }
  }

  const auto empty = read_file(baseline / "cbl_00_empty.CBL");
  const auto empty_outcome = oxq::convert::inspect_cbl(empty);
  if (!std::holds_alternative<oxq::convert::CblLibraryInfo>(empty_outcome) ||
      std::get<oxq::convert::CblLibraryInfo>(empty_outcome).name != "cbl_00_empty" ||
      std::get<oxq::convert::CblLibraryInfo>(empty_outcome).author.empty() ||
      std::get<oxq::convert::CblLibraryInfo>(empty_outcome).author_email.empty() ||
      std::get<oxq::convert::CblLibraryInfo>(empty_outcome).created_at.empty() ||
      std::get<oxq::convert::CblLibraryInfo>(empty_outcome).modified_at.empty()) {
    return 3;
  }

  auto invalid_magic = empty;
  invalid_magic[2] = std::byte{0};
  if (!error_is(oxq::convert::inspect_cbl(invalid_magic),
                oxq::convert::CblErrorCode::invalid_magic, 2)) {
    return 4;
  }
  if (!error_is(oxq::convert::inspect_cbl(std::span<const std::byte>{empty}.first(0x1043f)),
                oxq::convert::CblErrorCode::truncated_input, 0x1043f)) {
    return 5;
  }

  auto invalid_used_size = read_file(baseline / "cbl_01_game_empty.CBL");
  write_u32(invalid_used_size, 0x10440 + 12, 4097);
  if (!error_is(oxq::convert::inspect_cbl(invalid_used_size),
                oxq::convert::CblErrorCode::invalid_directory, 0x10440 + 12)) {
    return 6;
  }

  oxq::convert::CblReaderLimits limits;
  limits.max_total_blocks = 0;
  if (!error_is(oxq::convert::inspect_cbl(
                    read_file(baseline / "cbl_01_game_empty.CBL"), limits),
                oxq::convert::CblErrorCode::resource_limit, 0x10440 + 8)) {
    return 7;
  }
  return 0;
}
