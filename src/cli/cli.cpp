#include "cli.hpp"

#include "json.hpp"
#include "names.hpp"

#include <oxq/convert/cbl_reader.hpp>
#include <oxq/convert/cbl_writer.hpp>
#include <oxq/core/codec_error.hpp>
#include <oxq/core/product.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/validator.hpp>
#include <oxq/core/writer.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace oxq::cli {
namespace {

namespace fs = std::filesystem;

enum class Format { oxq, cbl, unknown };

struct FileError {
  std::string message;
};

using FileOutcome = std::variant<std::vector<std::byte>, FileError>;

struct ConvertArguments {
  bool strict{};
  bool json{};
  fs::path output;
  Format input_format{Format::unknown};
  Format output_format{Format::unknown};
  std::vector<fs::path> inputs;
};

struct Summary {
  std::size_t succeeded{};
  std::size_t skipped{};
  std::size_t warnings{};
  std::size_t losses{};
  std::size_t failed{};
};

[[nodiscard]] int exit_code(ExitCode code) noexcept {
  return static_cast<int>(code);
}

void print_usage(std::ostream& output) {
  output << "Usage: oxq <command> [options]\n"
            "Commands:\n"
            "  convert   Convert CBL libraries to OXQ files or OXQ files to CBL\n"
            "  inspect   Show an OXQ or CBL summary\n"
            "  validate  Validate an OXQ file and its replay state\n"
            "  dump      Write canonical GameModel JSON\n"
            "  help      Show command help\n"
            "Global options:\n"
            "  --help    Show this help\n"
            "  --version Show the program version\n";
}

void print_command_help(std::string_view command, std::ostream& output) {
  if (command == "convert") {
    output << "Usage: oxq convert [--strict] [--json] [--input-format cbl|oxq] "
              "[--output-format oxq|cbl] --output PATH INPUT...\n";
  } else if (command == "inspect") {
    output << "Usage: oxq inspect [--json] INPUT\n";
  } else if (command == "validate") {
    output << "Usage: oxq validate [--json] INPUT.oxq\n";
  } else if (command == "dump") {
    output << "Usage: oxq dump INPUT\n";
  } else {
    print_usage(output);
  }
}

[[nodiscard]] bool known_command(std::string_view command) noexcept {
  return command == "convert" || command == "inspect" ||
         command == "validate" || command == "dump";
}

[[nodiscard]] Format parse_format(std::string_view value) noexcept {
  if (value == "oxq") {
    return Format::oxq;
  }
  if (value == "cbl") {
    return Format::cbl;
  }
  return Format::unknown;
}

[[nodiscard]] Format detect_format(const fs::path& path) {
  auto extension = path.extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](const unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                         });
  return parse_format(extension == ".oxq"   ? "oxq"
                      : extension == ".cbl" ? "cbl"
                                             : "");
}

