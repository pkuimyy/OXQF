#include <oxq/editor/editor_session.hpp>

#include <oxq/format/state_validation.hpp>
#include <oxq/format/validation.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace oxq::editor {
namespace {

[[nodiscard]] format::Side opposite(format::Side side) noexcept {
  return side == format::Side::red ? format::Side::black : format::Side::red;
}

[[nodiscard]] EditorError node_error(format::NodeId node) {
  EditorError error;
  error.code = EditorErrorCode::node_not_found;
  error.message = "node " + std::to_string(node) + " does not exist";
  error.node_id = node;
  return error;
}

[[nodiscard]] EditorError position_error(format::NodeId node,
                                         std::string message) {
  EditorError error;
  error.code = EditorErrorCode::invalid_position;
  error.message = std::move(message);
  error.node_id = node;
  return error;
}

[[nodiscard]] EditorError revision_error(std::uint64_t expected,
                                         std::uint64_t actual,
                                         std::string message) {
  EditorError error;
  error.code = EditorErrorCode::revision_conflict;
  error.message = std::move(message);
  error.expected_revision = expected;
  error.actual_revision = actual;
  return error;
}

[[nodiscard]] std::vector<format::ValidationIssue> validate_document(
    const format::GameDocument& document, const SessionOptions& options) {
  return options.validation_policy == ValidationPolicy::structural
             ? format::validate(document)
             : format::validate_state(document);
}

[[nodiscard]] std::variant<format::Position, EditorError> position_at(
    const format::GameDocument& document, format::NodeId target) {
  std::unordered_map<format::NodeId, const format::MoveNode*> nodes;
  nodes.reserve(document.move_tree.nodes.size());
  for (const auto& node : document.move_tree.nodes) {
    nodes.emplace(node.id, &node);
  }

  const auto target_iterator = nodes.find(target);
  if (target_iterator == nodes.end()) {
    return node_error(target);
  }

  std::vector<const format::MoveNode*> path;
  const format::MoveNode* node = target_iterator->second;
  while (node->parent.has_value()) {
    path.push_back(node);
    const auto parent = nodes.find(*node->parent);
    if (parent == nodes.end()) {
      return position_error(node->id, "node parent does not exist");
    }
    node = parent->second;
  }
  std::ranges::reverse(path);

  std::array<std::optional<format::Piece>, 90> board;
  for (const auto& piece : document.initial_position.pieces) {
    board[piece.square] = piece;
  }

  format::Side side_to_move = document.initial_position.side_to_move;
  std::uint16_t fullmove_number = document.initial_position.fullmove_number;
  for (const auto* move_node : path) {
    if (!move_node->move.has_value()) {
      return position_error(move_node->id, "non-root node has no move");
    }
    const auto move = *move_node->move;
    if (!board[move.from_square].has_value()) {
      return position_error(move_node->id, "move origin does not contain a piece");
    }
    auto moving = *board[move.from_square];
    if (moving.side != side_to_move) {
      return position_error(move_node->id, "move origin belongs to the other side");
    }
    if (board[move.to_square].has_value() &&
        board[move.to_square]->side == moving.side) {
      return position_error(move_node->id, "move destination contains a friendly piece");
    }
    moving.square = move.to_square;
    board[move.to_square] = moving;
    board[move.from_square].reset();
    if (side_to_move == format::Side::black) {
      if (fullmove_number == std::numeric_limits<std::uint16_t>::max()) {
        EditorError error;
        error.code = EditorErrorCode::move_number_overflow;
        error.message = "fullmove number exceeds the format position range";
        error.node_id = move_node->id;
        return error;
      }
      ++fullmove_number;
    }
    side_to_move = opposite(side_to_move);
  }

  format::Position position;
  position.side_to_move = side_to_move;
  position.fullmove_number = fullmove_number;
  for (const auto& square : board) {
    if (square.has_value()) {
      position.pieces.push_back(*square);
    }
  }
  return position;
}

}  // namespace

EditorSession::EditorSession(format::GameDocument document)
    : document_(std::move(document)) {}

const format::GameDocument& EditorSession::document() const noexcept {
  return document_;
}

format::GameDocument EditorSession::export_document() const {
  return document_;
}

