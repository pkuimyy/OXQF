#include <oxq/editor/command.hpp>
#include <oxq/editor/editor_session.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace {

using oxq::editor::CommandResult;
using oxq::editor::DeleteSubtreeCommand;
using oxq::editor::EditorSession;
using oxq::editor::InsertMoveCommand;
using oxq::editor::ReorderVariationCommand;
using oxq::editor::ReplaceMoveCommand;
using oxq::editor::SessionOptions;
using oxq::editor::SetAnnotationsCommand;
using oxq::editor::SetMetadataCommand;
using oxq::editor::ValidationPolicy;
using oxq::format::Annotation;
using oxq::format::GameDocument;
using oxq::format::GameMetadata;
using oxq::format::Move;
using oxq::format::NodeId;

class Random {
 public:
  [[nodiscard]] std::uint32_t next() noexcept {
    state_ = state_ * 1664525U + 1013904223U;
    return state_;
  }

 private:
  std::uint32_t state_{0x4f585146U};
};

[[nodiscard]] GameDocument empty_document() {
  GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000038");
  return document;
}

[[nodiscard]] Move unused_move(const GameDocument& document, NodeId parent,
                               std::uint32_t& serial) {
  const auto* parent_node = document.move_tree.findNode(parent);
  while (true) {
    const auto value = serial++;
    const auto from = static_cast<std::uint8_t>(value % 90U);
    auto to = static_cast<std::uint8_t>((value * 37U + 1U) % 90U);
    if (to == from) {
      to = static_cast<std::uint8_t>((to + 1U) % 90U);
    }
    const Move candidate{from, to};
    const auto duplicate = std::ranges::any_of(
        parent_node->children, [&](const NodeId child) {
          return document.move_tree.findNode(child)->move == candidate;
        });
    if (!duplicate) {
      return candidate;
    }
  }
}

}  // namespace

int main() {
  const auto initial = empty_document();
  SessionOptions options;
  options.validation_policy = ValidationPolicy::structural;
  options.max_history_entries = 1024;
  auto opened = oxq::editor::open_document(initial, options);
  if (!std::holds_alternative<EditorSession>(opened)) {
    return 1;
  }
  auto session = std::get<EditorSession>(std::move(opened));
  Random random;
  std::uint32_t move_serial = 0;
  std::size_t history_count = 0;

  for (std::size_t step = 0; step < 400; ++step) {
    const auto& nodes = session.document().move_tree.nodes;
    const auto choice = random.next() % 6U;
    oxq::editor::Command command;
    if (choice == 0 || nodes.size() == 1) {
      const auto parent = nodes[random.next() % nodes.size()].id;
      command = InsertMoveCommand{
          parent, unused_move(session.document(), parent, move_serial), {}};
    } else if (choice == 1) {
      const auto node = nodes[1 + random.next() % (nodes.size() - 1)].id;
      command = DeleteSubtreeCommand{node};
    } else if (choice == 2) {
      const auto node = nodes[1 + random.next() % (nodes.size() - 1)].id;
      const auto* target = session.document().move_tree.findNode(node);
      command = ReplaceMoveCommand{
          node, unused_move(session.document(), *target->parent, move_serial)};
    } else if (choice == 3) {
      const auto node = nodes[1 + random.next() % (nodes.size() - 1)].id;
      const auto* target = session.document().move_tree.findNode(node);
      const auto* parent =
          session.document().move_tree.findNode(*target->parent);
      command = ReorderVariationCommand{
          node, static_cast<std::size_t>(random.next() % parent->children.size())};
    } else if (choice == 4) {
      const auto node = nodes[random.next() % nodes.size()].id;
      Annotation annotation;
      annotation.text = "annotation-" + std::to_string(step);
      command = SetAnnotationsCommand{node, {std::move(annotation)}};
    } else {
      GameMetadata metadata = session.document().metadata;
      metadata.title = "position-" + std::to_string(step);
      command = SetMetadataCommand{std::move(metadata)};
    }

    const auto before_revision = session.state().revision;
    const auto result = session.execute(std::move(command), before_revision);
    if (!std::holds_alternative<CommandResult>(result) ||
        !session.document().move_tree.validateInvariants()) {
      return 2;
    }
    if (session.state().revision != before_revision) {
      ++history_count;
    }
  }

  const auto final_document = session.export_document();
  for (std::size_t index = 0; index < history_count; ++index) {
    const auto undone = session.undo(session.state().revision);
    if (!std::holds_alternative<CommandResult>(undone) ||
        !session.document().move_tree.validateInvariants()) {
      return 3;
    }
  }
  if (!(session.export_document() == initial) || session.state().can_undo ||
      !session.state().can_redo || session.state().dirty) {
    return 4;
  }

  for (std::size_t index = 0; index < history_count; ++index) {
    const auto redone = session.redo(session.state().revision);
    if (!std::holds_alternative<CommandResult>(redone) ||
        !session.document().move_tree.validateInvariants()) {
      return 5;
    }
  }
  if (!(session.export_document() == final_document) ||
      !session.state().can_undo || session.state().can_redo ||
      !session.state().dirty) {
    return 6;
  }
  return 0;
}