[[nodiscard]] FileOutcome read_file(const fs::path& path) {
  std::error_code filesystem_error;
  const auto size = fs::file_size(path, filesystem_error);
  if (filesystem_error) {
    return FileError{"cannot stat input: " + filesystem_error.message()};
  }
  constexpr std::uintmax_t maximum = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  if (size > maximum) {
    return FileError{"input exceeds the CLI 2 GiB file limit"};
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return FileError{"cannot open input"};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  if (!input) {
    return FileError{"cannot read complete input"};
  }
  return bytes;
}

enum class WriteStatus { written, exists, failed };

[[nodiscard]] WriteStatus write_atomic(const fs::path& path,
                                       std::span<const std::byte> bytes,
                                       std::string& message) {
  std::error_code filesystem_error;
  const bool target_exists = fs::exists(path, filesystem_error);
  if (filesystem_error) {
    message = "cannot inspect output: " + filesystem_error.message();
    return WriteStatus::failed;
  }
  if (target_exists) {
    return WriteStatus::exists;
  }
  auto parent = path.parent_path();
  if (parent.empty()) {
    parent = ".";
  }
  fs::create_directories(parent, filesystem_error);
  if (filesystem_error) {
    message = "cannot create output directory: " + filesystem_error.message();
    return WriteStatus::failed;
  }
  fs::path temporary;
  for (unsigned suffix = 0; suffix < 1000; ++suffix) {
    temporary = path;
    temporary += ".tmp." + std::to_string(suffix);
    const bool temporary_exists = fs::exists(temporary, filesystem_error);
    if (filesystem_error) {
      message = "cannot inspect temporary output: " +
                filesystem_error.message();
      return WriteStatus::failed;
    }
    if (!temporary_exists) {
      break;
    }
    if (suffix == 999) {
      message = "cannot reserve a sibling temporary output";
      return WriteStatus::failed;
    }
  }
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      message = "cannot create temporary output";
      return WriteStatus::failed;
    }
    if (!bytes.empty()) {
      output.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    output.flush();
    if (!output) {
      output.close();
      fs::remove(temporary, filesystem_error);
      message = "cannot write complete temporary output";
      return WriteStatus::failed;
    }
  }
  fs::rename(temporary, path, filesystem_error);
  if (filesystem_error) {
    std::error_code cleanup_error;
    fs::remove(temporary, cleanup_error);
    message = "cannot publish output: " + filesystem_error.message();
    return WriteStatus::failed;
  }
  return WriteStatus::written;
}

void codec_error(const fs::path& path, const core::CodecError& issue,
                 std::ostream& error) {
  error << path.string() << ": codec error " << name(issue.code)
        << " at offset " << issue.offset;
  if (!issue.field.empty()) {
    error << " [" << issue.field << ']';
  }
  error << ": " << issue.message << '\n';
}

void cbl_error(const fs::path& path, const convert::CblError& issue,
               std::ostream& error) {
  error << path.string() << ": CBL error " << name(issue.code)
        << " at offset " << issue.offset;
  if (!issue.field.empty()) {
    error << " [" << issue.field << ']';
  }
  error << ": " << issue.message << '\n';
}

void conversion_diagnostics(const fs::path& path,
                            const convert::ConversionReport& report,
                            std::ostream& error) {
  for (const auto& diagnostic : report.diagnostics) {
    error << path.string() << ": "
          << (diagnostic.severity == convert::ConversionSeverity::loss
                  ? "loss"
                  : "warning")
          << " " << name(diagnostic.code);
    if (diagnostic.game_index.has_value()) {
      error << " game=" << *diagnostic.game_index;
    }
    if (diagnostic.node_index.has_value()) {
      error << " node=" << *diagnostic.node_index;
    }
    if (!diagnostic.field.empty()) {
      error << " [" << diagnostic.field << ']';
    }
    error << ": " << diagnostic.message << '\n';
  }
}

[[nodiscard]] std::optional<ConvertArguments> parse_convert(
    int argc, char* argv[], std::ostream& error) {
  ConvertArguments arguments;
  for (int index = 2; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--strict") {
      arguments.strict = true;
    } else if (argument == "--json") {
      arguments.json = true;
    } else if (argument == "--output" || argument == "--input-format" ||
               argument == "--output-format") {
      if (++index >= argc) {
        error << "oxq convert: missing value for " << argument << '\n';
        return std::nullopt;
      }
      if (argument == "--output") {
        arguments.output = fs::path{argv[index]};
      } else {
        const auto format = parse_format(argv[index]);
        if (format == Format::unknown) {
          error << "oxq convert: format must be cbl or oxq\n";
          return std::nullopt;
        }
        (argument == "--input-format" ? arguments.input_format
                                      : arguments.output_format) = format;
      }
    } else if (argument.starts_with("--")) {
      error << "oxq convert: unknown option " << argument << '\n';
      return std::nullopt;
    } else {
      arguments.inputs.emplace_back(argv[index]);
    }
  }
  if (arguments.output.empty() || arguments.inputs.empty()) {
    error << "oxq convert: --output and at least one input are required\n";
    return std::nullopt;
  }
  if (arguments.input_format == Format::unknown) {
    arguments.input_format = detect_format(arguments.inputs.front());
  }
  if (arguments.input_format == Format::unknown) {
    error << "oxq convert: cannot infer input format\n";
    return std::nullopt;
  }
  for (const auto& input : arguments.inputs) {
    const auto detected = detect_format(input);
    if (detected != Format::unknown && detected != arguments.input_format) {
      error << "oxq convert: mixed input formats are not supported\n";
      return std::nullopt;
    }
  }
  const auto required_output = arguments.input_format == Format::cbl
                                   ? Format::oxq
                                   : Format::cbl;
  if (arguments.output_format == Format::unknown) {
    arguments.output_format = required_output;
  }
  if (arguments.output_format != required_output) {
    error << "oxq convert: only CBL-to-OXQ and OXQ-to-CBL are supported\n";
    return std::nullopt;
  }
  return arguments;
}

