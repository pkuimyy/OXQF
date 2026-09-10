#include <oxq/editor/console.hpp>
#include <oxq/editor/editor_session.hpp>
#include <oxq/format/reader.hpp>
#include <oxq/format/writer.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> read_file(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<char> chars{std::istreambuf_iterator<char>{input},
                          std::istreambuf_iterator<char>{}};
  std::vector<std::byte> bytes;
  bytes.reserve(chars.size());
  for (const auto character : chars) {
    bytes.push_back(static_cast<std::byte>(character));
  }
  return bytes;
}

}  // namespace

int main() {
  const auto source = read_file(std::filesystem::path{OXQF_VECTOR_DIRECTORY} /
                                "variation-zh.oxq");
  const auto read = oxq::format::read_oxq(source);
  if (!std::holds_alternative<oxq::format::ReaderResult>(read)) {
    return 1;
  }
  const auto original = std::get<oxq::format::ReaderResult>(read).game;
  auto opened = oxq::editor::open_document(original);
  if (!std::holds_alternative<oxq::editor::EditorSession>(opened)) {
    return 2;
  }
  auto session = std::get<oxq::editor::EditorSession>(std::move(opened));
  oxq::editor::DebugConsole console{session};

  const auto annotation = console.execute(
      "annotation set --node 0 --kind comment --text \"集成往返验证\"",
      "integration-1");
  if (!annotation.ok || annotation.revision != 1 ||
      !console.execute("undo", "integration-2").ok ||
      !(session.export_document() == original) || session.state().dirty ||
      !console.execute("redo", "integration-3").ok ||
      !session.state().dirty) {
    return 3;
  }

  const auto tree = console.execute("tree --depth 8 --nodes 1000",
                                    "integration-4");
  const auto validation = console.execute("validate --state", "integration-5");
  if (!tree.ok || !tree.graph.has_value() || tree.graph->nodes.empty() ||
      !validation.ok || !validation.validation_issues.empty()) {
    return 4;
  }

  const auto exported = session.export_document();
  const auto written = oxq::format::write_oxq(exported);
  if (!std::holds_alternative<std::vector<std::byte>>(written)) {
    return 5;
  }
  const auto reread = oxq::format::read_oxq(
      std::get<std::vector<std::byte>>(written));
  if (!std::holds_alternative<oxq::format::ReaderResult>(reread) ||
      !(std::get<oxq::format::ReaderResult>(reread).game == exported)) {
    return 6;
  }
  return 0;
}
