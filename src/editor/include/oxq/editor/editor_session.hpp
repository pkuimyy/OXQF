#pragma once

#include <oxq/editor/command.hpp>
#include <oxq/editor/error.hpp>
#include <oxq/format/document.hpp>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace oxq::editor {

enum class ValidationPolicy {
  structural,
  state_consistent,
};

struct SessionOptions {
  ValidationPolicy validation_policy{ValidationPolicy::state_consistent};
};

struct SessionState {
  format::NodeId current_node{0};
  bool dirty{false};
  std::uint64_t revision{0};
  bool can_undo{false};
  bool can_redo{false};

  friend bool operator==(const SessionState&, const SessionState&) = default;
};

struct SessionSnapshot {
  SessionState state;
  format::Position position;

  friend bool operator==(const SessionSnapshot&, const SessionSnapshot&) = default;
};

class EditorSession {
 public:
  EditorSession(const EditorSession&) = delete;
  EditorSession& operator=(const EditorSession&) = delete;
  EditorSession(EditorSession&&) noexcept = default;
  EditorSession& operator=(EditorSession&&) noexcept = default;
  ~EditorSession() = default;

  [[nodiscard]] const format::GameDocument& document() const noexcept;
  [[nodiscard]] format::GameDocument export_document() const;
  [[nodiscard]] const SessionState& state() const noexcept;
  [[nodiscard]] std::variant<SessionSnapshot, EditorError> snapshot() const;

  [[nodiscard]] std::variant<CommandResult, EditorError> execute(
      Command command,
      std::optional<std::uint64_t> expected_revision = std::nullopt);
  [[nodiscard]] std::variant<CommandResult, EditorError> undo(
      std::optional<std::uint64_t> expected_revision = std::nullopt);
  [[nodiscard]] std::variant<CommandResult, EditorError> redo(
      std::optional<std::uint64_t> expected_revision = std::nullopt);

  [[nodiscard]] Status checkout(
      format::NodeId node,
      std::optional<std::uint64_t> expected_revision = std::nullopt);
  [[nodiscard]] Status mark_saved(std::uint64_t revision);

 private:
  explicit EditorSession(format::GameDocument document);

  struct InsertHistoryEntry {
    format::MoveNode node;
    std::size_t sibling_index{0};
    format::NodeId before_current{0};
    format::NodeId after_current{0};
    std::uint64_t before_token{0};
    std::uint64_t after_token{0};
  };

  format::GameDocument document_;
  SessionState state_;
  SessionOptions options_;
  std::vector<InsertHistoryEntry> undo_history_;
  std::vector<InsertHistoryEntry> redo_history_;
  std::uint64_t current_document_token_{0};
  std::uint64_t saved_document_token_{0};
  std::uint64_t next_document_token_{1};

  friend std::variant<EditorSession, EditorError> open_document(
      format::GameDocument, SessionOptions);
};

using OpenOutcome = std::variant<EditorSession, EditorError>;
using SnapshotOutcome = std::variant<SessionSnapshot, EditorError>;
using CommandOutcome = std::variant<CommandResult, EditorError>;

[[nodiscard]] OpenOutcome open_document(
    format::GameDocument document,
    SessionOptions options = {});

}  // namespace oxq::editor