[[nodiscard]] std::string summary_json(const Summary& summary) {
  return "{\"succeeded\":" + std::to_string(summary.succeeded) +
         ",\"skipped\":" + std::to_string(summary.skipped) +
         ",\"warnings\":" + std::to_string(summary.warnings) +
         ",\"losses\":" + std::to_string(summary.losses) +
         ",\"failed\":" + std::to_string(summary.failed) + '}';
}

void print_summary(const Summary& summary, bool json, std::ostream& output) {
  if (json) {
    output << summary_json(summary) << '\n';
  } else {
    output << "succeeded=" << summary.succeeded
           << " skipped=" << summary.skipped
           << " warnings=" << summary.warnings
           << " losses=" << summary.losses
           << " failed=" << summary.failed << '\n';
  }
}

void add_report(Summary& summary, const convert::ConversionReport& report) {
  for (const auto& diagnostic : report.diagnostics) {
    if (diagnostic.severity == convert::ConversionSeverity::loss) {
      ++summary.losses;
    } else {
      ++summary.warnings;
    }
  }
}

[[nodiscard]] int summary_exit(const Summary& summary, bool rejected,
                               std::size_t input_failures,
                               std::size_t output_failures) noexcept {
  if (summary.succeeded != 0 &&
      (summary.failed != 0 || summary.skipped != 0)) {
    return exit_code(ExitCode::partial_failure);
  }
  if (rejected) {
    return exit_code(ExitCode::conversion_rejected);
  }
  if (output_failures != 0 || summary.skipped != 0) {
    return exit_code(ExitCode::output_error);
  }
  if (input_failures != 0) {
    return exit_code(ExitCode::input_error);
  }
  return exit_code(ExitCode::success);
}

