#pragma once

#include <oxq/editor/command.hpp>
#include <oxq/editor/error.hpp>
#include <oxq/format/document.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace oxq::editor {

enum class ValidationPolicy {
  structural,
  state_consistent,
};

struct SessionOptions {
  ValidationPolicy validation_policy{ValidationPolicy::state_consistent};
  std::size_t max_compound_commands{1024};
  std::size_t max_history_entries{1024};
  std::size_t max_history_bytes{64U * 1024U * 1024U};
  std::size_t max_position_cache_entries{4096};
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

struct PositionCacheStats {
  std::size_t entries{0};
  std::uint64_t hits{0};
  std::uint64_t misses{0};

  friend bool operator==(const PositionCacheStats&,
                         const PositionCacheStats&) = default;
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
  [[nodiscard]] std::variant<format::Position, EditorError> position_at(
      format::NodeId node) const;
  [[nodiscard]] PositionCacheStats position_cache_stats() const noexcept;

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

  struct StoredNode {
    std::size_t storage_index{0};
    format::MoveNode node;
  };

  enum class HistoryKind {
    insert,
    delete_subtree,
    replace_move,
    reorder_variation,
    set_annotations,
    set_metadata,
    compound,
  };

  struct HistoryEntry {
    HistoryKind kind{HistoryKind::insert};
    std::vector<StoredNode> nodes;
    format::NodeId root{0};
    format::NodeId parent{0};
    std::size_t sibling_index{0};
    format::NodeId before_current{0};
    format::NodeId after_current{0};
    std::uint64_t before_token{0};
    std::uint64_t after_token{0};
    std::optional<format::Move> before_move;
    std::optional<format::Move> after_move;
    std::optional<std::size_t> reordered_index;
    std::optional<std::vector<format::Annotation>> before_annotations;
    std::optional<std::vector<format::Annotation>> after_annotations;
    std::optional<format::GameMetadata> before_metadata;
    std::optional<format::GameMetadata> after_metadata;
    std::optional<format::GameDocument> before_document;
    std::optional<format::GameDocument> after_document;
    std::optional<ChangeSet> forward_changes;
  };

  format::GameDocument document_;
  SessionState state_;
  SessionOptions options_;
  std::vector<HistoryEntry> undo_history_;
  std::vector<HistoryEntry> redo_history_;
  std::uint64_t current_document_token_{0};
  std::uint64_t saved_document_token_{0};
  std::uint64_t next_document_token_{1};
  mutable std::unordered_map<format::NodeId, format::Position> position_cache_;
  mutable std::uint64_t position_cache_hits_{0};
  mutable std::uint64_t position_cache_misses_{0};

  [[nodiscard]] bool restore_subtree(format::GameDocument& document,
                                     const HistoryEntry& entry) const;
  [[nodiscard]] std::size_t estimate_history_bytes(
      const HistoryEntry& entry) const noexcept;
  [[nodiscard]] Status store_history(HistoryEntry entry);
  void invalidate_position_cache(const std::vector<format::NodeId>& nodes);
  void invalidate_position_subtree(format::NodeId root);

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
