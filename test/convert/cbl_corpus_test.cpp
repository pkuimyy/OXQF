#include <oxq/convert/cbl_reader.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct CorpusStats {
  std::uint64_t files{};
  std::uint64_t bytes{};
  std::uint64_t directory_capacity{};
  std::uint64_t allocated_resources{};
  std::uint64_t live_games{};
  std::uint64_t deleted_games{};
  std::uint64_t live_non_games{};
  std::uint64_t converted_games{};
  std::uint64_t move_nodes{};
  std::uint64_t annotations{};
  std::uint64_t warnings{};
  std::uint64_t losses{};
};

[[nodiscard]] bool is_cbl(const fs::path& path) {
  auto extension = path.extension().string();
  std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return extension == ".cbl";
}

[[nodiscard]] bool read_file(const fs::path& path, std::vector<std::byte>& bytes) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return false;
  }
  const auto size = input.tellg();
  if (size < 0) {
    return false;
  }
  bytes.resize(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  return static_cast<bool>(input);
}

void print_stats(const CorpusStats& stats) {
  std::cout << "files=" << stats.files << '\n'
            << "bytes=" << stats.bytes << '\n'
            << "directory_capacity=" << stats.directory_capacity << '\n'
            << "allocated_resources=" << stats.allocated_resources << '\n'
            << "live_games=" << stats.live_games << '\n'
            << "deleted_games=" << stats.deleted_games << '\n'
            << "live_non_games=" << stats.live_non_games << '\n'
            << "converted_games=" << stats.converted_games << '\n'
            << "move_nodes=" << stats.move_nodes << '\n'
            << "annotations=" << stats.annotations << '\n'
            << "warnings=" << stats.warnings << '\n'
            << "losses=" << stats.losses << '\n';
}

[[nodiscard]] bool check_release_baseline(const CorpusStats& stats) {
  struct Expected {
    std::string_view name;
    std::uint64_t actual;
    std::uint64_t expected;
  };
  const Expected expected[] = {
      {"files", stats.files, 1'570},
      {"bytes", stats.bytes, 1'611'668'576},
      {"directory_capacity", stats.directory_capacity, 567'960},
      {"allocated_resources", stats.allocated_resources, 322'488},
      {"live_games", stats.live_games, 322'418},
      {"deleted_games", stats.deleted_games, 65},
      {"live_non_games", stats.live_non_games, 5},
      {"converted_games", stats.converted_games, 322'418},
      {"move_nodes", stats.move_nodes, 50'361'992},
      {"annotations", stats.annotations, 246'060},
      {"warnings", stats.warnings, 283'346},
      {"losses", stats.losses, 165},
  };
  bool matches = true;
  for (const auto& item : expected) {
    if (item.actual != item.expected) {
      std::cerr << item.name << ": expected " << item.expected << ", got " << item.actual
                << '\n';
      matches = false;
    }
  }
  return matches;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: oxq_convert_cbl_corpus_test <directory> [--release-baseline]\n";
    return 2;
  }
  const bool release_baseline = argc == 3 && std::string_view(argv[2]) == "--release-baseline";
  if (argc == 3 && !release_baseline) {
    std::cerr << "unknown option: " << argv[2] << '\n';
    return 2;
  }

  std::error_code error;
  std::vector<fs::path> paths;
  for (fs::recursive_directory_iterator iterator(argv[1], error), end; iterator != end;
       iterator.increment(error)) {
    if (error) {
      std::cerr << "cannot enumerate corpus: " << error.message() << '\n';
      return 1;
    }
    if (iterator->is_regular_file(error) && !error && is_cbl(iterator->path())) {
      paths.push_back(iterator->path());
    }
  }
  if (error) {
    std::cerr << "cannot enumerate corpus: " << error.message() << '\n';
    return 1;
  }
  std::ranges::sort(paths);

  CorpusStats stats;
  std::vector<std::byte> bytes;
  for (const auto& path : paths) {
    if (!read_file(path, bytes)) {
      std::cerr << "cannot read: " << path << '\n';
      return 1;
    }
    stats.files += 1;
    stats.bytes += bytes.size();

    auto outcome = oxq::convert::read_cbl(std::span<const std::byte>(bytes));
    if (const auto* cbl_error = std::get_if<oxq::convert::CblError>(&outcome)) {
      std::cerr << "CBL error in " << path << ": code=" << static_cast<int>(cbl_error->code)
                << " offset=" << cbl_error->offset << " field=" << cbl_error->field
                << " message=" << cbl_error->message << '\n';
      return 1;
    }
    auto& result = std::get<oxq::convert::CblReadResult>(outcome);
    if (result.report.rejected || result.games.size() != result.library.live_game_count) {
      std::cerr << "incomplete conversion in " << path << '\n';
      return 1;
    }
    stats.directory_capacity += result.library.directory_capacity;
    stats.allocated_resources += result.library.allocated_resource_count;
    stats.live_games += result.library.live_game_count;
    stats.deleted_games += result.library.deleted_game_count;
    stats.live_non_games += result.library.live_non_game_count;
    stats.converted_games += result.games.size();
    for (const auto& game : result.games) {
      stats.move_nodes += game.move_tree.nodes.size() - 1U;
      for (const auto& node : game.move_tree.nodes) {
        stats.annotations += node.annotations.size();
      }
    }
    for (const auto& diagnostic : result.report.diagnostics) {
      if (diagnostic.severity == oxq::convert::ConversionSeverity::warning) {
        stats.warnings += 1;
      } else {
        stats.losses += 1;
      }
    }
    if (stats.files % 100U == 0U) {
      std::cerr << "processed " << stats.files << '/' << paths.size() << " files\n";
    }
  }

  print_stats(stats);
  return release_baseline && !check_release_baseline(stats) ? 1 : 0;
}