const SessionState& EditorSession::state() const noexcept {
  return state_;
}

SnapshotOutcome EditorSession::snapshot() const {
  auto position = position_at(document_, state_.current_node);
  if (std::holds_alternative<EditorError>(position)) {
    return std::get<EditorError>(std::move(position));
  }
  return SessionSnapshot{state_, std::get<format::Position>(std::move(position))};
}

bool EditorSession::restore_subtree(format::GameDocument& document,
                                    const HistoryEntry& entry) const {
  auto& storage = document.move_tree.nodes;
  for (const auto& stored : entry.nodes) {
    if (stored.storage_index > storage.size()) {
      return false;
    }
    storage.insert(storage.begin() + static_cast<std::ptrdiff_t>(stored.storage_index),
                   stored.node);
  }
  if (!document.move_tree.rebuildIndex()) {
    return false;
  }
  auto* parent = document.move_tree.findNode(entry.parent);
  if (parent == nullptr || entry.sibling_index > parent->children.size()) {
    return false;
  }
  parent->children.insert(
      parent->children.begin() + static_cast<std::ptrdiff_t>(entry.sibling_index),
      entry.root);
  return document.move_tree.validateInvariants();
}

CommandOutcome EditorSession::execute(
    Command command, std::optional<std::uint64_t> expected_revision) {
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    return revision_error(*expected_revision, state_.revision,
                          "session revision does not match the expected revision");
  }

  if (std::holds_alternative<DeleteSubtreeCommand>(command)) {
    const auto& deletion = std::get<DeleteSubtreeCommand>(command);
    if (deletion.node == 0) {
      EditorError error;
      error.code = EditorErrorCode::invalid_argument;
      error.message = "the root node cannot be deleted";
      error.node_id = deletion.node;
      return error;
    }
    const auto* target = document_.move_tree.findNode(deletion.node);
    if (target == nullptr) {
      return node_error(deletion.node);
    }
    const auto parent_id = *target->parent;
    const auto* parent = document_.move_tree.findNode(parent_id);
    if (parent == nullptr) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "deleted subtree has no valid parent";
      error.node_id = deletion.node;
      return error;
    }
    const auto sibling = std::ranges::find(parent->children, deletion.node);
    if (sibling == parent->children.end()) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "deleted subtree is absent from its parent's children";
      error.node_id = deletion.node;
      return error;
    }
    const auto sibling_index =
        static_cast<std::size_t>(std::distance(parent->children.begin(), sibling));

    std::unordered_set<format::NodeId> removed_ids;
    std::vector<format::NodeId> pending{deletion.node};
    while (!pending.empty()) {
      const auto current = pending.back();
      pending.pop_back();
      if (!removed_ids.insert(current).second) {
        continue;
      }
      const auto* removed = document_.move_tree.findNode(current);
      if (removed == nullptr) {
        return node_error(current);
      }
      pending.insert(pending.end(), removed->children.begin(), removed->children.end());
    }

    std::vector<StoredNode> stored_nodes;
    std::vector<format::NodeId> removed_order;
    stored_nodes.reserve(removed_ids.size());
    removed_order.reserve(removed_ids.size());
    for (std::size_t index = 0; index < document_.move_tree.nodes.size(); ++index) {
      const auto& candidate = document_.move_tree.nodes[index];
      if (removed_ids.contains(candidate.id)) {
        stored_nodes.push_back({index, candidate});
        removed_order.push_back(candidate.id);
      }
    }

    format::GameDocument working = document_;
    if (!working.move_tree.removeNode(deletion.node)) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "could not remove the requested subtree";
      error.node_id = deletion.node;
      return error;
    }
    auto issues = validate_document(working, options_);
    if (format::has_errors(issues)) {
      EditorError error;
      error.code = EditorErrorCode::validation_failed;
      error.message = "subtree deletion failed document validation";
      error.validation_issues = std::move(issues);
      error.node_id = deletion.node;
      return error;
    }

    const auto before_revision = state_.revision;
    const auto before_current = state_.current_node;
    const auto after_current = removed_ids.contains(before_current) ? parent_id : before_current;
    const auto before_token = current_document_token_;
    const auto after_token = next_document_token_++;
    document_ = std::move(working);
    state_.current_node = after_current;
    ++state_.revision;
    current_document_token_ = after_token;
    state_.dirty = current_document_token_ != saved_document_token_;
    undo_history_.push_back({HistoryKind::delete_subtree, std::move(stored_nodes),
                             deletion.node, parent_id, sibling_index, before_current,
                             after_current, before_token, after_token});
    redo_history_.clear();
    state_.can_undo = true;
    state_.can_redo = false;

    ChangeSet changes;
    changes.before_revision = before_revision;
    changes.after_revision = state_.revision;
    changes.removed = std::move(removed_order);
    changes.reordered_parents = {parent_id};
    changes.selection_changed = before_current != after_current;
    return CommandResult{state_.revision, std::move(changes), std::nullopt};
  }

  const auto& insert = std::get<InsertMoveCommand>(command);
  const auto* parent = document_.move_tree.findNode(insert.parent);
  if (parent == nullptr) {
    return node_error(insert.parent);
  }
  if (insert.move.from_square >= 90 || insert.move.to_square >= 90 ||
      insert.move.from_square == insert.move.to_square) {
    EditorError error;
    error.code = EditorErrorCode::invalid_argument;
    error.message = "move squares must be distinct values in the range 0..89";
    return error;
  }
  const std::size_t insertion_index =
      insert.sibling_index.value_or(parent->children.size());
  if (insertion_index > parent->children.size()) {
    EditorError error;
    error.code = EditorErrorCode::invalid_argument;
    error.message = "sibling index is outside the parent's child range";
    error.node_id = insert.parent;
    return error;
  }
  for (const auto child_id : parent->children) {
    const auto* child = document_.move_tree.findNode(child_id);
    if (child != nullptr && child->move == std::optional{insert.move}) {
      EditorError error;
      error.code = EditorErrorCode::duplicate_move;
      error.message = "the parent already has an identical child move";
      error.node_id = child_id;
      return error;
    }
  }

  format::GameDocument working = document_;
  format::NodeId created{};
  try {
    created = working.move_tree.addNode(insert.parent, insert.move);
    auto* working_parent = working.move_tree.findNode(insert.parent);
    if (working_parent == nullptr) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "inserted move lost its parent";
      return error;
    }
    auto& children = working_parent->children;
    const auto appended = children.back();
    children.pop_back();
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(insertion_index),
                    appended);
  } catch (const std::bad_alloc&) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "not enough memory to insert the move";
    return error;
  } catch (const std::exception& exception) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = exception.what();
    return error;
  }

  auto issues = validate_document(working, options_);
  if (format::has_errors(issues)) {
    EditorError error;
    error.code = EditorErrorCode::validation_failed;
    error.message = "inserted move failed document validation";
    error.validation_issues = std::move(issues);
    error.node_id = created;
    return error;
  }

  const auto before_revision = state_.revision;
  const auto before_current = state_.current_node;
  const auto before_token = current_document_token_;
  const auto after_token = next_document_token_++;
  const auto inserted_index = *working.move_tree.storageIndex(created);
  const auto inserted_node = *working.move_tree.findNode(created);
  document_ = std::move(working);
  state_.current_node = created;
  ++state_.revision;
  current_document_token_ = after_token;
  state_.dirty = current_document_token_ != saved_document_token_;
  undo_history_.push_back({HistoryKind::insert,
                           {{inserted_index, inserted_node}},
                           created,
                           insert.parent,
                           insertion_index,
                           before_current,
                           created,
                           before_token,
                           after_token});
  redo_history_.clear();
  state_.can_undo = true;
  state_.can_redo = false;

  ChangeSet changes;
  changes.before_revision = before_revision;
  changes.after_revision = state_.revision;
  changes.inserted = {created};
  changes.reordered_parents = {insert.parent};
  changes.selection_changed = before_current != created;
  return CommandResult{state_.revision, std::move(changes), created};
}

