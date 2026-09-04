#include "cli.hpp"

#include <oxq/core/reader.hpp>
#include <oxq/core/writer.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Invocation {
  int status{};
  std::string output;
  std::string error;
};

[[nodiscard]] Invocation invoke(std::initializer_list<std::string_view> values) {
  std::vector<std::string> arguments;
  arguments.reserve(values.size());
  for (const auto value : values) {
    arguments.emplace_back(value);
  }
  std::vector<char*> pointers;
  pointers.reserve(arguments.size());
  for (auto& argument : arguments) {
    pointers.push_back(argument.data());
  }
  std::ostringstream output;
  std::ostringstream error;
  const auto status = oxq::cli::run(static_cast<int>(pointers.size()),
                                    pointers.data(), output, error);
  return {status, output.str(), error.str()};
}

[[nodiscard]] std::size_t regular_file_count(const fs::path& directory) {
  std::size_t result = 0;
  for (const auto& entry : fs::directory_iterator{directory}) {
    if (entry.is_regular_file()) {
      ++result;
    }
  }
  return result;
}

[[nodiscard]] bool contains(const std::string& text,
                            std::string_view expected) noexcept {
  return text.find(expected) != std::string::npos;
}

[[nodiscard]] std::vector<std::byte> read_bytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  const std::vector<char> characters{std::istreambuf_iterator<char>{input},
                                     std::istreambuf_iterator<char>{}};
  std::vector<std::byte> result;
  result.reserve(characters.size());
  for (const auto character : characters) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return result;
}

[[nodiscard]] bool write_bytes(const fs::path& path,
                               const std::vector<std::byte>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

[[nodiscard]] bool write_text(const fs::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
  return static_cast<bool>(output);
}

}  // namespace

