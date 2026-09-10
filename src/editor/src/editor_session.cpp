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

CommandOutcome EditorSession::execute(
    Command command, std::optional<std::uint64_t> expected_revision) {
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    return revision_error(*expected_revision, state_.revision,
                          "session revision does not match the expected revision");
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
  const auto inserted_node = *working.move_tree.findNode(created);
  document_ = std::move(working);
  state_.current_node = created;
  ++state_.revision;
  current_document_token_ = after_token;
  state_.dirty = current_document_token_ != saved_document_token_;
  undo_history_.push_back({inserted_node, insertion_index, before_current, created,
                           before_token, after_token});
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
  if (!working.move_tree.removeNode(entry.node.id) ||
      format::has_errors(validate_document(working, options_))) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = "could not undo the inserted move";
    error.node_id = entry.node.id;
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
  changes.removed = {entry.node.id};
  changes.reordered_parents = {*entry.node.parent};
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
  if (!working.move_tree.restoreNode(entry.node)) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = "could not restore the inserted move";
    error.node_id = entry.node.id;
    return error;
  }
  auto* parent = working.move_tree.findNode(*entry.node.parent);
  if (parent == nullptr || entry.sibling_index >= parent->children.size()) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = "could not restore the move's sibling position";
    error.node_id = entry.node.id;
    return error;
  }
  auto& children = parent->children;
  const auto restored = children.back();
  children.pop_back();
  children.insert(children.begin() + static_cast<std::ptrdiff_t>(entry.sibling_index),
                  restored);
  if (format::has_errors(validate_document(working, options_))) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = "restored move failed document validation";
    error.node_id = entry.node.id;
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
  changes.inserted = {entry.node.id};
  changes.reordered_parents = {*entry.node.parent};
  changes.selection_changed = entry.before_current != entry.after_current;
  return CommandResult{state_.revision, std::move(changes), entry.node.id};
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