[[nodiscard]] int convert_cbl_to_oxq(const ConvertArguments& arguments,
                                     std::ostream& output,
                                     std::ostream& error) {
  Summary summary;
  bool rejected = false;
  std::size_t input_failures = 0;
  std::size_t output_failures = 0;
  std::error_code directory_error;
  fs::create_directories(arguments.output, directory_error);
  if (directory_error || !fs::is_directory(arguments.output, directory_error)) {
    error << arguments.output.string() << ": cannot create output directory";
    if (directory_error) {
      error << ": " << directory_error.message();
    }
    error << '\n';
    summary.failed = 1;
    print_summary(summary, arguments.json, output);
    return exit_code(ExitCode::output_error);
  }
  for (const auto& input_path : arguments.inputs) {
    auto file = read_file(input_path);
    if (const auto* issue = std::get_if<FileError>(&file)) {
      error << input_path.string() << ": " << issue->message << '\n';
      ++summary.failed;
      ++input_failures;
      continue;
    }
    convert::CblReadOptions options;
    options.mode = arguments.strict ? convert::ConversionMode::strict
                                    : convert::ConversionMode::lenient;
    auto read = convert::read_cbl(std::get<std::vector<std::byte>>(file), options);
    if (const auto* issue = std::get_if<convert::CblError>(&read)) {
      cbl_error(input_path, *issue, error);
      ++summary.failed;
      ++input_failures;
      continue;
    }
    auto& result = std::get<convert::CblReadResult>(read);
    add_report(summary, result.report);
    conversion_diagnostics(input_path, result.report, error);
    if (result.report.rejected) {
      rejected = true;
      ++summary.failed;
      continue;
    }
    for (const auto& game : result.games) {
      const auto target = arguments.output / (game.uuid.to_string() + ".oxq");
      auto written = core::write_oxq(game);
      if (const auto* issue = std::get_if<core::WriterError>(&written)) {
        error << input_path.string() << ": cannot encode game "
              << game.uuid.to_string() << ": " << issue->message << '\n';
        ++summary.failed;
        ++output_failures;
        continue;
      }
      std::string message;
      const auto status = write_atomic(
          target, std::get<std::vector<std::byte>>(written), message);
      if (status == WriteStatus::written) {
        ++summary.succeeded;
      } else if (status == WriteStatus::exists) {
        error << target.string() << ": target exists; skipped\n";
        ++summary.skipped;
      } else {
        error << target.string() << ": " << message << '\n';
        ++summary.failed;
        ++output_failures;
      }
    }
  }
  print_summary(summary, arguments.json, output);
  return summary_exit(summary, rejected, input_failures, output_failures);
}

[[nodiscard]] int convert_oxq_to_cbl(const ConvertArguments& arguments,
                                     std::ostream& output,
                                     std::ostream& error) {
  Summary summary;
  std::size_t input_failures = 0;
  std::size_t output_failures = 0;
  std::vector<core::GameModel> games;
  for (const auto& input_path : arguments.inputs) {
    auto file = read_file(input_path);
    if (const auto* issue = std::get_if<FileError>(&file)) {
      error << input_path.string() << ": " << issue->message << '\n';
      ++summary.failed;
      ++input_failures;
      continue;
    }
    auto read = core::read_oxq(std::get<std::vector<std::byte>>(file));
    if (const auto* issue = std::get_if<core::CodecError>(&read)) {
      codec_error(input_path, *issue, error);
      ++summary.failed;
      ++input_failures;
      continue;
    }
    games.push_back(std::move(std::get<core::ReaderResult>(read).game));
  }
  if (!games.empty()) {
    convert::CblWriteOptions options;
    options.mode = arguments.strict ? convert::ConversionMode::strict
                                    : convert::ConversionMode::lenient;
    options.library.name = arguments.output.stem().string();
    auto written = convert::write_cbl(games, options);
    if (const auto* issue = std::get_if<convert::CblWriteError>(&written)) {
      error << arguments.output.string() << ": CBL write error "
            << name(issue->code) << " [" << issue->field
            << "]: " << issue->message << '\n';
      ++summary.failed;
      ++output_failures;
    } else {
      auto& result = std::get<convert::CblWriteResult>(written);
      add_report(summary, result.report);
      conversion_diagnostics(arguments.output, result.report, error);
      if (result.report.rejected) {
        ++summary.failed;
        ++output_failures;
        print_summary(summary, arguments.json, output);
        return exit_code(ExitCode::conversion_rejected);
      }
      std::string message;
      const auto status = write_atomic(arguments.output, result.bytes, message);
      if (status == WriteStatus::written) {
        ++summary.succeeded;
      } else if (status == WriteStatus::exists) {
        error << arguments.output.string() << ": target exists; skipped\n";
        ++summary.skipped;
      } else {
        error << arguments.output.string() << ": " << message << '\n';
        ++summary.failed;
        ++output_failures;
      }
    }
  }
  print_summary(summary, arguments.json, output);
  return summary_exit(summary, false, input_failures, output_failures);
}