int main() {
  const fs::path vectors{OXQF_VECTOR_DIRECTORY};
  const fs::path gold{OXQF_GOLD_BASELINE_DIRECTORY};
  const fs::path work{OXQF_CLI_TEST_DIRECTORY};
  std::error_code filesystem_error;
  fs::remove_all(work, filesystem_error);
  if (filesystem_error || !fs::create_directories(work, filesystem_error) ||
      filesystem_error) {
    return 1;
  }

  const auto help = invoke({"oxq", "--help"});
  if (help.status != 0 || !help.error.empty() ||
      !contains(help.output, "convert") || !contains(help.output, "dump")) {
    return 2;
  }
  for (const auto command : {"convert", "inspect", "validate", "dump"}) {
    const auto command_help = invoke({"oxq", command, "--help"});
    if (command_help.status != 0 || !command_help.error.empty() ||
        !command_help.output.starts_with("Usage: oxq ")) {
      return 3;
    }
  }
  const auto usage = invoke({"oxq", "unknown"});
  if (usage.status != 2 || !usage.output.empty() ||
      !contains(usage.error, "unknown command")) {
    return 4;
  }

  const auto variation = (vectors / "variation-zh.oxq").string();
  const auto inspect = invoke({"oxq", "inspect", "--json", variation});
  if (inspect.status != 0 || !inspect.error.empty() ||
      !contains(inspect.output, "\"format\":\"oxq\"") ||
      !contains(inspect.output, "\"nodes\":4") ||
      !inspect.output.ends_with("}\n")) {
    return 5;
  }
  const auto validate = invoke({"oxq", "validate", "--json", variation});
  if (validate.status != 0 || !validate.error.empty() ||
      validate.output !=
          "{\"valid\":true,\"reader\":{\"canonical\":true,"
          "\"skipped_unknown_sections\":0,"
          "\"skipped_unknown_metadata_fields\":0,"
          "\"skipped_unknown_metadata_value_types\":0},\"issues\":[]}\n") {
    return 6;
  }
  const auto invalid =
      (vectors / "invalid-magic-eof.oxq").string();
  const auto invalid_validation =
      invoke({"oxq", "validate", "--json", invalid});
  if (invalid_validation.status != 5 ||
      !contains(invalid_validation.output, "\"valid\":false") ||
      !contains(invalid_validation.output, "\"offset\":6") ||
      !contains(invalid_validation.error, "offset 6")) {
    return 7;
  }
  auto state_source =
      oxq::core::read_oxq(read_bytes(vectors / "variation-zh.oxq"));
  if (!std::holds_alternative<oxq::core::ReaderResult>(state_source)) {
    return 8;
  }
  auto state_game =
      std::get<oxq::core::ReaderResult>(std::move(state_source)).game;
  state_game.move_tree.nodes[1].move->from_square = 50;
  auto state_written = oxq::core::write_oxq(state_game);
  const auto state_path = work / "invalid-state.oxq";
  if (!std::holds_alternative<std::vector<std::byte>>(state_written) ||
      !write_bytes(state_path,
                   std::get<std::vector<std::byte>>(state_written))) {
    return 9;
  }
  const auto invalid_state =
      invoke({"oxq", "validate", "--json", state_path.string()});
  if (invalid_state.status != 5 ||
      !contains(invalid_state.output, "\"code\":\"missing_source_piece\"") ||
      !contains(invalid_state.error, "missing_source_piece")) {
    return 10;
  }
  const auto dump = invoke(
      {"oxq", "dump", (vectors / "minimal.oxq").string()});
  if (dump.status != 0 || !dump.error.empty() ||
      !dump.output.starts_with(
          "{\"uuid\":\"01980000-0000-7000-8000-000000000001\"") ||
      !contains(dump.output, "\"initial_position\"") ||
      !contains(dump.output, "\"move_tree\"") || !dump.output.ends_with("}\n")) {
    return 11;
  }

  const auto two_games = (gold / "cbl_10_two_games.CBL").string();
  const auto cbl_inspect = invoke({"oxq", "inspect", "--json", two_games});
  if (cbl_inspect.status != 0 || !cbl_inspect.error.empty() ||
      !contains(cbl_inspect.output, "\"format\":\"cbl\"") ||
      !contains(cbl_inspect.output, "\"games\":2")) {
    return 12;
  }

  const auto output_directory = (work / "from-cbl").string();
  const auto converted = invoke(
      {"oxq", "convert", "--json", "--output", output_directory, two_games});
  if (converted.status != 0 ||
      converted.output !=
          "{\"succeeded\":2,\"skipped\":0,\"warnings\":0,\"losses\":0,"
          "\"failed\":0}\n" ||
      !converted.error.empty() || regular_file_count(output_directory) != 2) {
    return 13;
  }
  const auto repeated = invoke(
      {"oxq", "convert", "--json", "--output", output_directory, two_games});
  if (repeated.status != 4 ||
      repeated.output !=
          "{\"succeeded\":0,\"skipped\":2,\"warnings\":0,\"losses\":0,"
          "\"failed\":0}\n" ||
      !contains(repeated.error, "target exists; skipped")) {
    return 14;
  }

  const auto partial_directory = (work / "partial").string();
  const auto partial = invoke(
      {"oxq", "convert", "--json", "--output", partial_directory,
       two_games, (work / "missing.CBL").string()});
  if (partial.status != 7 ||
      partial.output !=
          "{\"succeeded\":2,\"skipped\":0,\"warnings\":0,\"losses\":0,"
          "\"failed\":1}\n" ||
      !contains(partial.error, "cannot stat input") ||
      regular_file_count(partial_directory) != 2) {
    return 15;
  }

  std::vector<std::string> generated;
  for (const auto& entry : fs::directory_iterator{output_directory}) {
    generated.push_back(entry.path().string());
  }
  std::ranges::sort(generated);
  const auto combined = (work / "combined.CBL").string();
  const auto merged = invoke({"oxq", "convert", "--json", "--output",
                              combined, generated[0], generated[1]});
  if (merged.status != 0 || !fs::is_regular_file(combined) ||
      !contains(merged.output, "\"succeeded\":1") ||
      !contains(merged.output, "\"warnings\":1") ||
      !contains(merged.output, "\"losses\":20")) {
    return 16;
  }
  const auto merged_inspect = invoke({"oxq", "inspect", "--json", combined});
  if (merged_inspect.status != 0 ||
      !contains(merged_inspect.output, "\"games\":2")) {
    return 17;
  }
  const auto strict_target = (work / "strict.CBL").string();
  const auto strict = invoke({"oxq", "convert", "--strict", "--json",
                              "--output", strict_target, generated[0],
                              generated[1]});
  if (strict.status != 6 || fs::exists(strict_target) ||
      !contains(strict.output, "\"failed\":1") ||
      !contains(strict.error, "loss")) {
    return 18;
  }

  const auto mixed_target = (work / "mixed.CBL").string();
  const auto mixed = invoke(
      {"oxq", "convert", "--json", "--output", mixed_target,
       generated[0], invalid});
  if (mixed.status != 7 || !fs::is_regular_file(mixed_target) ||
      !contains(mixed.output, "\"succeeded\":1") ||
      !contains(mixed.output, "\"failed\":1")) {
    return 19;
  }

  const auto mixed_formats = invoke(
      {"oxq", "convert", "--output", (work / "bad").string(), two_games,
       variation});
  if (mixed_formats.status != 2 ||
      !contains(mixed_formats.error, "mixed input formats")) {
    return 20;
  }
  const auto blocker = work / "not-a-directory";
  {
    std::ofstream stream(blocker);
    stream << "block";
  }
  const auto output_failure = invoke(
      {"oxq", "convert", "--json", "--output", blocker.string(), two_games});
  if (output_failure.status != 4 ||
      !contains(output_failure.output, "\"failed\":1") ||
      !contains(output_failure.error, "cannot create output directory")) {
    return 21;
  }

  const auto extensionless = work / "extensionless-input";
  fs::copy_file(vectors / "minimal.oxq", extensionless, filesystem_error);
  if (filesystem_error) {
    return 22;
  }
  const auto explicit_target = (work / "explicit.CBL").string();
  const auto explicit_formats = invoke(
      {"oxq", "convert", "--input-format", "oxq", "--output-format", "cbl",
       "--output", explicit_target, extensionless.string()});
  if (explicit_formats.status != 0 || !fs::is_regular_file(explicit_target)) {
    return 23;
  }

  const auto json_directory = work / "json";
  if (!fs::create_directories(json_directory, filesystem_error) ||
      filesystem_error ||
      !write_text(json_directory / "inspect-oxq.json", inspect.output) ||
      !write_text(json_directory / "validate-valid.json", validate.output) ||
      !write_text(json_directory / "validate-invalid.json",
                  invalid_validation.output) ||
      !write_text(json_directory / "dump.json", dump.output) ||
      !write_text(json_directory / "inspect-cbl.json", cbl_inspect.output) ||
      !write_text(json_directory / "convert-summary.json", converted.output)) {
    return 24;
  }
  return 0;
}