CommandOutcome EditorSession::undo(
    std::optional<std::uint64_t> expected_revision) {
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    return revision_error(*expected_revision, state_.revision,
                          "session revision does not match the expected revision");
  }
  if (undo_history_.empty()) {
    EditorError error;
    error.code = EditorErrorCode::history_empty;
    error.message = "there is no command to undo";
    return error;
  }

  const auto entry = undo_history_.back();
  format::GameDocument working = document_;
  const bool mutation_succeeded =
      entry.kind == HistoryKind::insert
          ? working.move_tree.removeNode(entry.root)
          : restore_subtree(working, entry);
  if (!mutation_succeeded ||
      format::has_errors(validate_document(working, options_))) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = "could not undo the editor command";
    error.node_id = entry.root;
    return error;
  }

  const auto before_revision = state_.revision;
  document_ = std::move(working);
  state_.current_node = entry.before_current;
  ++state_.revision;
  current_document_token_ = entry.before_token;
  state_.dirty = current_document_token_ != saved_document_token_;
  undo_history_.pop_back();
  redo_history_.push_back(entry);
  state_.can_undo = !undo_history_.empty();
  state_.can_redo = true;

  ChangeSet changes;
  changes.before_revision = before_revision;
  changes.after_revision = state_.revision;
  for (const auto& stored : entry.nodes) {
    (entry.kind == HistoryKind::insert ? changes.removed : changes.inserted)
        .push_back(stored.node.id);
  }
  changes.reordered_parents = {entry.parent};
  changes.selection_changed = entry.before_current != entry.after_current;
  return CommandResult{state_.revision, std::move(changes), std::nullopt};
}