[[nodiscard]] int command_convert(int argc, char* argv[],
                                  std::ostream& output,
                                  std::ostream& error) {
  if (argc == 3 && std::string_view{argv[2]} == "--help") {
    print_command_help("convert", output);
    return exit_code(ExitCode::success);
  }
  const auto arguments = parse_convert(argc, argv, error);
  if (!arguments.has_value()) {
    print_command_help("convert", error);
    return exit_code(ExitCode::usage);
  }
  return arguments->input_format == Format::cbl
             ? convert_cbl_to_oxq(*arguments, output, error)
             : convert_oxq_to_cbl(*arguments, output, error);
}

struct SingleArguments {
  bool json{};
  fs::path input;
};

[[nodiscard]] std::optional<SingleArguments> parse_single(
    int argc, char* argv[], std::string_view command, bool allow_json,
    std::ostream& error) {
  SingleArguments result;
  for (int index = 2; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (allow_json && argument == "--json") {
      result.json = true;
    } else if (argument.starts_with("--")) {
      error << "oxq " << command << ": unknown option " << argument << '\n';
      return std::nullopt;
    } else if (!result.input.empty()) {
      error << "oxq " << command << ": exactly one input is required\n";
      return std::nullopt;
    } else {
      result.input = fs::path{argv[index]};
    }
  }
  if (result.input.empty()) {
    error << "oxq " << command << ": exactly one input is required\n";
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] std::size_t annotation_count(const core::GameModel& game) {
  std::size_t count = 0;
  for (const auto& node : game.move_tree.nodes) {
    count += node.annotations.size();
  }
  return count;
}

[[nodiscard]] std::optional<std::string> canonical_game_json(
    const core::GameModel& game) {
  auto written = core::write_oxq(game);
  if (!std::holds_alternative<std::vector<std::byte>>(written)) {
    return std::nullopt;
  }
  auto read = core::read_oxq(std::get<std::vector<std::byte>>(written));
  if (!std::holds_alternative<core::ReaderResult>(read)) {
    return std::nullopt;
  }
  return game_json(std::get<core::ReaderResult>(read).game);
}

[[nodiscard]] int command_inspect(int argc, char* argv[],
                                  std::ostream& output,
                                  std::ostream& error) {
  if (argc == 3 && std::string_view{argv[2]} == "--help") {
    print_command_help("inspect", output);
    return exit_code(ExitCode::success);
  }
  const auto arguments = parse_single(argc, argv, "inspect", true, error);
  if (!arguments.has_value()) {
    print_command_help("inspect", error);
    return exit_code(ExitCode::usage);
  }
  auto file = read_file(arguments->input);
  if (const auto* issue = std::get_if<FileError>(&file)) {
    error << arguments->input.string() << ": " << issue->message << '\n';
    return exit_code(ExitCode::input_error);
  }
  const auto format = detect_format(arguments->input);
  if (format == Format::oxq) {
    auto read = core::read_oxq(std::get<std::vector<std::byte>>(file));
    if (const auto* issue = std::get_if<core::CodecError>(&read)) {
      codec_error(arguments->input, *issue, error);
      return exit_code(ExitCode::input_error);
    }
    const auto& result = std::get<core::ReaderResult>(read);
    if (arguments->json) {
      output << "{\"format\":\"oxq\",\"file\":"
             << json_string(arguments->input.string()) << ",\"uuid\":"
             << json_string(result.game.uuid.to_string()) << ",\"title\":"
             << (result.game.metadata.title.has_value()
                     ? json_string(*result.game.metadata.title)
                     : "null")
             << ",\"pieces\":" << result.game.initial_position.pieces.size()
             << ",\"nodes\":" << result.game.move_tree.nodes.size()
             << ",\"annotations\":" << annotation_count(result.game)
             << ",\"reader\":"
             << reader_diagnostics_json(result.diagnostics) << "}\n";
    } else {
      output << "format=oxq uuid=" << result.game.uuid.to_string()
             << " pieces=" << result.game.initial_position.pieces.size()
             << " nodes=" << result.game.move_tree.nodes.size()
             << " annotations=" << annotation_count(result.game)
             << " canonical="
             << (result.diagnostics.canonical_ordering() ? "true" : "false")
             << '\n';
    }
    return exit_code(ExitCode::success);
  }
  if (format == Format::cbl) {
    auto read = convert::read_cbl(std::get<std::vector<std::byte>>(file));
    if (const auto* issue = std::get_if<convert::CblError>(&read)) {
      cbl_error(arguments->input, *issue, error);
      return exit_code(ExitCode::input_error);
    }
    const auto& result = std::get<convert::CblReadResult>(read);
    std::size_t nodes = 0;
    std::size_t annotations = 0;
    for (const auto& game : result.games) {
      nodes += game.move_tree.nodes.size();
      annotations += annotation_count(game);
    }
    if (arguments->json) {
      output << "{\"format\":\"cbl\",\"file\":"
             << json_string(arguments->input.string()) << ",\"library\":"
             << cbl_library_json(result.library) << ",\"games\":"
             << result.games.size() << ",\"nodes\":" << nodes
             << ",\"annotations\":" << annotations
             << ",\"report\":" << conversion_report_json(result.report)
             << "}\n";
    } else {
      output << "format=cbl uuid=" << result.library.uuid.to_string()
             << " games=" << result.games.size() << " nodes=" << nodes
             << " annotations=" << annotations
             << " warnings=" << result.report.diagnostics.size() << '\n';
    }
    conversion_diagnostics(arguments->input, result.report, error);
    return exit_code(ExitCode::success);
  }
  error << arguments->input.string()
        << ": cannot infer format; expected .oxq or .cbl\n";
  return exit_code(ExitCode::usage);
}

[[nodiscard]] int command_validate(int argc, char* argv[],
                                   std::ostream& output,
                                   std::ostream& error) {
  if (argc == 3 && std::string_view{argv[2]} == "--help") {
    print_command_help("validate", output);
    return exit_code(ExitCode::success);
  }
  const auto arguments = parse_single(argc, argv, "validate", true, error);
  if (!arguments.has_value()) {
    print_command_help("validate", error);
    return exit_code(ExitCode::usage);
  }
  if (detect_format(arguments->input) != Format::oxq) {
    error << "oxq validate: input must have an .oxq extension\n";
    return exit_code(ExitCode::usage);
  }
  auto file = read_file(arguments->input);
  if (const auto* issue = std::get_if<FileError>(&file)) {
    error << arguments->input.string() << ": " << issue->message << '\n';
    return exit_code(ExitCode::validation_error);
  }
  auto validation =
      core::validate_oxq_state(std::get<std::vector<std::byte>>(file));
  if (const auto* issue = std::get_if<core::CodecError>(&validation)) {
    codec_error(arguments->input, *issue, error);
    if (arguments->json) {
      output << "{\"valid\":false,\"codec_error\":{\"code\":"
             << json_string(name(issue->code)) << ",\"offset\":"
             << issue->offset << ",\"field\":" << json_string(issue->field)
             << ",\"message\":" << json_string(issue->message) << "}}\n";
    }
    return exit_code(ExitCode::validation_error);
  }
  const auto& result = std::get<core::StateValidatorResult>(validation);
  const bool valid = !core::has_errors(result.issues);
  if (arguments->json) {
    output << "{\"valid\":" << (valid ? "true" : "false")
           << ",\"reader\":"
           << reader_diagnostics_json(result.diagnostics)
           << ",\"issues\":" << validation_issues_json(result.issues)
           << "}\n";
  } else {
    output << "valid=" << (valid ? "true" : "false")
           << " canonical="
           << (result.diagnostics.canonical_ordering() ? "true" : "false")
           << " issues=" << result.issues.size() << '\n';
  }
  for (const auto& issue : result.issues) {
    error << arguments->input.string() << ": "
          << (issue.severity == core::ValidationSeverity::error ? "error"
                                                                : "warning")
          << " " << name(issue.code) << " [" << issue.path
          << "]: " << issue.message << '\n';
  }
  return valid ? exit_code(ExitCode::success)
               : exit_code(ExitCode::validation_error);
}

[[nodiscard]] int command_dump(int argc, char* argv[], std::ostream& output,
                               std::ostream& error) {
  if (argc == 3 && std::string_view{argv[2]} == "--help") {
    print_command_help("dump", output);
    return exit_code(ExitCode::success);
  }
  const auto arguments = parse_single(argc, argv, "dump", false, error);
  if (!arguments.has_value()) {
    print_command_help("dump", error);
    return exit_code(ExitCode::usage);
  }
  auto file = read_file(arguments->input);
  if (const auto* issue = std::get_if<FileError>(&file)) {
    error << arguments->input.string() << ": " << issue->message << '\n';
    return exit_code(ExitCode::input_error);
  }
  const auto format = detect_format(arguments->input);
  if (format == Format::oxq) {
    auto read = core::read_oxq(std::get<std::vector<std::byte>>(file));
    if (const auto* issue = std::get_if<core::CodecError>(&read)) {
      codec_error(arguments->input, *issue, error);
      return exit_code(ExitCode::input_error);
    }
    const auto json =
        canonical_game_json(std::get<core::ReaderResult>(read).game);
    if (!json.has_value()) {
      error << arguments->input.string()
            << ": cannot normalize decoded GameModel\n";
      return exit_code(ExitCode::input_error);
    }
    output << *json << '\n';
    return exit_code(ExitCode::success);
  }
  if (format == Format::cbl) {
    auto read = convert::read_cbl(std::get<std::vector<std::byte>>(file));
    if (const auto* issue = std::get_if<convert::CblError>(&read)) {
      cbl_error(arguments->input, *issue, error);
      return exit_code(ExitCode::input_error);
    }
    const auto& result = std::get<convert::CblReadResult>(read);
    conversion_diagnostics(arguments->input, result.report, error);
    output << '[';
    bool first = true;
    for (const auto& game : result.games) {
      const auto json = canonical_game_json(game);
      if (!json.has_value()) {
        error << arguments->input.string()
              << ": cannot normalize decoded GameModel\n";
        return exit_code(ExitCode::input_error);
      }
      if (!first) {
        output << ',';
      }
      first = false;
      output << *json;
    }
    output << "]\n";
    return exit_code(ExitCode::success);
  }
  error << arguments->input.string()
        << ": cannot infer format; expected .oxq or .cbl\n";
  return exit_code(ExitCode::usage);
}

}  // namespace