CommandOutcome EditorSession::redo(
    std::optional<std::uint64_t> expected_revision) {
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    return revision_error(*expected_revision, state_.revision,
                          "session revision does not match the expected revision");
  }
  if (redo_history_.empty()) {
    EditorError error;
    error.code = EditorErrorCode::history_empty;
    error.message = "there is no command to redo";
    return error;
  }

  const auto entry = redo_history_.back();
  format::GameDocument working = document_;
  const bool mutation_succeeded =
      entry.kind == HistoryKind::insert
          ? restore_subtree(working, entry)
          : working.move_tree.removeNode(entry.root);
  if (!mutation_succeeded ||
      format::has_errors(validate_document(working, options_))) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = "could not redo the editor command";
    error.node_id = entry.root;
    return error;
  }

  const auto before_revision = state_.revision;
  document_ = std::move(working);
  state_.current_node = entry.after_current;
  ++state_.revision;
  current_document_token_ = entry.after_token;
  state_.dirty = current_document_token_ != saved_document_token_;
  redo_history_.pop_back();
  undo_history_.push_back(entry);
  state_.can_undo = true;
  state_.can_redo = !redo_history_.empty();

  ChangeSet changes;
  changes.before_revision = before_revision;
  changes.after_revision = state_.revision;
  for (const auto& stored : entry.nodes) {
    (entry.kind == HistoryKind::insert ? changes.inserted : changes.removed)
        .push_back(stored.node.id);
  }
  changes.reordered_parents = {entry.parent};
  changes.selection_changed = entry.before_current != entry.after_current;
  const auto created = entry.kind == HistoryKind::insert
                           ? std::optional<format::NodeId>{entry.root}
                           : std::nullopt;
  return CommandResult{state_.revision, std::move(changes), created};
}

Status EditorSession::checkout(format::NodeId node,
                               std::optional<std::uint64_t> expected_revision) {
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    return revision_error(*expected_revision, state_.revision,
                          "session revision does not match the expected revision");
  }
  if (document_.move_tree.findNode(node) == nullptr) {
    return node_error(node);
  }
  if (node != state_.current_node) {
    state_.current_node = node;
    ++state_.revision;
  }
  return std::nullopt;
}

Status EditorSession::mark_saved(std::uint64_t revision) {
  if (revision != state_.revision) {
    return revision_error(revision, state_.revision,
                          "saved revision is no longer current");
  }
  saved_document_token_ = current_document_token_;
  state_.dirty = false;
  return std::nullopt;
}

OpenOutcome open_document(format::GameDocument document, SessionOptions options) {
  auto issues = validate_document(document, options);
  if (format::has_errors(issues)) {
    EditorError error;
    error.code = EditorErrorCode::invalid_document;
    error.message = "document failed editor session validation";
    error.validation_issues = std::move(issues);
    return error;
  }
  EditorSession session{std::move(document)};
  session.options_ = options;
  return session;
}

}  // namespace oxq::editor