int run(int argc, char* argv[], std::ostream& output, std::ostream& error) {
  if (argc == 1) {
    print_usage(output);
    return exit_code(ExitCode::success);
  }
  const std::string_view command{argv[1]};
  if (command == "--help") {
    if (argc != 2) {
      error << "oxq --help: no arguments are accepted\n";
      return exit_code(ExitCode::usage);
    }
    print_usage(output);
    return exit_code(ExitCode::success);
  }
  if (command == "help") {
    if (argc == 3 && known_command(argv[2])) {
      print_command_help(argv[2], output);
      return exit_code(ExitCode::success);
    }
    if (argc != 2) {
      error << "oxq help: expected one of convert, inspect, validate, or dump\n";
      return exit_code(ExitCode::usage);
    }
    print_usage(output);
    return exit_code(ExitCode::success);
  }
  if (command == "--version" && argc == 2) {
    output << "oxq " << core::product_version() << '\n';
    return exit_code(ExitCode::success);
  }
  if (command == "convert") {
    return command_convert(argc, argv, output, error);
  }
  if (command == "inspect") {
    return command_inspect(argc, argv, output, error);
  }
  if (command == "validate") {
    return command_validate(argc, argv, output, error);
  }
  if (command == "dump") {
    return command_dump(argc, argv, output, error);
  }
  error << "oxq: unknown command " << command << '\n';
  print_usage(error);
  return exit_code(ExitCode::usage);
}

}  // namespace oxq::cli
